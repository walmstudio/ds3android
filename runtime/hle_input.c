/* DirectInput 8 (dinput8.dll): IDirectInput8 + system mouse/keyboard devices. MassEffect.exe creates them right after
 * the window (FWindowsViewport input init) and does not check DirectInput8Create's result, so "no DirectInput" is not
 * an option. Devices are always attached and acquired; their state comes from hle_input_key/hle_input_mouse (the
 * Android front end will feed touch/keyboard events there): immediate state (GetDeviceState) plus a buffered event
 * ring (GetDeviceData, DIPROP_BUFFERSIZE). Game controllers: none enumerated (the engine uses XInput for pads).
 * ME1_TRACE_INPUT=1 logs the device calls.
 * XInput 1.3 (xinput1_3.dll, imported by ordinal: #2 XInputGetState, #3 XInputSetState): one virtual pad (user 0) fed
 * by hle_xinput_set (physical Android gamepad or the on-screen pad). The engine polls users 0..3 every frame
 * (UWindowsClient JoystickInfo) and reads XboxTypeS_* keys - vanilla DefaultInput.ini has no bindings for them, the
 * ME1Controller mod (BIOInput.ini) does. ME1_XINPUT: 0 = no pad, 1 = pad 0 always connected, unset/2 = connected
 * from the first hle_xinput_set on. Rumble (XInputSetState) is read back by hle_xinput_vibration. */
#include "hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define DI_OK 0u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOINTERFACE 0x80004002u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_UNSUPPORTED 0x80004001u
#define DIERR_NOTINITIALIZED 0x80070015u
#define DIERR_OUTOFMEMORY 0x8007000eu
#define DIERR_NOAGGREGATION 0x80040110u

enum { DEV_MOUSE = 1, DEV_KEYBOARD = 2 };
enum { DI_ALL = 0, DI_POINTER = 2, DI_KEYBOARD = 3 };
#define RING 256
typedef struct { uint32_t ofs, data, time, seq; } Ev;
typedef struct {
    int type, refs, acquired; uint32_t bufsize, coop, hevent;
    uint32_t unacq_ms;                                         /* v1.1.5: time of the last Unacquire */
    uint8_t keys[256]; int32_t mx, my, mz; uint8_t mbtn[8];   /* immediate state; mouse axes are relative deltas */
    Ev ring[RING]; unsigned head, tail;                        /* buffered data */
} Dev;
static Dev g_dev[3];
/* A/W are separate interfaces, but share the virtual devices and their state. */
static uint32_t g_obj_di, g_vt_dev, g_obj_dev[3], g_seq;
static unsigned g_di_refs;
static int g_trace = -1;
static pthread_mutex_t g_imx = PTHREAD_MUTEX_INITIALIZER;   /* the front end feeds events from its own thread */
#define TR(...) do { if (g_trace) fprintf(stderr, "[dinput] " __VA_ARGS__); } while (0)

static uint32_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static int flavour(uint32_t base, uint32_t self) { return !base ? -1 : self == base ? 0 : self == base + 4 ? 1 : -1; }
static Dev* dev_of(uint32_t self) { for (int i = 1; i < 3; i++) if (flavour(g_obj_dev[i], self) >= 0) return &g_dev[i]; return NULL; }
static int guest_span(uint32_t p, uint32_t n) { return p && p <= UINT32_MAX - (n - 1); }
static void push_ev(Dev* d, uint32_t ofs, uint32_t data) {
    if (!d->bufsize || !d->acquired) return;
    unsigned n = d->head - d->tail; if (n >= RING || n >= d->bufsize) d->tail++;   /* overflow: drop the oldest */
    Ev* e = &d->ring[d->head++ % RING]; e->ofs = ofs; e->data = data; e->time = now_ms(); e->seq = ++g_seq;
}
/* front end API: DIK_* scan code; mouse deltas in mickeys + button mask (bit i = button i) */
void hle_input_key(int dik, int down) {
    if (dik < 0 || dik > 255) return;
    pthread_mutex_lock(&g_imx); Dev* d = &g_dev[DEV_KEYBOARD];
    uint8_t v = down ? 0x80 : 0; if (d->keys[dik] != v) { d->keys[dik] = v; push_ev(d, (uint32_t)dik, v); }
    pthread_mutex_unlock(&g_imx);
}
void hle_input_mouse(int dx, int dy, int dz, unsigned buttons) {
    pthread_mutex_lock(&g_imx); Dev* d = &g_dev[DEV_MOUSE];
    d->mx += dx; d->my += dy; d->mz += dz;
    if (dx) push_ev(d, 0, (uint32_t)dx);
    if (dy) push_ev(d, 4, (uint32_t)dy);
    if (dz) push_ev(d, 8, (uint32_t)dz);
    for (int b = 0; b < 8; b++) { uint8_t v = (buttons >> b & 1) ? 0x80 : 0; if (d->mbtn[b] != v) { d->mbtn[b] = v; push_ev(d, 12 + b, v); } }
    pthread_mutex_unlock(&g_imx);
}

/* ---- XInput ---- */
static struct { int connected; uint32_t packet; uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry; uint16_t vl, vr; uint32_t polls, sets; } g_xi;
static int g_xi_mode = -1;
static int xi_mode(void) {
    if (g_xi_mode < 0) { const char* e = getenv("ME1_XINPUT"); g_xi_mode = e && *e ? atoi(e) : 2; if (g_xi_mode == 1) g_xi.connected = 1; }
    return g_xi_mode;
}
static int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
void hle_xinput_set(unsigned buttons, int lt, int rt, int lx, int ly, int rx, int ry) {
    pthread_mutex_lock(&g_imx);
    if (xi_mode() && !g_xi.connected) { g_xi.connected = 1; fprintf(stderr, "[xinput] gamepad 0 connected\n"); }
    uint8_t l = (uint8_t)(lt < 0 ? 0 : lt > 255 ? 255 : lt), r = (uint8_t)(rt < 0 ? 0 : rt > 255 ? 255 : rt);
    int16_t a = clamp16(lx), b = clamp16(ly), x = clamp16(rx), y = clamp16(ry);
    if (g_xi.buttons != (uint16_t)buttons || g_xi.lt != l || g_xi.rt != r || g_xi.lx != a || g_xi.ly != b || g_xi.rx != x || g_xi.ry != y) {
        g_xi.buttons = (uint16_t)buttons; g_xi.lt = l; g_xi.rt = r; g_xi.lx = a; g_xi.ly = b; g_xi.rx = x; g_xi.ry = y; g_xi.packet++;
    }
    pthread_mutex_unlock(&g_imx);
}
void hle_xinput_connect(int on) {
    pthread_mutex_lock(&g_imx);
    if (xi_mode()) { if (g_xi.connected != !!on) fprintf(stderr, "[xinput] gamepad 0 %s\n", on ? "connected" : "disconnected"); g_xi.connected = !!on; }
    if (!on) { g_xi.buttons = 0; g_xi.lt = g_xi.rt = 0; g_xi.lx = g_xi.ly = g_xi.rx = g_xi.ry = 0; g_xi.vl = g_xi.vr = 0; g_xi.packet++; }
    pthread_mutex_unlock(&g_imx);
}
int hle_xinput_vibration(void) {   /* (left motor << 16) | right motor, 0..65535 each */
    pthread_mutex_lock(&g_imx); int v = (int)((uint32_t)g_xi.vl << 16 | g_xi.vr); pthread_mutex_unlock(&g_imx); return v;
}
void hle_xinput_brief(char* out, size_t cap) {
    snprintf(out, cap, "pad %s polls %u rumble-sets %u", g_xi.connected ? "on" : "off", g_xi.polls, g_xi.sets);
}
static void xi_GetState(CPU* c) {   /* (dwUserIndex, XINPUT_STATE*) */
    uint32_t u = ARG(0), p = ARG(1);
    pthread_mutex_lock(&g_imx);
    int ok = xi_mode() && g_xi.connected && u == 0 && p;
    g_xi.polls++;
    if (ok) {
        wr32(p, g_xi.packet); wr16(p + 4, g_xi.buttons); M[p + 6] = g_xi.lt; M[p + 7] = g_xi.rt;
        wr16(p + 8, (uint16_t)g_xi.lx); wr16(p + 10, (uint16_t)g_xi.ly); wr16(p + 12, (uint16_t)g_xi.rx); wr16(p + 14, (uint16_t)g_xi.ry);
    }
    pthread_mutex_unlock(&g_imx);
    RET(ok ? 0 : 1167);   /* ERROR_DEVICE_NOT_CONNECTED */
}
static void xi_SetState(CPU* c) {   /* (dwUserIndex, XINPUT_VIBRATION*) */
    uint32_t u = ARG(0), p = ARG(1);
    pthread_mutex_lock(&g_imx);
    int ok = xi_mode() && g_xi.connected && u == 0;
    if (ok && p) { g_xi.vl = rd16(p); g_xi.vr = rd16(p + 2); g_xi.sets++; }
    pthread_mutex_unlock(&g_imx);
    RET(ok ? 0 : 1167);
}

static void xi_GetCapabilities(CPU* c) {
    uint32_t u = ARG(0), flags = ARG(1), p = ARG(2);
    if (u >= 4 || (flags && flags != 1) || !guest_span(p, 20)) { RET(87); return; }
    pthread_mutex_lock(&g_imx);
    int ok = xi_mode() && g_xi.connected && u == 0;
    pthread_mutex_unlock(&g_imx);
    if (ok) {
        memset(M + p, 0, 20);
        M[p] = M[p + 1] = 1;                         /* gamepad type/subtype */
        wr16(p + 2, 1);                              /* force feedback supported */
        wr16(p + 4, 0xF3FF); M[p + 6] = M[p + 7] = 255;
        for (unsigned i = 8; i < 16; i += 2) wr16(p + i, 0xFFC0);
        wr16(p + 16, 255); wr16(p + 18, 255);
    }
    RET(ok ? 0 : 1167);
}

/* Win32 GUIDs as four little-endian words. */
static const uint32_t di_iid[2][4] = {
    {0xbf798030, 0x4da2483a, 0x645d99aa, 0x009736ed},
    {0xbf798031, 0x4da2483a, 0x645d99aa, 0x009736ed},
};
static const uint32_t dv_iid[2][4] = {
    {0x54d41080, 0x4833dc15, 0x8f741ba4, 0x7981a373},
    {0x54d41081, 0x4833dc15, 0x8f741ba4, 0x7981a373},
};
static const uint32_t iid_unknown[4] = {0, 0, 0xc0, 0x46000000};
static const uint32_t sys_guid[2][4] = {
    {0x6f1d2b60, 0x11cfd5a0, 0x4544c7bf, 0x00005453},
    {0x6f1d2b61, 0x11cfd5a0, 0x4544c7bf, 0x00005453},
};
static int same_guid(uint32_t p, const uint32_t guid[4]) { return guest_span(p, 16) && !memcmp(M + p, guid, 16); }
static int device_type(uint32_t p) {
    return same_guid(p, sys_guid[0]) ? DEV_MOUSE : same_guid(p, sys_guid[1]) ? DEV_KEYBOARD : 0;
}
static uint32_t dev_kind(int t) { return t == DEV_MOUSE ? 0x212 : 0x413; }

static int build(void) {
    if (g_obj_di) return 1;
    static const char* const di[] = { "QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices", "GetDeviceStatus",
        "RunControlPanel", "Initialize", "FindDevice", "EnumDevicesBySemantics", "ConfigureDevices" };
    static const char* const dv[] = { "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects", "GetProperty",
        "SetProperty", "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData", "SetDataFormat", "SetEventNotification",
        "SetCooperativeLevel", "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize", "CreateEffect", "EnumEffects",
        "GetEffectInfo", "GetForceFeedbackState", "SendForceFeedbackCommand", "EnumCreatedEffectObjects", "Escape", "Poll",
        "SendDeviceData", "EnumEffectsInFile", "WriteEffectToFile", "BuildActionMap", "SetActionMap", "GetImageInfo" };
    char n[96];
    uint32_t p = ghalloc(4 * (2 + 11 + 32), 1);
    if (!p) return 0;
    uint32_t vt_di = p + 8;
    for (int i = 0; i < 11; i++) { snprintf(n, sizeof n, "IDirectInput8::%s", di[i]); wr32(vt_di + 4 * i, hle_resolve("dinput8.com", n)); }
    g_vt_dev = vt_di + 4 * 11;
    for (int i = 0; i < 32; i++) { snprintf(n, sizeof n, "IDirectInputDevice8::%s", dv[i]); wr32(g_vt_dev + 4 * i, hle_resolve("dinput8.com", n)); }
    wr32(p, vt_di); wr32(p + 4, vt_di);
    __atomic_store_n(&g_obj_di, p, __ATOMIC_RELEASE);
    return 1;
}
static void di_Create(CPU* c) {   /* (hinst, version, riid, ppvOut, punkOuter) */
    uint32_t iid = ARG(2), out = ARG(3);
    if (!guest_span(out, 4)) { RET(DIERR_INVALIDPARAM); return; }
    int w = same_guid(iid, di_iid[1]);
    int known = w || same_guid(iid, di_iid[0]) || same_guid(iid, iid_unknown);
    wr32(out, 0);
    if (ARG(4)) { RET(DIERR_NOAGGREGATION); return; }
    if (!known) { RET(DIERR_NOINTERFACE); return; }
    if (g_trace < 0) { const char* e = getenv("ME1_TRACE_INPUT"); g_trace = e && atoi(e); }
    if (!build()) { RET(DIERR_OUTOFMEMORY); return; }
    uint32_t self = g_obj_di + 4 * w;
    wr32(out, self); g_di_refs++;
    fprintf(stderr, "[dinput] DirectInput8Create(%c, version %x) -> %08x (system mouse + keyboard, no game controllers)\n", w ? 'W' : 'A', ARG(1), self);
    RET(DI_OK);
}
#define SELF ARG(0)
static void qi(CPU* c) {
    uint32_t iid = ARG(1), out = ARG(2);
    if (!guest_span(out, 4)) { RET(DIERR_INVALIDPARAM); return; }
    Dev* d = dev_of(SELF);
    const uint32_t (*known)[4] = d ? dv_iid : di_iid;
    uint32_t base = d ? g_obj_dev[d->type] : g_obj_di;
    int w = same_guid(iid, known[1]);
    int matches = w || same_guid(iid, known[0]) || same_guid(iid, iid_unknown);
    wr32(out, 0);
    if (flavour(base, SELF) < 0 || !matches) { RET(DIERR_NOINTERFACE); return; }
    wr32(out, base + 4 * w);
    if (d) d->refs++; else g_di_refs++;
    RET(DI_OK);
}
static void addref(CPU* c) { Dev* d = dev_of(SELF); RET(d ? (unsigned)++d->refs : ++g_di_refs); }
static void release(CPU* c) { Dev* d = dev_of(SELF); RET(d ? (unsigned)(d->refs ? --d->refs : 0) : g_di_refs ? --g_di_refs : 0); }
static void di_CreateDevice(CPU* c) {   /* (this, rguid, ppDevice, punkOuter) */
    int w = flavour(g_obj_di, SELF), t = device_type(ARG(1));
    uint32_t out = ARG(2);
    if (w < 0 || !guest_span(out, 4)) { RET(DIERR_INVALIDPARAM); return; }
    wr32(out, 0);
    if (ARG(3)) { RET(DIERR_NOAGGREGATION); return; }
    if (!t) { RET(DIERR_DEVICENOTREG); return; }
    if (!g_obj_dev[t]) {
        uint32_t p = ghalloc(8, 1);
        if (!p) { RET(DIERR_OUTOFMEMORY); return; }
        wr32(p, g_vt_dev); wr32(p + 4, g_vt_dev); g_obj_dev[t] = p;
    }
    pthread_mutex_lock(&g_imx); Dev* d = &g_dev[t]; d->type = t; d->refs++; pthread_mutex_unlock(&g_imx);
    wr32(out, g_obj_dev[t] + 4 * w);
    fprintf(stderr, "[dinput] CreateDevice(%s) -> %08x\n", t == DEV_MOUSE ? "GUID_SysMouse" : "GUID_SysKeyboard", rd32(out));
    RET(DI_OK);
}
static uint32_t instance_size(int w) { return 40 + 2 * 260 * (w + 1) + 20; }
static void instance_info(uint32_t p, uint32_t size, int t, int w) {
    unsigned chars = 260 * (w + 1);
    memset(M + p, 0, size);
    wr32(p, size);
    memcpy(M + p + 4, sys_guid[t - 1], 16);
    memcpy(M + p + 20, sys_guid[t - 1], 16);
    wr32(p + 36, dev_kind(t));
    const char* name = t == DEV_MOUSE ? "Mouse" : "Keyboard";
    for (unsigned i = 0; name[i]; i++) {
        wr8(p + 40 + i * (w + 1), name[i]);
        wr8(p + 40 + chars + i * (w + 1), name[i]);
    }
    if (size == instance_size(w)) {
        wr16(p + size - 4, 1);                         /* Generic Desktop usage page */
        wr16(p + size - 2, t == DEV_MOUSE ? 2 : 6);   /* mouse / keyboard */
    }
}
static void di_EnumDevices(CPU* c) {
    int w = flavour(__atomic_load_n(&g_obj_di, __ATOMIC_ACQUIRE), SELF);
    uint32_t cls = ARG(1), callback = ARG(2), context = ARG(3), flags = ARG(4);
    if (w < 0 || !callback || (flags & ~0x70101u) || (cls > 4 && (cls < 0x11 || cls > 0x1c))) {
        RET(DIERR_INVALIDPARAM); return;
    }
    int mouse = cls == DI_ALL || cls == DI_POINTER || cls == 0x12;
    int key = cls == DI_ALL || cls == DI_KEYBOARD || cls == 0x13;
    /* Virtual mouse and keyboard are attached; no aliases, hidden or FF devices. */
    if ((!mouse && !key) || (flags & 0x100)) { RET(DI_OK); return; }
    uint32_t size = instance_size(w), p = ghalloc(size, 0);
    if (!p) { RET(DIERR_OUTOFMEMORY); return; }
    uint32_t args[] = {p, context};
    if (mouse) {
        instance_info(p, size, DEV_MOUSE, w);
        if (!rt_call(c, callback, 2, args)) key = 0;
    }
    if (key) {
        instance_info(p, size, DEV_KEYBOARD, w);
        rt_call(c, callback, 2, args);
    }
    ghfree(p);
    RET(DI_OK);
}
static void di_GetDeviceStatus(CPU* c) { RET(device_type(ARG(1)) ? DI_OK : DIERR_DEVICENOTREG); }
static void r_ok(CPU* c) { RET(DI_OK); }
static void r_unsup(CPU* c) { RET(DIERR_UNSUPPORTED); }

static void dv_GetCapabilities(CPU* c) {
    Dev* d = dev_of(SELF); uint32_t p = ARG(1); if (!d || !p) { RET(DIERR_INVALIDPARAM); return; }
    uint32_t sz = rd32(p); if (sz < 24) { RET(DIERR_INVALIDPARAM); return; }
    memset(M + p + 4, 0, sz - 4);
    wr32(p + 4, 1 | 0);   /* DIDC_ATTACHED */
    wr32(p + 8, dev_kind(d->type));
    if (d->type == DEV_MOUSE) { wr32(p + 12, 3); wr32(p + 16, 8); }
    else wr32(p + 16, 128);
    RET(DI_OK);
}
static void dv_GetProperty(CPU* c) {   /* (this, rguidProp, pdiph) */
    Dev* d = dev_of(SELF); uint32_t prop = ARG(1), p = ARG(2);
    if (d && p && prop == 1) { wr32(p + 16, d->bufsize); RET(DI_OK); return; }   /* DIPROP_BUFFERSIZE: DIPROPDWORD.dwData */
    RET(DIERR_UNSUPPORTED);
}
static void dv_SetProperty(CPU* c) {
    Dev* d = dev_of(SELF); uint32_t prop = ARG(1), p = ARG(2);
    TR("SetProperty(%s, prop %u, value %u)\n", d && d->type == DEV_MOUSE ? "mouse" : "keyboard", prop, p ? rd32(p + 16) : 0);
    if (d && p && prop == 1) { pthread_mutex_lock(&g_imx); d->bufsize = rd32(p + 16); if (d->bufsize > RING) d->bufsize = RING; pthread_mutex_unlock(&g_imx); }
    RET(DI_OK);   /* DIPROP_AXISMODE (2), granularity, ...: accepted */
}
static void dv_Acquire(CPU* c) { Dev* d = dev_of(SELF); if (d) { if (!d->acquired) TR("Acquire(%s)\n", d->type == DEV_MOUSE ? "mouse" : "keyboard"); d->acquired = 1; } RET(DI_OK); }
static void dv_Unacquire(CPU* c) { Dev* d = dev_of(SELF); if (d) { d->acquired = 0; d->unacq_ms = now_ms(); } RET(DI_OK); }
/* v1.1.5: Windows hides the cursor while a mouse is acquired in exclusive mode (DISCL_EXCLUSIVE). A short Unacquire
 * (re-acquire with another cooperative level, every frame in UE3) does not count. Read without g_imx: called by
 * hle_exe.c with its window lock held, only plain ints are read. */
int hle_input_mouse_exclusive(void) {
    for (int i = 1; i < 3; i++) {
        Dev* d = &g_dev[i];
        if (d->type == DEV_MOUSE && (d->coop & 1) && (d->acquired || now_ms() - d->unacq_ms < 100)) return 1;
    }
    return 0;
}
static void dv_GetDeviceState(CPU* c) {   /* (this, cbData, lpvData) */
    Dev* d = dev_of(SELF); uint32_t n = ARG(1), p = ARG(2);
    if (!d || !p) { RET(DIERR_INVALIDPARAM); return; }
    pthread_mutex_lock(&g_imx);
    memset(M + p, 0, n);
    if (d->type == DEV_KEYBOARD) memcpy(M + p, d->keys, n < 256 ? n : 256);
    else if (n >= 16) {   /* DIMOUSESTATE (16) / DIMOUSESTATE2 (20) */
        wr32(p, (uint32_t)d->mx); wr32(p + 4, (uint32_t)d->my); wr32(p + 8, (uint32_t)d->mz); d->mx = d->my = d->mz = 0;
        memcpy(M + p + 12, d->mbtn, n >= 20 ? 8 : 4);
    }
    pthread_mutex_unlock(&g_imx);
    RET(DI_OK);
}
static void dv_GetDeviceData(CPU* c) {   /* (this, cbObjectData, rgdod, pdwInOut, dwFlags) */
    Dev* d = dev_of(SELF); uint32_t cb = ARG(1), out = ARG(2), pn = ARG(3), fl = ARG(4);
    if (!d || !pn) { RET(DIERR_INVALIDPARAM); return; }
    pthread_mutex_lock(&g_imx);
    uint32_t want = rd32(pn), got = 0;
    while (d->tail != d->head && got < want) {
        Ev* e = &d->ring[d->tail % RING];
        if (out) { uint32_t q = out + got * cb; memset(M + q, 0, cb); wr32(q, e->ofs); wr32(q + 4, e->data); wr32(q + 8, e->time); wr32(q + 12, e->seq); }
        if (!(fl & 1)) d->tail++;   /* DIGDD_PEEK */
        got++;
        if (fl & 1) break;
    }
    if (!out && want == 0xFFFFFFFFu) d->tail = d->head;   /* flush */
    pthread_mutex_unlock(&g_imx);
    wr32(pn, got);
    RET(DI_OK);
}
static void dv_SetDataFormat(CPU* c) { Dev* d = dev_of(SELF); TR("SetDataFormat(%s, %u bytes)\n", d && d->type == DEV_MOUSE ? "mouse" : "keyboard", ARG(1) ? rd32(ARG(1) + 12) : 0); RET(DI_OK); }
static void dv_SetEventNotification(CPU* c) { Dev* d = dev_of(SELF); if (d) d->hevent = ARG(1); RET(DI_OK); }
static void dv_SetCooperativeLevel(CPU* c) {
    Dev* d = dev_of(SELF);
    if (d && d->type == DEV_MOUSE && d->coop != ARG(2)) {   /* v1.1.5: exclusive (gameplay) / non-exclusive (cursor) - changes are logged */
        static unsigned n; if (n++ < 200) fprintf(stderr, "[dinput] mouse cooperative level %x -> %x (%s)\n", d->coop, ARG(2), ARG(2) & 1 ? "exclusive" : "non-exclusive");
    }
    if (d) d->coop = ARG(2); TR("SetCooperativeLevel(%s, %x)\n", d && d->type == DEV_MOUSE ? "mouse" : "keyboard", ARG(2)); RET(DI_OK); }
static void dv_GetDeviceInfo(CPU* c) {
    Dev* d = dev_of(SELF); uint32_t p = ARG(1);
    if (!d || !guest_span(p, 4)) { RET(DIERR_INVALIDPARAM); return; }
    int w = flavour(g_obj_dev[d->type], SELF);
    uint32_t size = rd32(p), full = instance_size(w);
    if ((size != full && size != full - 20) || !guest_span(p, size)) { RET(DIERR_INVALIDPARAM); return; }
    instance_info(p, size, d->type, w);
    RET(DI_OK);
}
static void dv_GetObjectInfo(CPU* c) { RET(DIERR_UNSUPPORTED); }

#define D(n, a, f) { "dinput8.com", n, a, f, 0 },
const HleDef hle_input_defs[] = {
    { "xinput1_3.dll", "#2", 2, xi_GetState, 0 }, { "xinput1_3.dll", "#3", 2, xi_SetState, 0 },
    { "xinput1_3.dll", "XInputGetState", 2, xi_GetState, 0 }, { "xinput1_3.dll", "XInputSetState", 2, xi_SetState, 0 },
    { "xinput1_3.dll", "#4", 3, xi_GetCapabilities, 0 }, { "xinput1_3.dll", "XInputGetCapabilities", 3, xi_GetCapabilities, 0 },
    { "dinput8.dll", "DirectInput8Create", 5, di_Create, 0 },
    D("IDirectInput8::QueryInterface", 3, qi) D("IDirectInput8::AddRef", 1, addref) D("IDirectInput8::Release", 1, release)
    D("IDirectInput8::CreateDevice", 4, di_CreateDevice)
    { "dinput8.com", "IDirectInput8::EnumDevices", 5, di_EnumDevices, HLE_NOLOCK }, /* guest callback may reenter HLE */
    D("IDirectInput8::GetDeviceStatus", 2, di_GetDeviceStatus) D("IDirectInput8::RunControlPanel", 3, r_ok)
    D("IDirectInput8::Initialize", 3, r_ok) D("IDirectInput8::FindDevice", 4, r_unsup)
    D("IDirectInput8::EnumDevicesBySemantics", 6, r_ok) D("IDirectInput8::ConfigureDevices", 5, r_unsup)
    D("IDirectInputDevice8::QueryInterface", 3, qi) D("IDirectInputDevice8::AddRef", 1, addref) D("IDirectInputDevice8::Release", 1, release)
    D("IDirectInputDevice8::GetCapabilities", 2, dv_GetCapabilities) D("IDirectInputDevice8::EnumObjects", 4, r_ok)
    D("IDirectInputDevice8::GetProperty", 3, dv_GetProperty) D("IDirectInputDevice8::SetProperty", 3, dv_SetProperty)
    D("IDirectInputDevice8::Acquire", 1, dv_Acquire) D("IDirectInputDevice8::Unacquire", 1, dv_Unacquire)
    D("IDirectInputDevice8::GetDeviceState", 3, dv_GetDeviceState) D("IDirectInputDevice8::GetDeviceData", 5, dv_GetDeviceData)
    D("IDirectInputDevice8::SetDataFormat", 2, dv_SetDataFormat) D("IDirectInputDevice8::SetEventNotification", 2, dv_SetEventNotification)
    D("IDirectInputDevice8::SetCooperativeLevel", 3, dv_SetCooperativeLevel) D("IDirectInputDevice8::GetObjectInfo", 4, dv_GetObjectInfo)
    D("IDirectInputDevice8::GetDeviceInfo", 2, dv_GetDeviceInfo) D("IDirectInputDevice8::RunControlPanel", 3, r_ok)
    D("IDirectInputDevice8::Initialize", 4, r_ok) D("IDirectInputDevice8::CreateEffect", 5, r_unsup)
    D("IDirectInputDevice8::EnumEffects", 4, r_ok) D("IDirectInputDevice8::GetEffectInfo", 3, r_unsup)
    D("IDirectInputDevice8::GetForceFeedbackState", 2, r_unsup) D("IDirectInputDevice8::SendForceFeedbackCommand", 2, r_unsup)
    D("IDirectInputDevice8::EnumCreatedEffectObjects", 4, r_ok) D("IDirectInputDevice8::Escape", 2, r_unsup)
    D("IDirectInputDevice8::Poll", 1, r_ok) D("IDirectInputDevice8::SendDeviceData", 5, r_unsup)
    D("IDirectInputDevice8::EnumEffectsInFile", 5, r_unsup) D("IDirectInputDevice8::WriteEffectToFile", 5, r_unsup)
    D("IDirectInputDevice8::BuildActionMap", 4, r_unsup) D("IDirectInputDevice8::SetActionMap", 4, r_unsup)
    D("IDirectInputDevice8::GetImageInfo", 2, r_unsup)
};
const unsigned hle_input_ndefs = sizeof hle_input_defs / sizeof hle_input_defs[0];
