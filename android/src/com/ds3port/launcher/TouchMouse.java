package com.ds3port.launcher;

/** Owns the two buttons of the explicit hidden-overlay touch-as-mouse mode. */
final class TouchMouse {
    interface Sink { void send(int action, float x, float y); }
    private final Sink sink;
    private int left = -1, right = -1;
    private float lastX, lastY;

    TouchMouse(Sink sink) { this.sink = sink; }
    void down(int id, float x, float y, boolean first) {
        if (first) { cancel(); left = id; send(0, x, y); }
        else if (left >= 0 && right < 0) { right = id; send(3, lastX, lastY); }
    }
    int cursor() { return left >= 0 ? left : right; }
    void move(float x, float y) { send(2, x, y); }
    void up(int id, float x, float y) {
        if (id == left) { left = -1; send(1, x, y); }
        else if (id == right) { right = -1; send(4, lastX, lastY); }
    }
    void cancel() {
        if (left >= 0) up(left, lastX, lastY);
        if (right >= 0) up(right, lastX, lastY);
    }
    private void send(int action, float x, float y) { lastX = x; lastY = y; sink.send(action, x, y); }
}
