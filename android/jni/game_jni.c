/* libme1game.so: the recompiled MassEffect.exe (+ msvcr80, msvcp80, PhysX, D3DX) as a shared library.
 *  - GameActivity (process ":game") calls nativeStart / nativeSurface / nativeTouch / nativeKey / nativePad;
 *  - the "test" launcher executable libme1run.so (run_main.c) calls me1_exe_main directly (no window: frames go to
 *    an offscreen EGL surface and ME1_FRAME_DUMP).
 * Log: stdout/stderr -> the given file (Documents/me1exe_run.log). Native crashes are reported there too. */
#include <jni.h>
#include "hle.h"
#include "ds3_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <time.h>
#include <ucontext.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include "hle.h"

void me1gl_publish_window(void* anw);
int me1gl_get_view(int* x, int* y, int* w, int* h, int* fw, int* fh);
void hle_hb_now(const char* why);

static JavaVM* g_vm;
static jclass g_act;           /* GameActivity class (global ref) */
static int g_started;
static int g_active;
/* Source bits match GameActivity. Native guest key/button edges follow the union. */
enum { KBM_DIRECT, KBM_PAD, KBM_OVERLAY, KBM_POINTER, KBM_SOURCES };
static unsigned char g_keys[512], g_mouse_buttons[3];
static float g_mouse_rx, g_mouse_ry;
static int g_argc; static char** g_argv;
static void first_present(void *opaque) {
    (void)opaque; static int sent;
    if (__atomic_exchange_n(&sent,1,__ATOMIC_ACQ_REL) || !g_vm || !g_act) return;
    JNIEnv *e=NULL; int detached=(*g_vm)->GetEnv(g_vm,(void**)&e,JNI_VERSION_1_6)!=JNI_OK;
    if (detached && (*g_vm)->AttachCurrentThread(g_vm,&e,NULL)) return;
    jmethodID m=(*e)->GetStaticMethodID(e,g_act,"onFirstFramePresented","()V");
    if (m) (*e)->CallStaticVoidMethod(e,g_act,m);
    if (detached) (*g_vm)->DetachCurrentThread(g_vm);
}

#define FN(name) Java_com_ds3port_launcher_GameActivity_##name
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "me1game", __VA_ARGS__)

/* ---- native crash report (async-signal-safe writes), then the default handler (debuggerd tombstone) ---- */
static struct sigaction g_old[32];
static void wr(const char* s) { if (write(2, s, strlen(s)) < 0) {} }
static void wrhex(uint64_t v) { char b[19] = "0x"; for (int i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15]; b[18] = 0; wr(b); }
static void on_crash(int sig, siginfo_t* si, void* uc) {
    static volatile int once;
    if (!once) {
        once = 1;
        uint64_t pc = 0, lr = 0, sp = 0;
#if defined(__aarch64__)
        ucontext_t* u = uc; pc = u->uc_mcontext.pc; lr = u->uc_mcontext.regs[30]; sp = u->uc_mcontext.sp;
#endif
        Dl_info di; memset(&di, 0, sizeof di);
        wr("\n[exe] NATIVE CRASH: signal "); char n[8]; snprintf(n, sizeof n, "%d", sig); wr(n);
        wr(" addr "); wrhex((uint64_t)(uintptr_t)si->si_addr); wr(" pc "); wrhex(pc);
        if (pc && dladdr((void*)(uintptr_t)pc, &di) && di.dli_fname) {
            wr(" ("); wr(di.dli_fname); wr(" +"); wrhex(pc - (uint64_t)(uintptr_t)di.dli_fbase);
            if (di.dli_sname) { wr(" "); wr(di.dli_sname); }
            wr(")");
        }
        wr(" lr "); wrhex(lr);
        if (lr && dladdr((void*)(uintptr_t)lr, &di) && di.dli_fname) { wr(" (+"); wrhex(lr - (uint64_t)(uintptr_t)di.dli_fbase); if (di.dli_sname) { wr(" "); wr(di.dli_sname); } wr(")"); }
        wr(" sp "); wrhex(sp); wr("\n");
        hle_hb_now("(native crash)");
        fflush(stdout); fflush(stderr);
    }
    sigaction(sig, &g_old[sig], NULL);
    raise(sig);
}
static void crash_handlers(void) {
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    stack_t ss; ss.ss_sp = malloc(1 << 16); ss.ss_size = 1 << 16; ss.ss_flags = 0; sigaltstack(&ss, NULL);
    for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) {
        struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(sigs[i], &sa, &g_old[sigs[i]]);
    }
}

/* ---- game thread ---- */
/* v0.10.10: the Vulkan driver dies serially - schedule a process restart (GameActivity.onGameRestart, AlarmManager)
 * and let the caller exit with code 77 */
static void me1_request_restart(void) {
    if (!g_vm || !g_act) return;
    JNIEnv* e = NULL;
    jboolean detached = (*g_vm)->GetEnv(g_vm, (void**)&e, JNI_VERSION_1_6) != JNI_OK;
    if (detached && (*g_vm)->AttachCurrentThread(g_vm, &e, NULL) != 0) return;
    jmethodID m = (*e)->GetStaticMethodID(e, g_act, "onGameRestart", "()V");
    if (m) (*e)->CallStaticVoidMethod(e, g_act, m);
    if (detached) (*g_vm)->DetachCurrentThread(g_vm);
}
/* v0.10.2: one-line user notification (see hle.h); GameActivity.onDeviceLost posts it on the UI thread as a Toast */
static void me1_toast(const char* msg) {
    if (!g_vm || !g_act || !msg) return;
    JNIEnv* e = NULL;
    int attached = (*g_vm)->GetEnv(g_vm, (void**)&e, JNI_VERSION_1_6) != JNI_OK;
    if (attached && (*g_vm)->AttachCurrentThread(g_vm, &e, NULL) != 0) return;
    jmethodID m = (*e)->GetStaticMethodID(e, g_act, "onDeviceLost", "(Ljava/lang/String;)V");
    if (m) {
        jstring s = (*e)->NewStringUTF(e, msg);
        if (s) { (*e)->CallStaticVoidMethod(e, g_act, m, s); (*e)->DeleteLocalRef(e, s); }
    }
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
/* v1.2.2: ExitProcess (hle_win32.c) - the same goodbye as a return from main, without exit()'s destructors */
static void me1_on_exit(int code) {
    fprintf(stderr, "[exe] game exited (%d, ExitProcess)\n", code); fflush(stdout); fflush(stderr);
    JNIEnv* e = NULL;
    if (g_vm && (*g_vm)->AttachCurrentThread(g_vm, &e, NULL) == 0 && e && g_act) {
        jmethodID m = (*e)->GetStaticMethodID(e, g_act, "onGameExit", "(I)V");
        if (m) (*e)->CallStaticVoidMethod(e, g_act, m, (jint)code);
        (*g_vm)->DetachCurrentThread(g_vm);
    }
    usleep(500000);
}
static void* game_main(void* p) {
    (void)p;
    Ds3Platform platform={0}; platform.present=first_present; platform.exit=me1_on_exit;
    int r = g_argc >= 3 ? ds3_main(&platform,g_argv[1],g_argv[2]) : 2;
    fprintf(stderr, "[exe] game exited (%d)\n", r); fflush(stdout); fflush(stderr);
    JNIEnv* e = NULL;
    if (g_vm && (*g_vm)->AttachCurrentThread(g_vm, &e, NULL) == 0 && e && g_act) {
        jmethodID m = (*e)->GetStaticMethodID(e, g_act, "onGameExit", "(I)V");
        if (m) (*e)->CallStaticVoidMethod(e, g_act, m, (jint)r);
        (*g_vm)->DetachCurrentThread(g_vm);
    }
    usleep(500000);
    _exit(r);
    return NULL;
}
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* res) { (void)res; g_vm = vm; return JNI_VERSION_1_6; }

/* args: game dir, docs dir, image dir, ["--", game args...]; env: "NAME=value" */
JNIEXPORT jint JNICALL FN(nativeStart)(JNIEnv* e, jclass cl, jobjectArray jargs, jobjectArray jenv, jstring jlog) {
    if (g_started) return 1;
    g_started = 1;
    hle_al_set_paused(!g_active);
    g_act = (*e)->NewGlobalRef(e, cl);
    const char* log = jlog ? (*e)->GetStringUTFChars(e, jlog, NULL) : NULL;
    if (log && *log) {
        int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        else LOGI("cannot open log %s", log);
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (log) (*e)->ReleaseStringUTFChars(e, jlog, log);
    int ne = jenv ? (*e)->GetArrayLength(e, jenv) : 0;
    for (int i = 0; i < ne; i++) {
        jstring s = (*e)->GetObjectArrayElement(e, jenv, i); const char* v = (*e)->GetStringUTFChars(e, s, NULL);
        char* kv = strdup(v); char* eq = strchr(kv, '=');
        if (eq) {
            *eq = 0;
            // Older launchers set 1 (force connected). Android Pad now attaches only after input.
            const char* value = !strcmp(kv, "ME1_XINPUT") && !strcmp(eq + 1, "1") ? "2" : eq + 1;
            setenv(kv, value, 1);
        }
        free(kv); (*e)->ReleaseStringUTFChars(e, s, v); (*e)->DeleteLocalRef(e, s);
    }
    int na = jargs ? (*e)->GetArrayLength(e, jargs) : 0;
    g_argv = calloc((size_t)na + 2, sizeof(char*)); g_argv[0] = strdup("DeadSpace3");
    for (int i = 0; i < na; i++) {
        jstring s = (*e)->GetObjectArrayElement(e, jargs, i); const char* v = (*e)->GetStringUTFChars(e, s, NULL);
        g_argv[i + 1] = strdup(v); (*e)->ReleaseStringUTFChars(e, s, v); (*e)->DeleteLocalRef(e, s);
    }
    g_argc = na + 1;
    hle_toast_hook = me1_toast;   /* v0.10.2: DEVICE_LOST and other one-time events -> GameActivity Toast */
    hle_restart_hook = me1_request_restart;   /* v0.10.10: serial driver deaths -> game process restart */
    hle_exit_hook = me1_on_exit;   /* v1.2.2: ExitProcess without exit() (destroyed driver mutexes -> abort) */
    time_t t = time(NULL); fprintf(stderr, "[exe] libme1game.so in the activity process (pid %d), %s", (int)getpid(), ctime(&t));
    crash_handlers();
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 16u << 20);
    pthread_t th;
    if (pthread_create(&th, &a, game_main, NULL)) { fprintf(stderr, "[exe] cannot start the game thread\n"); return -1; }
    pthread_detach(th);
    return 0;
}
JNIEXPORT void JNICALL FN(nativeSurface)(JNIEnv* e, jclass cl, jobject surface) {
    ANativeWindow* w = surface ? ANativeWindow_fromSurface(e, surface) : NULL;
    me1gl_publish_window(w);     /* renderer owns the pending ref; no UI teardown wait */
    if (w) ANativeWindow_release(w);
}
/* window pixel -> back buffer pixel (the picture is letterboxed); returns 0 outside it */
static int to_frame(float x, float y, int* fx, int* fy, int* fw, int* fh) {
    int vx, vy, vw, vh;
    if (!me1gl_get_view(&vx, &vy, &vw, &vh, fw, fh) || vw <= 0 || vh <= 0) return 0;
    *fx = (int)((x - vx) * *fw / vw); *fy = (int)((y - vy) * *fh / vh);
    if (*fx < 0) *fx = 0; if (*fy < 0) *fy = 0; if (*fx >= *fw) *fx = *fw - 1; if (*fy >= *fh) *fy = *fh - 1;
    return 1;
}
static int set_owner(unsigned char* owners, int source, int down) {
    unsigned before = *owners, mask = 1u << source;
    *owners = down ? before | mask : before & ~mask;
    return !!before != !!*owners;
}
static int mouse_edge(int source, int button, int down) {
    return set_owner(&g_mouse_buttons[button], source, down) ? 2 * button + (down ? 1 : 2) : 0;
}
/* action: 0 down, 1 up, 2 move, 3 right down, 4 right up */
JNIEXPORT void JNICALL FN(nativePointer)(JNIEnv* e, jclass cl, jint source, jint action, jfloat x, jfloat y) {
    if (!g_active || (unsigned)source >= KBM_SOURCES || (unsigned)action > 4) return;
    int fx, fy, fw, fh;
    int framed = to_frame(x, y, &fx, &fy, &fw, &fh), edge = 0;
    if (action != 2) {
        int down = action == 0 || action == 3;
        // No new press without a position, but cancellation must release after surface loss.
        if (framed || !down) edge = mouse_edge(source, action >= 3, down);
    }
    if (framed) hle_ui_pointer(fx, fy, fw, fh, edge);
    else if (edge) hle_ui_mouse_button(edge);
}
/* on-screen controls: look area (relative motion in window pixels, scaled to mickeys) and fire/aim buttons */
JNIEXPORT void JNICALL FN(nativeMouseRel)(JNIEnv* e, jclass cl, jfloat dx, jfloat dy) {
    if (!g_active) return;
    g_mouse_rx += dx; g_mouse_ry += dy;
    int ix = (int)g_mouse_rx, iy = (int)g_mouse_ry; g_mouse_rx -= (float)ix; g_mouse_ry -= (float)iy;
    hle_ui_mouse_rel(ix, iy);
}
/* v1.1.5: the game's cursor for CursorView: -1 no window yet, else (state + 1) << 42 | x << 21 | y, state 1 shown / 0 hidden
 * (hle_ui_cursor: the Windows cursor the game shows in menus and hides while it captures the mouse), x/y in window pixels */
JNIEXPORT jlong JNICALL FN(nativeCursor)(JNIEnv* e, jclass cl) {
    (void)e; (void)cl;
    int cx, cy, cw, ch, vx, vy, vw, vh, fw, fh;
    int s = hle_ui_cursor(&cx, &cy, &cw, &ch);
    if (s < 0 || cw <= 0 || ch <= 0 || !me1gl_get_view(&vx, &vy, &vw, &vh, &fw, &fh) || vw <= 0 || vh <= 0) return -1;
    int64_t x = vx + ((int64_t)cx * vw + vw / 2) / cw, y = vy + ((int64_t)cy * vh + vh / 2) / ch;
    if (x < 0) x = 0; if (y < 0) y = 0;
    return ((jlong)(s + 1) << 42) | ((jlong)(x & 0x1FFFFF) << 21) | (jlong)(y & 0x1FFFFF);
}
JNIEXPORT void JNICALL FN(nativeMouseButtonFrom)(JNIEnv* e, jclass cl, jint source, jint button, jint down) {
    if (!g_active || (unsigned)source >= KBM_SOURCES || (unsigned)button > 4) return;
    if (button == 3 || button == 4) { if (down) hle_ui_wheel(button == 3 ? 120 : -120); return; }   /* v1.2: wheel up / down (one notch) */
    int edge = mouse_edge(source, button, down);
    if (edge) hle_ui_mouse_button(edge);
}
/* Android KeyEvent code -> Windows virtual key + DirectInput scan code */
static void map_key(int k, int* vk, int* dik) {
    static const unsigned char dik_az[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
    static const unsigned char dik_09[10] = { 0x0B,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A };
    *vk = 0; *dik = 0;
    if (k >= 29 && k <= 54) { *vk = 'A' + (k - 29); *dik = dik_az[k - 29]; return; }
    if (k >= 7 && k <= 16) { *vk = '0' + (k - 7); *dik = dik_09[k - 7]; return; }
    if (k >= 131 && k <= 142) { *vk = 0x70 + (k - 131); *dik = k - 131 < 10 ? 0x3B + (k - 131) : 0x57 + (k - 141); return; }   /* F1..F12 */
    switch (k) {
    case 4: case 111: *vk = 0x1B; *dik = 0x01; break;       /* BACK, ESCAPE */
    case 66: case 160: *vk = 0x0D; *dik = 0x1C; break;      /* ENTER, NUMPAD_ENTER */
    case 62: *vk = 0x20; *dik = 0x39; break;                /* SPACE */
    case 61: *vk = 0x09; *dik = 0x0F; break;                /* TAB */
    case 67: *vk = 0x08; *dik = 0x0E; break;                /* DEL (backspace) */
    case 112: *vk = 0x2E; *dik = 0xD3; break;               /* FORWARD_DEL */
    case 19: *vk = 0x26; *dik = 0xC8; break;                /* DPAD_UP */
    case 20: *vk = 0x28; *dik = 0xD0; break;
    case 21: *vk = 0x25; *dik = 0xCB; break;
    case 22: *vk = 0x27; *dik = 0xCD; break;
    case 23: *vk = 0x0D; *dik = 0x1C; break;                /* DPAD_CENTER */
    case 59: *vk = 0xA0; *dik = 0x2A; break;                /* SHIFT_LEFT */
    case 60: *vk = 0xA1; *dik = 0x36; break;
    case 113: *vk = 0xA2; *dik = 0x1D; break;               /* CTRL_LEFT */
    case 114: *vk = 0xA3; *dik = 0x9D; break;
    case 57: *vk = 0xA4; *dik = 0x38; break;                /* ALT_LEFT */
    case 58: *vk = 0xA5; *dik = 0xB8; break;
    case 68: *vk = 0xC0; *dik = 0x29; break;                /* GRAVE (console) */
    case 69: *vk = 0xBD; *dik = 0x0C; break; case 70: *vk = 0xBB; *dik = 0x0D; break;
    case 71: *vk = 0xDB; *dik = 0x1A; break; case 72: *vk = 0xDD; *dik = 0x1B; break;
    case 73: *vk = 0xDC; *dik = 0x2B; break; case 74: *vk = 0xBA; *dik = 0x27; break;
    case 75: *vk = 0xDE; *dik = 0x28; break; case 55: *vk = 0xBC; *dik = 0x33; break;
    case 56: *vk = 0xBE; *dik = 0x34; break; case 76: *vk = 0xBF; *dik = 0x35; break;
    case 92: *vk = 0x21; *dik = 0xC9; break; case 93: *vk = 0x22; *dik = 0xD1; break;   /* PAGE_UP/DOWN */
    case 122: *vk = 0x24; *dik = 0xC7; break; case 123: *vk = 0x23; *dik = 0xCF; break; /* MOVE_HOME/END */
    /* v1.2 (custom on-screen layouts): Insert, Caps/Num/Scroll Lock, Print Screen, Pause, the numeric keypad */
    case 124: *vk = 0x2D; *dik = 0xD2; break; case 115: *vk = 0x14; *dik = 0x3A; break;
    case 143: *vk = 0x90; *dik = 0x45; break; case 116: *vk = 0x91; *dik = 0x46; break;
    case 120: *vk = 0x2C; *dik = 0xB7; break; case 121: *vk = 0x13; *dik = 0xC5; break;
    case 154: *vk = 0x6F; *dik = 0xB5; break; case 155: *vk = 0x6A; *dik = 0x37; break;   /* NUMPAD / * */
    case 156: *vk = 0x6D; *dik = 0x4A; break; case 157: *vk = 0x6B; *dik = 0x4E; break;   /* NUMPAD - + */
    case 158: *vk = 0x6E; *dik = 0x53; break;                                             /* NUMPAD . */
    default:
        if (k >= 144 && k <= 153) {   /* NUMPAD_0..9 */
            static const unsigned char dik_np[10] = { 0x52,0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49 };
            *vk = 0x60 + (k - 144); *dik = dik_np[k - 144];
        }
        break;
    }
}
static void send_key(int code, int down, int uni) {
    int vk, dik; map_key(code, &vk, &dik);
    if (!vk) return;
    int shift = vk >= 0xA0 && vk <= 0xA5;
    if (shift) hle_ui_key(0x10 + (vk - 0xA0) / 2, 0, down, 0);   /* generic VK_SHIFT / VK_CONTROL / VK_MENU message */
    hle_ui_key(vk, dik, down, uni > 0 && uni < 0x10000 ? uni : 0);
}
JNIEXPORT void JNICALL FN(nativeActive)(JNIEnv* e, jclass cl, jboolean active) {
    g_active = active;
    if (!active) {
        for (unsigned i = 0; i < sizeof g_keys; ++i) if (g_keys[i]) { send_key(i, 0, 0); g_keys[i] = 0; }
        for (unsigned i = 0; i < sizeof g_mouse_buttons; ++i) if (g_mouse_buttons[i]) {
            hle_ui_mouse_button(2 * i + 2); g_mouse_buttons[i] = 0;
        }
        g_mouse_rx = g_mouse_ry = 0;
        /* Java releases Pad ownership before deactivation; a neutral set here would attach a phantom pad. */
    }
    hle_al_set_paused(!active);
}
JNIEXPORT void JNICALL FN(nativeKeyFrom)(JNIEnv* e, jclass cl, jint source, jint code, jint down, jint uni) {
    if (!g_active || (unsigned)source >= KBM_SOURCES || (unsigned)code >= sizeof g_keys) return;
    int direct_repeat = source == KBM_DIRECT && down && ((g_keys[code] & (1u << KBM_DIRECT)) || uni);
    if (set_owner(&g_keys[code], source, down) || direct_repeat) send_key(code, down, uni);
}
/* v1.1.3: a character typed on the phone keyboard that has no PC key (e.g. Cyrillic) -> WM_CHAR */
JNIEXPORT void JNICALL FN(nativeChar)(JNIEnv* e, jclass cl, jint ch) { (void)e; (void)cl; if (g_active) hle_ui_char(ch); }
/* gamepad (physical or on-screen) -> XInput pad 0: XINPUT_GAMEPAD button mask, triggers 0..255, sticks -32768..32767 */
JNIEXPORT void JNICALL FN(nativePad)(JNIEnv* e, jclass cl, jint buttons, jint lt, jint rt, jint lx, jint ly, jint rx, jint ry) {
    if (g_active) hle_xinput_set((unsigned)buttons, lt, rt, lx, ly, rx, ry);
}
JNIEXPORT void JNICALL FN(nativePadConnect)(JNIEnv* e, jclass cl, jint on) { hle_xinput_connect(on); }
JNIEXPORT jint JNICALL FN(nativePadVibration)(JNIEnv* e, jclass cl) { return hle_xinput_vibration(); }
void me1_status(int64_t o[10]);
JNIEXPORT jlongArray JNICALL FN(nativeStatus)(JNIEnv* e, jclass cl) {
    (void)cl; int64_t o[11]; memset(o, 0, sizeof o); me1_status(o);
    o[0]=hle_d3d9_presented();
    o[10]=me1gl_present_failure(); /* incident ID, 0 when healthy */
    jlongArray a = (*e)->NewLongArray(e, 11);
    if (a) (*e)->SetLongArrayRegion(e, a, 0, 11, (const jlong*)o);
    return a;
}
/* v0.10: on-screen FPS counter - {fps*10, frame ms*10, renderer 0 none/1 GLES/2 zink/3 Vulkan, fps limit} */
void me1_perf(int64_t o[4]);
JNIEXPORT jlongArray JNICALL FN(nativePerf)(JNIEnv* e, jclass cl) {
    (void)cl; int64_t o[4]; memset(o, 0, sizeof o); me1_perf(o);
    jlongArray a = (*e)->NewLongArray(e, 4);
    if (a) (*e)->SetLongArrayRegion(e, a, 0, 4, (const jlong*)o);
    return a;
}
/* v1.2.1 GEARMASS: one-line load-governor indicator for the FPS overlay ("" when GEARMASS is off / not started) */
int gm_hud(char* out, size_t cap);
JNIEXPORT jstring JNICALL FN(nativeGearmass)(JNIEnv* e, jclass cl) {
    (void)cl; char b[256]; b[0] = 0; gm_hud(b, sizeof b);
    return (*e)->NewStringUTF(e, b);
}
JNIEXPORT void JNICALL FN(nativeSignal)(JNIEnv* e, jclass cl, jint sig) { (void)e; (void)cl; (void)sig; hle_hb_now("Android diagnostic signal"); }
