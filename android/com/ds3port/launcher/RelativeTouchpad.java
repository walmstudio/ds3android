package com.ds3port.launcher;

/** Receives free fingers only. A two-finger tap is one gesture, not two clicks. */
final class RelativeTouchpad {
    interface Sink {
        void relative(float dx, float dy);
        void click(int button);
    }

    private final Sink sink;
    private int active, fingers, first, second;
    private float x0, y0, x1, y1, lastX, lastY;
    private long started;
    private boolean moved, dragging;

    RelativeTouchpad(Sink sink) { this.sink = sink; }

    void down(int id, float x, float y, long at) {
        int bit = 1 << id;
        if ((active & bit) != 0) return;
        active |= bit;
        if (++fingers == 1) {
            first = id; lastX = x0 = x; lastY = y0 = y; started = at;
        } else if (fingers == 2) {
            second = id; x1 = x; y1 = y;
        }
    }

    void move(int id, float x, float y, float slop) {
        if ((active & (1 << id)) == 0 || fingers > 2) return;
        float dx = x - (id == first ? x0 : x1), dy = y - (id == first ? y0 : y1);
        moved |= Math.abs(dx) + Math.abs(dy) >= slop;
        if (fingers == 1 && moved) dragging = true;
        if (dragging && id == first) {
            if (x != lastX || y != lastY) sink.relative(x - lastX, y - lastY);
            lastX = x; lastY = y;
        }
    }

    void up(int id, float x, float y, long at, float slop, boolean tapClick) {
        int bit = 1 << id;
        if ((active & bit) == 0) return;
        move(id, x, y, slop);
        active &= ~bit;
        if (active != 0) return;
        int button = tapClick && fingers <= 2 && !moved && at - started < 350 ? fingers - 1 : -1;
        cancel();
        if (button >= 0) sink.click(button);
    }

    void reject(int id) { if ((active & (1 << id)) != 0) cancel(); }

    void cancel() { active = fingers = 0; moved = dragging = false; }
}
