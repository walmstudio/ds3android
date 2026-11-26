/* DS3 frontend using the Mass Effect port's SDL2 UI. No guest code here. */
#define _GNU_SOURCE
#include "ui.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char game[PATH_MAX], profile[PATH_MAX], script[PATH_MAX];
static const char *const language[] = {"Русский", "English"};
static const char *const windows[] = {"Wayland", "X11"};
static const char *const window_env[] = {"wayland", "x11"};
static const char *const renderers[] = {"Vulkan", "GLES"};
static const char *const render_env[] = {"vulkan", "gles"};
static const char *const sounds[] = {"PipeWire", "ALSA", "PulseAudio"};
static const char *const sound_env[] = {"pipewire", "alsa", "pulse"};
static const char *const tabs[] = {"Главная", "Графика", "Звук", "Журнал"};
static const char *const render_sizes[] = {"Из профиля", "1280x720", "1600x900", "1920x1080"};
static const char *const gm_labels[] = {"Консервативный", "Авто", "Экономия"};
static const char *const gm_modes[] = {"custom", "auto", "eco"};
static int lang, ws, ren, audio, tab, render_size, gm;
static double fps;
static char alsa_device[128], config[PATH_MAX];
extern char **environ;

static void prefs_load(void) {
    char *dir=SDL_GetPrefPath("ds3port","launcher");
    if (!dir) return;
    int n=snprintf(config,sizeof config,"%ssettings.conf",dir);
    SDL_free(dir);
    if (n<0 || (size_t)n>=sizeof config) { config[0]=0; return; }
    FILE *f=fopen(config,"r");
    if (!f) return;
    char *s=NULL;
    size_t cap=0;
    while (getline(&s,&cap,f)>=0) {
        s[strcspn(s,"\n\r")]=0;
        char *v=strchr(s,'='); if (!v) continue;
        *v++=0;
        if (!strcmp(s,"game") && strlen(v)<sizeof game) strcpy(game,v);
        if (!strcmp(s,"profile") && strlen(v)<sizeof profile) strcpy(profile,v);
        if (!strcmp(s,"alsa") && strlen(v)<sizeof alsa_device) strcpy(alsa_device,v);
        if (!strcmp(s,"lang")) lang=!strcmp(v,"en");
        if (!strcmp(s,"window")) ws=!strcmp(v,"x11");
        if (!strcmp(s,"renderer")) ren=!strcmp(v,"gles");
        if (!strcmp(s,"audio")) audio=!strcmp(v,"alsa")?1:!strcmp(v,"pulse")?2:0;
        if (!strcmp(s,"fps")) { int x=atoi(v); if (x>=0 && x<=120) fps=x; }
        if (!strcmp(s,"size")) for (int i=1;i<4;++i) if (!strcmp(v,render_sizes[i])) render_size=i;
        if (!strcmp(s,"gm")) for (int i=0;i<3;++i) if (!strcmp(v,gm_modes[i])) gm=i;
    }
    free(s); fclose(f);
}

static int prefs_save(void) {
    char tmp[PATH_MAX];
    int n=snprintf(tmp,sizeof tmp,"%s.XXXXXX",config);
    if (!*config || n<0 || (size_t)n>=sizeof tmp || strpbrk(game,"\r\n") || strpbrk(profile,"\r\n") || strpbrk(alsa_device,"\r\n")) return 0;
    int fd=mkstemp(tmp);
    if (fd<0) return 0;
    FILE *f=fdopen(fd,"w");
    int ok=0;
    if (f) {
        n=fprintf(f,"game=%s\nprofile=%s\nlang=%s\nwindow=%s\nrenderer=%s\naudio=%s\nalsa=%s\nfps=%g\nsize=%s\ngm=%s\n",
                  game,profile,lang?"en":"ru",window_env[ws],render_env[ren],sound_env[audio],alsa_device,fps,render_size?render_sizes[render_size]:"game",gm_modes[gm]);
        ok=fclose(f)==0 && n>=0;
    } else close(fd);
    if (ok && !rename(tmp,config)) return 1;
    unlink(tmp); return 0;
}

static int own_path(void) {
    ssize_t n=readlink("/proc/self/exe",script,sizeof script-1);
    if (n<0 || (size_t)n>=sizeof script-1) return 0;
    script[n]=0;
    char *slash=strrchr(script,'/');
    if (!slash || (size_t)(script+sizeof script-slash)<sizeof "/play-ds3.sh") return 0;
    strcpy(slash,"/play-ds3.sh");
    return 1;
}

static const char *validate(void) {
    char path[PATH_MAX], root[PATH_MAX], docs[PATH_MAX];
    struct stat st;
    if (access(script,X_OK)) return "Не найден соседний play-ds3.sh";
    if (!realpath(game,root)) return "Папка игры не найдена";
    int n=snprintf(path,sizeof path,"%s/deadspace3.exe",root);
    if (n<0 || (size_t)n>=sizeof path || stat(path,&st) || !S_ISREG(st.st_mode))
        return "В папке нет deadspace3.exe";
    if (!realpath(profile,docs) || stat(docs,&st) || !S_ISDIR(st.st_mode))
        return "Укажи существующую папку профиля";
    size_t r=strlen(root), d=strlen(docs);
    if (d==1 || r==1 || (!strncmp(root,docs,d) && (root[d]=='/' || !root[d])) ||
        (!strncmp(docs,root,r) && (docs[r]=='/' || !docs[r])))
        return "Папки профиля и игры должны быть раздельны";
    return NULL;
}

static pid_t start(void) {
    char root[PATH_MAX], docs[PATH_MAX];
    if (!realpath(game,root) || !realpath(profile,docs) || setenv("DS3_GAME_DIR",root,1)) return -1;
    char *args[]={script,lang?"en":"ru",docs,NULL};
    char limit[16]; snprintf(limit,sizeof limit,"%.0f",fps);
    if (setenv("DS3_WINDOW_SYSTEM",window_env[ws],1) || setenv("DS3_RENDERER",render_env[ren],1) ||
        setenv("DS3_AUDIO_BACKEND",sound_env[audio],1) || setenv("ME1_FPS_LIMIT",limit,1) || setenv("ME1_GEARMASS",gm_modes[gm],1) ||
        setenv("DS3_RENDER_SIZE",render_size?render_sizes[render_size]:"game",1) ||
        (*alsa_device ? setenv("DS3_ALSA_DEVICE",alsa_device,1) : unsetenv("DS3_ALSA_DEVICE"))) return -1;
    pid_t p;
    return posix_spawn(&p,script,NULL,NULL,args,environ)?-1:p;
}

int main(int argc,char **argv) {
    if (!own_path()) return 2;
    const char *home=getenv("HOME");
    if (!home) home=".";
    snprintf(game,sizeof game,"%s/ds3an/game",home);
    snprintf(profile,sizeof profile,"%s/.local/share/ds3port-test73",home);
    if (argc!=5) prefs_load();
    if (argc!=1 && argc!=4 && argc!=5) {
        fprintf(stderr,"Usage: %s [GAME_DIR PROFILE_DIR ru|en [--check]]\n",argv[0]); return 2;
    }
    if (argc>=4) {
        if (strlen(argv[1])>=sizeof game || strlen(argv[2])>=sizeof profile ||
            (strcmp(argv[3],"ru") && strcmp(argv[3],"en"))) return 2;
        strcpy(game,argv[1]); strcpy(profile,argv[2]); lang=!strcmp(argv[3],"en");
    }
    if (argc==5) {
        if (strcmp(argv[4],"--check")) return 2;
        const char *err=validate();
        if (err) { fprintf(stderr,"%s\n",err); return 2; }
        printf("game=%s\nprofile=%s\nlanguage=%s\n",game,profile,lang?"en":"ru");
        return 0;
    }
    const char* session=getenv("WAYLAND_DISPLAY");
    const char* ui_driver=session && *session?"wayland":"x11";
    if (setenv("SDL_VIDEODRIVER",ui_driver,1) || SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr,"%s: %s\n",ui_driver,SDL_GetError()); return 2;
    }
    SDL_Window *w=SDL_CreateWindow("Dead Space 3",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                                   750,455,SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *r=w?SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE):NULL;
    if (!r || !ui_init(r)) { fprintf(stderr,"SDL2: %s\n",SDL_GetError()); SDL_Quit(); return 2; }
    fprintf(stderr,"[ds3-launcher] %s window ready\n",SDL_GetCurrentVideoDriver());
    pid_t child=0;
    const char *status="Готово";
    int running=1;
    while (running) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e,33)) do {
            if (e.type==SDL_QUIT) running=0;
            ui_event(&e);
        } while (SDL_PollEvent(&e));
        if (child>0) {
            int result;
            if (waitpid(child,&result,WNOHANG)==child) {
                status=WIFEXITED(result) && WEXITSTATUS(result)==0?"Игра завершена":"Ошибка запуска — см. лог рядом со сборкой";
                child=0; SDL_ShowWindow(w); SDL_RaiseWindow(w);
            }
        }
        if (child>0 || !running) continue;
        ui_begin(w);
        ui.top=ui_popup_open()?UI_LAYER_POPUP:UI_LAYER_MAIN;
        ui_fill((SDL_Rect){0,0,ui.w,ui.h},UIC(C_BG));
        int x=D(24), width=ui.w-2*x;
        ui_text(UF_BOLD,D(29),x,D(18),UIC(C_TEXT),"Dead Space 3",-1);
        for (int i=0;i<4;++i) if (ui_button(ui_id("tab",i),(SDL_Rect){x+i*D(120),D(65),D(110),D(40)},tabs[i],tab==i?UB_TABSEL:UB_TAB)) tab=i;
        if (tab==0) {
            ui_text(UF_REG,D(16),x,D(123),UIC(C_DIM),"Папка игры",-1);
            ui_input(ui_id("game",0),(SDL_Rect){x,D(146),width,D(38)},game,sizeof game,"Путь к игре");
            ui_text(UF_REG,D(16),x,D(196),UIC(C_DIM),"Папка профиля",-1);
            ui_input(ui_id("profile",0),(SDL_Rect){x,D(219),width,D(38)},profile,sizeof profile,"Путь к профилю");
            ui_text(UF_REG,D(16),x,D(279),UIC(C_DIM),"Язык игры",-1);
            ui_combo(ui_id("lang",0),(SDL_Rect){x+D(170),D(268),D(230),D(37)},language,2,&lang);
        } else if (tab==1) {
            ui_text(UF_REG,D(16),x,D(145),UIC(C_DIM),"Оконная система",-1);
            ui_combo(ui_id("ws",0),(SDL_Rect){x+D(170),D(134),D(230),D(37)},windows,2,&ws);
            ui_text(UF_REG,D(16),x,D(198),UIC(C_DIM),"Рендерер",-1);
            ui_combo(ui_id("renderer",0),(SDL_Rect){x+D(170),D(187),D(230),D(37)},renderers,2,&ren);
            char label[64]; snprintf(label,sizeof label,"Лимит FPS: %.0f",fps);
            ui_text(UF_REG,D(16),x,D(251),UIC(C_DIM),label,-1);
            ui_slider(ui_id("fps",0),(SDL_Rect){x+D(170),D(240),D(230),D(37)},&fps,0,120,5);
            ui_text(UF_REG,D(16),x,D(306),UIC(C_DIM),"Разрешение",-1);
            ui_combo(ui_id("size",0),(SDL_Rect){x+D(170),D(294),D(230),D(37)},render_sizes,4,&render_size);
            ui_combo(ui_id("gm",0),(SDL_Rect){x+D(420),D(240),D(225),D(37)},gm_labels,3,&gm);
            ui_text(UF_REG,D(14),x+D(420),D(210),UIC(C_DIM),"GEARMASS · FPS 0 — без лимита",-1);
        } else if (tab==2) {
            ui_text(UF_REG,D(16),x,D(145),UIC(C_DIM),"Звуковой выход",-1);
            ui_combo(ui_id("audio",0),(SDL_Rect){x+D(170),D(134),D(230),D(37)},sounds,3,&audio);
            if (audio==1) {
                ui_text(UF_REG,D(16),x,D(198),UIC(C_DIM),"Устройство ALSA",-1);
                ui_input(ui_id("alsa",0),(SDL_Rect){x+D(170),D(187),D(390),D(37)},alsa_device,sizeof alsa_device,"Обязательно: hw:CARD=…,DEV=…");
            }
            ui_text(UF_REG,D(14),x,D(252),UIC(C_DIM),"При ошибке другой выход автоматически не включается.",-1);
        } else {
            SDL_Rect r={x,D(133),width,D(184)};
            char path[PATH_MAX], line[384], messages[6][384]={{0}};
            unsigned i=0;
            int n=snprintf(path,sizeof path,"%s/ds3-last.log",profile);
            FILE *f=n>=0 && (size_t)n<sizeof path?fopen(path,"r"):NULL;
            if (f) { while (fgets(line,sizeof line,f)) { line[strcspn(line,"\r\n")]=0; strcpy(messages[i++%6],line); } fclose(f); }
            ui_fill(r,UIC(C_PANEL));
            ui_text(UF_REG,D(15),x+D(9),r.y+D(8),UIC(C_TEXT),"Последний запуск: ds3-last.log",-1);
            for (unsigned j=i>6?i-6:0,y=0;j<i;++j,++y) ui_text_fit(UF_MONO,D(12),x+D(9),r.y+D(34)+y*D(20),width-D(18),UIC(C_DIM),messages[j%6]);
        }
        if (ui_button(ui_id("save",0),(SDL_Rect){x+D(210),D(350),D(180),D(43)},"Сохранить",UB_NORMAL)) status=prefs_save()?"Настройки сохранены":"Не удалось сохранить настройки";
        if (ui_button(ui_id("play",0),(SDL_Rect){x,D(350),D(180),D(43)},"Запустить",UB_PRIMARY)) {
            const char *err=validate();
            if (err) status=err;
            else if (audio==1 && strncmp(alsa_device,"hw:",3)) status="Укажи ALSA-устройство hw:CARD=…,DEV=…";
            else if (!prefs_save()) status="Не удалось сохранить настройки";
            else if ((child=start())<=0) status="Не удалось открыть процесс игры";
            else SDL_HideWindow(w);
        }
        ui_text_fit(UF_REG,D(15),x,D(413),width,UIC(C_DIM),status);
        ui_overlay(); ui_end(); SDL_RenderPresent(r);
    }
    ui_shutdown(); SDL_DestroyRenderer(r); SDL_DestroyWindow(w); SDL_Quit();
    return 0;
}
