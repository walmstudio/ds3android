package com.ds3port.launcher;

import java.util.ArrayList;
import java.util.List;

/** Captures Pad's JNI boundary; input processing uses the production Pad. */
final class GameActivity {
    static final int KBM_PAD = 1;
    static int[] state = new int[7];
    static boolean connected;
    static int padWrites;
    static final List<String> kbm = new ArrayList<>();
    static void nativePad(int b, int lt, int rt, int lx, int ly, int rx, int ry) {
        state = new int[]{b, lt, rt, lx, ly, rx, ry}; connected = true; padWrites++;
    }
    static void nativePadConnect(int on) { connected = on != 0; if (!connected) state = new int[7]; }
    static void nativeKeyFrom(int source, int code, int down, int unicode) { kbm.add(code + ":" + down); }
    static void nativeMouseButtonFrom(int source, int b, int down) { kbm.add("mouse" + b + ":" + down); }
    static void nativeMouseRel(float x, float y) { kbm.add("mouse move"); }
}
