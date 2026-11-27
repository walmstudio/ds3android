package com.ds3port.launcher;

import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;

import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.Map;

/** Merges physical devices and touch into one guest XInput pad, with an optional KB/M sink. */
final class Pad {
    static final int UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, START = 0x10, BACK = 0x20, LTHUMB = 0x40, RTHUMB = 0x80,
            LB = 0x100, RB = 0x200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
    private boolean physicalXinput = true, touchXinput = true, nintendo;
    float lookScale = 1.2f;
    boolean invertY;

    private static class State {
        int buttons;
        float lx, ly, rx, ry, lt, rt;
        void clear() { buttons = 0; lx = ly = rx = ry = lt = rt = 0; }
        boolean held() { return buttons != 0 || lx != 0 || ly != 0 || rx != 0 || ry != 0 || lt != 0 || rt != 0; }
        boolean hasXinputValue() {
            return (buttons | u8(lt) | u8(rt) | s16(lx) | s16(-ly) | s16(rx) | s16(-ry)) != 0;
        }
        void merge(State s) {
            buttons |= s.buttons;
            lt = Math.max(lt, s.lt); rt = Math.max(rt, s.rt);
            if (strength(s.lx, s.ly) > strength(lx, ly)) { lx = s.lx; ly = s.ly; }
            if (strength(s.rx, s.ry) > strength(rx, ry)) { rx = s.rx; ry = s.ry; }
        }
    }
    private static final class Physical extends State {
        int keys, hat, dpadHat, backKeys;
        boolean digitalLT, digitalRT;
        float analogLT, analogRT;
        long lastPress;
        void resolve() {
            buttons = keys | hat | dpadHat | (backKeys != 0 ? BACK : 0);
            lt = digitalLT ? 1 : analogLT; rt = digitalRT ? 1 : analogRT;
        }
    }
    private final HashMap<Integer, Physical> devices = new HashMap<>();
    private Map<String, String> buttonMap = java.util.Collections.emptyMap();
    private boolean rearm;
    private long pressOrder;
    private int vBtn, leftDirs, rightDirs;
    private float vLT, vRT;
    private boolean usedTouch;
    private final Object overlayStick = new Object();
    private final IdentityHashMap<Object, float[]> leftSticks = new IdentityHashMap<>(), rightSticks = new IdentityHashMap<>();
    private final State physical = new State(), touch = new State(), xpad = new State(), kpad = new State();
    private boolean sentPad;
    private int sBtn, sLT, sRT, sLX, sLY, sRX, sRY;
    /* KB/M emulation state */
    private int kBtn; private boolean kw, ka, ks, kd, lmb, rmb;
    private float kRX, kRY;
    private final Handler h = new Handler(Looper.getMainLooper());
    private boolean ticking;

    /** Android gamepad key -> XInput button bit (0 = not a pad button); triggers are handled separately */
    static int bit(int code) {
        switch (code) {
        case KeyEvent.KEYCODE_BUTTON_A: return A;
        case KeyEvent.KEYCODE_BUTTON_B: return B;
        case KeyEvent.KEYCODE_BUTTON_X: return X;
        case KeyEvent.KEYCODE_BUTTON_Y: return Y;
        case KeyEvent.KEYCODE_BUTTON_L1: return LB;
        case KeyEvent.KEYCODE_BUTTON_R1: return RB;
        case KeyEvent.KEYCODE_BUTTON_THUMBL: return LTHUMB;
        case KeyEvent.KEYCODE_BUTTON_THUMBR: return RTHUMB;
        case KeyEvent.KEYCODE_BUTTON_START: return START;
        case KeyEvent.KEYCODE_BUTTON_SELECT: case KeyEvent.KEYCODE_BUTTON_MODE: case KeyEvent.KEYCODE_BACK: return BACK;
        case KeyEvent.KEYCODE_DPAD_UP: return UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
        default: return 0;
        }
    }
    static boolean handlesKey(int code) {
        return bit(code) != 0 || code == KeyEvent.KEYCODE_BUTTON_L2 || code == KeyEvent.KEYCODE_BUTTON_R2;
    }
    boolean physKey(int id, int code, boolean down) {
        if (!handlesKey(code)) return false;
        Physical p = devices.get(id);
        if (p == null) { if (!down) return true; p = new Physical(); }
        if (code == KeyEvent.KEYCODE_BUTTON_L2) p.digitalLT = down;
        else if (code == KeyEvent.KEYCODE_BUTTON_R2) p.digitalRT = down;
        else {
            int b = bit(code);
            if (b == BACK) {
                int mask = code == KeyEvent.KEYCODE_BUTTON_SELECT ? 1 : code == KeyEvent.KEYCODE_BUTTON_MODE ? 2 : 4;
                p.backKeys = down ? p.backKeys | mask : p.backKeys & ~mask;
            } else {
                if (nintendo) b = b == A ? B : b == B ? A : b == X ? Y : b == Y ? X : b;
                p.keys = down ? p.keys | b : p.keys & ~b;
            }
        }
        updateDevice(id, p, down);
        return true;
    }
    void setButtonMap(Map<String, String> map) {
        buttonMap = map == null || map.isEmpty() ? java.util.Collections.emptyMap() : map;
        flush();
    }
    /** After pause/resume: ignore held physical state until the pad goes fully neutral. */
    void requireNeutral() { rearm = true; flush(); }
    void physAxes(int id, float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy) {
        Physical p = devices.get(id);
        if (p == null) p = new Physical();
        int hat = hat(hx, hy);
        boolean changed = p.lx != lx || p.ly != ly || p.rx != rx || p.ry != ry || p.analogLT != lt || p.analogRT != rt || p.hat != hat;
        p.lx = lx; p.ly = ly; p.rx = rx; p.ry = ry; p.analogLT = lt; p.analogRT = rt; p.hat = hat;
        updateDevice(id, p, changed);
    }
    void physHat(int id, float hx, float hy) {
        Physical p = devices.get(id);
        if (p == null) p = new Physical();
        int hat = hat(hx, hy); boolean changed = p.dpadHat != hat; p.dpadHat = hat;
        updateDevice(id, p, changed);
    }
    private static int hat(float x, float y) {
        return (y < -.5f ? UP : y > .5f ? DOWN : 0) | (x < -.5f ? LEFT : x > .5f ? RIGHT : 0);
    }
    private void updateDevice(int id, Physical p, boolean changed) {
        p.resolve();
        PadMap.Applied mapped = PadMap.apply(p.buttons, p.lt, p.rt, buttonMap);
        p.buttons = mapped.buttons; p.lt = mapped.lt; p.rt = mapped.rt;
        if (physicalXinput ? p.hasXinputValue() : p.held()) {
            if (changed || p.lastPress == 0) p.lastPress = ++pressOrder;
            devices.put(id, p);
        }
        if (p.lastPress != 0) flush();
    }
    void removeDevice(int id) { if (devices.remove(id) != null) flush(); }
    int rumbleDevice() {
        if (!physicalXinput) return -1;
        long newest = 0; int id = -1;
        for (int d : devices.keySet()) if (devices.get(d).lastPress > newest) { id = d; newest = devices.get(d).lastPress; }
        return id;
    }
    boolean hasXinput() { return sentPad; }
    void configure(boolean physicalMode, boolean touchMode, boolean swap) {
        if (physicalXinput != physicalMode || nintendo != swap) devices.clear();
        if (touchXinput != touchMode) clearTouch();
        physicalXinput = physicalMode; touchXinput = touchMode; nintendo = swap;
        flush(); // releases any old sink just once, with unaffected sources still held
    }
    void virtButton(int b, boolean down) { vBtn = down ? vBtn | b : vBtn & ~b; flush(); }
    void virtTrigger(int which, boolean down) {
        if (which == 1) vLT = down ? 1 : 0; else vRT = down ? 1 : 0;
        flush();
    }
    void virtLeft(float x, float y) { virtStick(overlayStick, false, x, y); }
    void virtRight(float x, float y) { virtStick(overlayStick, true, x, y); }
    void virtStick(Object owner, boolean right, float x, float y) {
        IdentityHashMap<Object, float[]> sticks = right ? rightSticks : leftSticks;
        boolean held = x != 0 || y != 0;
        if (!held) sticks.remove(owner);
        else {
            float[] v = sticks.get(owner);
            if (v == null) { v = new float[2]; sticks.put(owner, v); }
            v[0] = x; v[1] = y;
        }
        flush();
    }
    void virtDirection(boolean right, int dir, boolean down) {
        int mask = 1 << dir;
        if (right) rightDirs = down ? rightDirs | mask : rightDirs & ~mask;
        else leftDirs = down ? leftDirs | mask : leftDirs & ~mask;
        flush();
    }
    private void clearTouch() {
        leftSticks.clear(); rightSticks.clear(); vBtn = leftDirs = rightDirs = 0; vLT = vRT = 0; usedTouch = false;
    }
    void releaseTouch() { clearTouch(); flush(); }
    void releaseAll() { devices.clear(); clearTouch(); rearm = true; flush(); }

    private static float strength(float x, float y) { return x * x + y * y; }
    private static void stick(State s, boolean right, int dirs, IdentityHashMap<Object, float[]> sticks) {
        float x = ((dirs >> 1) & 1) - ((dirs >> 3) & 1), y = ((dirs >> 2) & 1) - (dirs & 1);
        for (float[] v : sticks.values()) if (strength(v[0], v[1]) > strength(x, y)) { x = v[0]; y = v[1]; }
        if (right) { s.rx = x; s.ry = y; } else { s.lx = x; s.ly = y; }
    }
    private static void route(State out, State physical, State touch, boolean quantized) {
        out.clear();
        if (physical != null) out.merge(physical);
        if (touch == null) return;
        out.buttons |= touch.buttons;
        out.lt = Math.max(out.lt, touch.lt); out.rt = Math.max(out.rt, touch.rt);
        // Pick each stick as a vector: physical whenever deflected, otherwise touch.
        if (neutralStick(out.lx, out.ly, quantized)) { out.lx = touch.lx; out.ly = touch.ly; }
        if (neutralStick(out.rx, out.ry, quantized)) { out.rx = touch.rx; out.ry = touch.ry; }
    }
    private static boolean neutralStick(float x, float y, boolean quantized) {
        return quantized ? (s16(x) | s16(-y)) == 0 : x == 0 && y == 0;
    }
    private static int s16(float v) { return (int) Math.round(Math.max(-1d, Math.min(1d, v)) * (v < 0 ? 32768 : 32767)); }
    private static int u8(float v) { return Math.round(Math.max(0f, Math.min(1f, v)) * 255f); }
    private void flush() {
        physical.clear();
        for (Physical p : devices.values()) physical.merge(p);
        if (rearm) {
            /* Empty device list is not a observed neutral report. Stay armed
             * until a real pad update comes back at rest; otherwise the first
             * held event after resume would be applied immediately. */
            if (devices.isEmpty() || physical.held() || physical.hasXinputValue()) physical.clear();
            else rearm = false;
        }
        touch.buttons = vBtn; touch.lt = vLT; touch.rt = vRT;
        stick(touch, false, leftDirs, leftSticks); stick(touch, true, rightDirs, rightSticks);
        if (touchXinput && touch.hasXinputValue()) usedTouch = true;
        route(xpad, physicalXinput ? physical : null, touchXinput ? touch : null, true);
        if ((physicalXinput && !devices.isEmpty()) || (touchXinput && usedTouch)) sendPad(xpad);
        else if (sentPad) { GameActivity.nativePadConnect(0); sentPad = false; }
        route(kpad, physicalXinput ? null : physical, touchXinput ? null : touch, false);
        emulate(kpad.buttons, kpad.lx, kpad.ly, kpad.rx, kpad.ry, kpad.lt, kpad.rt);
    }
    private void sendPad(State s) {
        int btn = s.buttons, lx = s16(s.lx), ly = s16(-s.ly), rx = s16(s.rx), ry = s16(-s.ry), lt = u8(s.lt), rt = u8(s.rt);
        if (!sentPad ? (btn | lx | ly | rx | ry | lt | rt) == 0
                : btn == sBtn && lx == sLX && ly == sLY && rx == sRX && ry == sRY && lt == sLT && rt == sRT) return;
        GameActivity.nativePad(btn, lt, rt, lx, ly, rx, ry);
        sentPad = true; sBtn = btn; sLX = lx; sLY = ly; sRX = rx; sRY = ry; sLT = lt; sRT = rt;
    }
    /* ---- keyboard + mouse emulation (profile kbm) ---- */
    private static final int[][] MAP = {   /* pad bit -> Android keycode (sent as a PC key by nativeKey) */
        { A, KeyEvent.KEYCODE_E }, { B, KeyEvent.KEYCODE_Q }, { X, KeyEvent.KEYCODE_SPACE }, { Y, KeyEvent.KEYCODE_F },
        { LB, KeyEvent.KEYCODE_R }, { RB, KeyEvent.KEYCODE_SHIFT_LEFT }, { LTHUMB, KeyEvent.KEYCODE_CTRL_LEFT }, { RTHUMB, KeyEvent.KEYCODE_J },
        { START, KeyEvent.KEYCODE_ESCAPE }, { BACK, KeyEvent.KEYCODE_M },
        { UP, KeyEvent.KEYCODE_DPAD_UP }, { DOWN, KeyEvent.KEYCODE_DPAD_DOWN }, { LEFT, KeyEvent.KEYCODE_DPAD_LEFT }, { RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT } };
    private void emulate(int btn, float lx, float ly, float rx, float ry, float lt, float rt) {
        int ch = btn ^ kBtn;
        if (ch != 0) for (int[] m : MAP) if ((ch & m[0]) != 0) GameActivity.nativeKeyFrom(GameActivity.KBM_PAD, m[1], (btn & m[0]) != 0 ? 1 : 0, 0);
        kBtn = btn;
        float t = 0.45f;
        boolean w = ly < -t && -ly > Math.abs(lx) * 0.42f, s = ly > t && ly > Math.abs(lx) * 0.42f;
        boolean a = lx < -t && -lx > Math.abs(ly) * 0.42f, d = lx > t && lx > Math.abs(ly) * 0.42f;
        if (w != kw) { kw = w; GameActivity.nativeKeyFrom(GameActivity.KBM_PAD, KeyEvent.KEYCODE_W, w ? 1 : 0, 0); }
        if (a != ka) { ka = a; GameActivity.nativeKeyFrom(GameActivity.KBM_PAD, KeyEvent.KEYCODE_A, a ? 1 : 0, 0); }
        if (s != ks) { ks = s; GameActivity.nativeKeyFrom(GameActivity.KBM_PAD, KeyEvent.KEYCODE_S, s ? 1 : 0, 0); }
        if (d != kd) { kd = d; GameActivity.nativeKeyFrom(GameActivity.KBM_PAD, KeyEvent.KEYCODE_D, d ? 1 : 0, 0); }
        boolean l = rt > 0.5f, r = lt > 0.5f;
        if (l != lmb) { lmb = l; GameActivity.nativeMouseButtonFrom(GameActivity.KBM_PAD, 0, l ? 1 : 0); }
        if (r != rmb) { rmb = r; GameActivity.nativeMouseButtonFrom(GameActivity.KBM_PAD, 1, r ? 1 : 0); }
        kRX = Math.abs(rx) > 0.15f ? rx : 0; kRY = Math.abs(ry) > 0.15f ? ry : 0;
        if (kRX == 0 && kRY == 0) { if (ticking) { h.removeCallbacks(tick); ticking = false; } }
        else if (!ticking) { ticking = true; h.post(tick); }
    }
    /** right stick -> relative mouse motion, 60 times a second while the stick is deflected (quadratic response) */
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (kRX == 0 && kRY == 0) { ticking = false; return; }
            float k = 16f * lookScale;
            float dx = Math.signum(kRX) * kRX * kRX * k, dy = Math.signum(kRY) * kRY * kRY * k;
            GameActivity.nativeMouseRel(dx, invertY ? -dy : dy);
            h.postDelayed(this, 16);
        }
    };
}
