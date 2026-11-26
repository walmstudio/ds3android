/* Only the SDL capture state and presented geometry are synthetic. Exercise
 * the actual SDL event handler and observe the HLE cursor and message queue. */
#define main ds3_host_main
#ifdef DS3_POINTER_RED
#include "../../gen61-wayland-vulkan/linux/ds3_wayland.c"
#else
#include "ds3_wayland.c"
#endif
#undef main
#include <assert.h>

static int raw_x, raw_y, fw=640, fh=480;
SDL_bool __wrap_SDL_GetRelativeMouseMode(void) { return SDL_TRUE; }
int __wrap_me1gl_get_view(int *x,int *y,int *w,int *h,int *fwidth,int *fheight) {
    *x=*y=0; *w=*fwidth=fw; *h=*fheight=fh; return 1;
}
void __real_hle_input_mouse(int,int,int,unsigned);
void __wrap_hle_input_mouse(int dx,int dy,int wheel,unsigned buttons) {
    raw_x+=dx; raw_y+=dy;
    __real_hle_input_mouse(dx,dy,wheel,buttons);
}
#define U(n,...) rt_call(&g_cpu,hle_resolve_impl("user32.dll",n),sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__})
static void motion(int dx, int dy) {
    SDL_Event e={.motion={.type=SDL_MOUSEMOTION,.xrel=dx,.yrel=dy}};
    event(&e);
}
int main(void) {
    rt_init();
    uint32_t name=ghalloc(32,1), pt=ghalloc(8,1), msg=ghalloc(28,1);
    memcpy(M+name,"pointer fixture",16);
    uint32_t hwnd=U("CreateWindowExA",0,name,0,0x10000000,0,0,640,480,0,0,0,0);
    assert(hwnd);
    U("SetCursorPos",320,240);
    U("ShowCursor",0);
    native.drawable=(1ull<<32)|(480ull<<16)|640;
    ds3_presented_drawable=1;
    motion(22,-11);
    U("GetCursorPos",pt);
    if (rd32(pt)!=342 || rd32(pt+4)!=229) { fputs("captured motion left Win32 cursor stationary\n",stderr); return 1; }
    motion(3000,-3000);
    U("GetCursorPos",pt);
    assert(rd32(pt)==639 && rd32(pt+4)==0);
    assert(raw_x==3022 && raw_y==-3011);
    SDL_Event e={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT}};
    event(&e);
    SDL_Delay(430);
    assert(U("PeekMessageW",msg,hwnd,0x201,0x201,1));
    assert(rd32(msg+12)==639);
    fw=320; fh=240;
    motion(-10,11);
    U("GetCursorPos",pt);
    assert(rd32(pt)==618 && rd32(pt+4)==22);
    assert(raw_x==3012 && raw_y==-3000);
    if (!getenv("SKIP_WARP")) {
        U("SetCursorPos",100,100);
        motion(10,-5);
        U("GetCursorPos",pt);
        if (rd32(pt)!=120 || rd32(pt+4)!=90) { fputs("captured motion ignored same-size guest warp\n",stderr); return 2; }
    }
    SDL_Event alt={.key={.type=SDL_KEYDOWN,.keysym={.scancode=SDL_SCANCODE_F4,.mod=KMOD_LALT}}};
    SDL_Event close={.window={.type=SDL_WINDOWEVENT,.event=SDL_WINDOWEVENT_CLOSE}};
    event(&alt); event(&close);
    for (int i=0;i<600;i++) { motion(1,0); hle_ui_wheel(120); }
    if (!U("PeekMessageW",msg,hwnd,0x10,0x10,1)) { fputs("SDL close lost under input\n",stderr); return 3; }
    assert(!U("PeekMessageW",msg,hwnd,0x10,0x10,1));
    event(&alt);
    assert(U("PeekMessageW",msg,hwnd,0x10,0x10,1));
    U("DestroyWindow",hwnd);
    event(&close); /* Must not terminate the host before guest cleanup. */
    puts("PASS captured cursor, edge clamping, click position, frame resize, raw deltas");
}
