/* Private DS3 desktop host. SDL owns Wayland window and input on main. */
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ds3_platform.h"
#include "ds3_audio_trace.h"
#include "hle.h"
#include "me1_platform.h"
#include "desktop-audio.inc"
#include "keymap.inc"

extern void me1gl_set_window(void *);
extern int me1gl_get_view(int *, int *, int *, int *, int *, int *);
extern void (*ds3_desktop_mode_hook)(unsigned, unsigned, int);
extern _Atomic unsigned ds3_presented_drawable;

static SDL_Window *win;
static Me1Window native;
static atomic_uint_fast64_t mode;
static atomic_int done;
static int result;
static unsigned char keys[SDL_NUM_SCANCODES], buttons[7];
static int pointer_x, pointer_y, pointer_w, pointer_h, client_x, client_y;
static uint64_t close_key_at;
static int frame_trace;
static const char *game, *docs;
static const char *window_system, *renderer;
static int vulkan;

static void publish_mode(unsigned w, unsigned h, int fullscreen) {
    if (w && h && w <= 16384 && h <= 16384)
        atomic_store(&mode, ((uint64_t)w << 32) | ((uint64_t)h << 1) | !!fullscreen);
}
static void presented(void *unused) {
    (void)unused;
    static atomic_flag first = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set(&first)) fprintf(stderr,"[ds3-%s] first real %s frame presented\n",window_system,vulkan?"Vulkan":"GLES");
    if (frame_trace) {
        struct timespec t;
        if (!clock_gettime(CLOCK_MONOTONIC, &t))
            fprintf(stderr,"[ds3-frame] %llu\n",(unsigned long long)t.tv_sec*1000000000ull+(unsigned long long)t.tv_nsec);
    }
}
void ds3_linux_audio_frame_limit_exit(void) { ds_audio_close(); }
static void game_exit(int code) { (void)code; ds_audio_close(); }
static void *run_game(void *unused) {
    (void)unused;
    static const Ds3Platform platform = {.present=presented, .audio_open=audio_open, .audio_start=audio_start, .exit=game_exit};
    result = ds3_main(&platform, game, docs);
    atomic_store(&done, 1);
    return NULL;
}
static void drawable(void) {
    int w, h;
    if (vulkan) SDL_Vulkan_GetDrawableSize(win, &w, &h);
    else SDL_GetWindowSizeInPixels(win, &w, &h);
    uint64_t prev = native.drawable;
    if (w > 0 && h > 0 && w <= 65535 && h <= 65535 &&
        (w != (int)me1_width(prev) || h != (int)me1_height(prev))) {
        unsigned gen=me1_generation(prev)+1;
        native.drawable=((uint64_t)gen<<32)|((uint64_t)h<<16)|(unsigned)w;
        fprintf(stderr, "[ds3-wayland] drawable=%dx%d generation=%u\n", w, h, gen);
    }
}
static int fullscreen(int on) {
    if (SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)) {
        fprintf(stderr, "[ds3-wayland] fullscreen failed: %s\n", SDL_GetError());
        return 0;
    }
    fprintf(stderr, "[ds3-wayland] fullscreen requested=%d\n", on);
    return 1;
}
static void release_input(void) {
    for (int i=0; i<SDL_NUM_SCANCODES; ++i) if (keys[i]) {
        int vk, dik;
        map_key(i, &vk, &dik);
        hle_ui_key(vk, dik, 0, 0);
        keys[i]=0;
    }
    for (int i=1; i<=5; i+=2) if (buttons[i]) {
        hle_ui_mouse_button(i+1);
        buttons[i]=0;
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    pointer_w=pointer_h=0;
    SDL_ShowCursor(SDL_ENABLE);
}
static void cursor(void) {
    int x,y,w,h;
    int hidden = hle_ui_cursor(&x,&y,&w,&h)==0;
    int want = hidden && (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);
    static int failed;
    if (!!SDL_GetRelativeMouseMode()!=!!want && !(failed && want)) {
        if (SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE)) {
            failed=1;
            fprintf(stderr, "[ds3-wayland] relative mouse unavailable: %s\n", SDL_GetError());
        } else {
            pointer_w=pointer_h=0;
            fprintf(stderr, "[ds3-wayland] relative mouse=%d\n", !!want);
        }
    }
    if (!want) failed=0;
}
static int frame_clamp(int64_t n,int limit) { return n<0?0:n>=limit?limit-1:(int)n; }
static void captured_motion(int dx,int dy) {
    if (!dx && !dy) return;
    int vx,vy,vw,vh,fw,fh;
    if (ds3_presented_drawable!=me1_generation(native.drawable) ||
        !me1gl_get_view(&vx,&vy,&vw,&vh,&fw,&fh) || fw<1 || fh<1) {
        hle_ui_mouse_rel(dx,dy);
        return;
    }
    int x,y,w,h;
    if (hle_ui_cursor(&x,&y,&w,&h)<0) { hle_ui_mouse_rel(dx,dy); return; }
    if (fw!=pointer_w || fh!=pointer_h || x!=client_x || y!=client_y) {
        pointer_x=frame_clamp((int64_t)x*fw/w,fw);
        pointer_y=frame_clamp((int64_t)y*fh/h,fh);
        pointer_w=fw; pointer_h=fh;
    }
    pointer_x=frame_clamp((int64_t)pointer_x+dx,fw);
    pointer_y=frame_clamp((int64_t)pointer_y+dy,fh);
    client_x=(int)((int64_t)pointer_x*w/fw);
    client_y=(int)((int64_t)pointer_y*h/fh);
    hle_ui_pointer_rel(pointer_x,pointer_y,fw,fh,dx,dy);
}
static int to_frame(int x,int y,int *fx,int *fy,int *fw,int *fh) {
    int vx,vy,vw,vh,w,h;
    uint64_t draw=native.drawable;
    if (ds3_presented_drawable!=me1_generation(draw)) return 0;
    SDL_GetWindowSize(win,&w,&h);
    if (w<1 || h<1 || !me1gl_get_view(&vx,&vy,&vw,&vh,fw,fh) || vw<1 || vh<1 || *fw<1 || *fh<1) return 0;
    int a=(int)(((double)x*me1_width(draw)/w-vx)* *fw/vw);
    int b=(int)(((double)y*me1_height(draw)/h-vy)* *fh/vh);
    *fx=a<0?0:a>=*fw?*fw-1:a;
    *fy=b<0?0:b>=*fh?*fh-1:b;
    return 1;
}
static void close_window(int key) {
    uint64_t now=SDL_GetTicks64()+1;
    if (!key && close_key_at && now-close_key_at<250) return;
    if (key) close_key_at=now;
    hle_ui_close();
}
static void event(const SDL_Event *e) {
    switch(e->type) {
    case SDL_QUIT: close_window(0); break;
    case SDL_WINDOWEVENT:
        if (e->window.event==SDL_WINDOWEVENT_FOCUS_LOST) release_input();
        if (e->window.event==SDL_WINDOWEVENT_CLOSE) close_window(0);
        break;
    case SDL_KEYDOWN: case SDL_KEYUP: {
        SDL_Scancode sc=e->key.keysym.scancode;
        const int down=e->type==SDL_KEYDOWN;
        if (sc<=SDL_SCANCODE_UNKNOWN || sc>=SDL_NUM_SCANCODES) break;
        if (sc==SDL_SCANCODE_F4 && down && (e->key.keysym.mod & KMOD_ALT)) {
            if (!e->key.repeat) close_window(1);
            break;
        }
        if (sc==SDL_SCANCODE_F11) {
            if (down && !e->key.repeat) fullscreen(!(SDL_GetWindowFlags(win)&SDL_WINDOW_FULLSCREEN));
            break;
        }
        int vk,dik;
        map_key(sc,&vk,&dik);
        if ((vk || dik) && !e->key.repeat) {
            keys[sc]=down;
            hle_ui_key(vk,dik,down,0);
        }
        break;
    }
    case SDL_MOUSEMOTION: {
        int x,y,w,h;
        if (SDL_GetRelativeMouseMode()) captured_motion(e->motion.xrel,e->motion.yrel);
        else if (to_frame(e->motion.x,e->motion.y,&x,&y,&w,&h)) hle_ui_pointer(x,y,w,h,0);
        break;
    }
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
        int b=e->button.button==SDL_BUTTON_LEFT?1:e->button.button==SDL_BUTTON_RIGHT?3:e->button.button==SDL_BUTTON_MIDDLE?5:0;
        if (!b) break;
        buttons[b]=e->type==SDL_MOUSEBUTTONDOWN;
        int x,y,w,h,code=b+!buttons[b];
        if (b!=5 && !SDL_GetRelativeMouseMode() && to_frame(e->button.x,e->button.y,&x,&y,&w,&h)) hle_ui_pointer(x,y,w,h,code);
        else hle_ui_mouse_button(code);
        break;
    }
    case SDL_MOUSEWHEEL: hle_ui_wheel(120 * (e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e->wheel.y:e->wheel.y)); break;
    }
}

int main(int argc,char **argv) {
    int first=1;
    if (argc==5 && !strcmp(argv[1],"--language")) {
        const char* locale=!strcmp(argv[2],"ru")?"ru_RU":!strcmp(argv[2],"en")?"en_EN":NULL;
        if (!locale) { fprintf(stderr,"--language: expected ru or en\n"); return 2; }
        if (setenv("EAGameLocale",locale,1)) { perror("EAGameLocale"); return 2; }
        fprintf(stderr,"[ds3-wayland] EAGameLocale=%s\n",locale);
        first=3;
    }
    if (argc!=first+2) { fprintf(stderr,"usage: %s [--language ru|en] GAME_DIR DOCS_DIR\n",argv[0]); return 2; }
    game=argv[first]; docs=argv[first+1];
    ds3_audio_trace_init();
    const char *trace=getenv("DS3_FRAME_TRACE");
    frame_trace=trace && !strcmp(trace,"1");
    window_system=getenv("DS3_WINDOW_SYSTEM");
    if (!window_system || !*window_system) window_system="wayland";
    if (strcmp(window_system,"wayland") && strcmp(window_system,"x11")) {
        fputs("invalid DS3_WINDOW_SYSTEM: expected wayland or x11\n",stderr); return 2;
    }
    renderer=getenv("DS3_RENDERER");
    if (!renderer || !*renderer) renderer="vulkan";
    if (strcmp(renderer,"vulkan") && strcmp(renderer,"gles")) {
        fputs("invalid DS3_RENDERER: expected vulkan or gles\n",stderr); return 2;
    }
    vulkan=!strcmp(renderer,"vulkan");
    int wayland=!strcmp(window_system,"wayland");
    unsigned long visual=0;
    setenv("SDL_VIDEODRIVER",window_system,1);
    setenv("DS3_RENDERER",renderer,1);
    setenv("DS3_DESKTOP_STRICT","1",1);
    unsetenv("DS3_WAYLAND_STRICT");
    unsetenv("DS3_VK_DRIVER");
    if (!wayland) {
        void* x=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);
        int (*init)(void)=x?dlsym(x,"XInitThreads"):NULL;
        if (!init || !init()) goto fail;
    }
    if (SDL_Init(SDL_INIT_VIDEO)) goto fail;
    win=SDL_CreateWindow("Dead Space 3",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,
                         (vulkan?SDL_WINDOW_VULKAN:0)|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) goto fail;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win,&info)) goto fail;
    if (wayland && info.subsystem==SDL_SYSWM_WAYLAND && info.info.wl.display && info.info.wl.surface) {
        native.kind=ME1_WS_WAYLAND; native.display=info.info.wl.display; native.surface=info.info.wl.surface;
    } else if (!wayland && info.subsystem==SDL_SYSWM_X11 && info.info.x11.display && info.info.x11.window) {
        native.kind=ME1_WS_X11; native.display=info.info.x11.display; native.xwindow=info.info.x11.window;
        if (!vulkan) {
            void* x=dlopen("libX11.so.6",RTLD_NOW|RTLD_LOCAL);
            int (*attributes)(Display*,Window,XWindowAttributes*)=x?dlsym(x,"XGetWindowAttributes"):NULL;
            VisualID (*visual_id)(Visual*)=x?dlsym(x,"XVisualIDFromVisual"):NULL;
            XWindowAttributes a;
            if (!attributes || !visual_id || !attributes(native.display,native.xwindow,&a) || !a.visual) goto fail;
            visual=visual_id(a.visual);
        }
    } else goto fail;
    SDL_DisplayMode desktop;
    int display=SDL_GetWindowDisplayIndex(win);
    if (display>=0 && !SDL_GetDesktopDisplayMode(display,&desktop)) {
        char size[32];
        snprintf(size,sizeof size,"%dx%d",desktop.w,desktop.h);
        setenv("ME1_SCREEN",size,0);
    }
    drawable();
    if (!native.drawable) goto fail;
    fprintf(stderr,"[ds3-%s] native %s window\n",window_system,wayland?"Wayland":"X11");
    me1_platform_set(native.kind,native.display,visual);
    me1gl_set_window(&native);
    ds3_desktop_mode_hook=publish_mode;
    if (!audio_preflight()) { SDL_Quit(); return 2; }
    pthread_t thread;
    if (pthread_create(&thread,NULL,run_game,NULL)) goto fail;
    uint64_t last=0;
    while (!atomic_load(&done)) {
        ds3_audio_trace_drain();
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e,8)) {
            event(&e);
            for (unsigned i=0;i<127 && SDL_PollEvent(&e);++i) event(&e);
        }
        uint64_t next=atomic_load(&mode);
        if (next!=last) {
            unsigned w=next>>32, h=(uint32_t)next>>1;
            int full=next&1;
            fprintf(stderr,"[ds3-wayland] guest render=%ux%u fullscreen=%d\n",w,h,full);
            if (!last || (int)(last&1)!=full) {
                if (!!(SDL_GetWindowFlags(win)&SDL_WINDOW_FULLSCREEN)!=full) fullscreen(full);
            }
            if (!last && !full) SDL_SetWindowSize(win,(int)w,(int)h);
            last=next;
        }
        drawable();
        cursor();
    }
    pthread_join(thread,NULL);
    ds_audio_close();
    ds3_audio_trace_finish();
    release_input();
    /* Runtime owns detached replay/audio threads. End the process without
       destroying their SDL surface or audio device underneath them. */
    fflush(NULL);
    _Exit(result);
fail:
    ds_audio_close();
    fprintf(stderr,"[ds3-desktop] cannot initialize %s/%s window: %s\n",window_system,renderer,SDL_GetError());
    SDL_Quit();
    return 2;
}
