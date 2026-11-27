package com.ds3port.launcher;

import java.util.function.BiConsumer;

/** UI-thread gate: only a resumed, focused activity with a live surface owns output. */
final class GameLifecycle<S> {
    private final BiConsumer<S, Boolean> output;
    private S surface, attached;
    private boolean resumed, focused;

    GameLifecycle(BiConsumer<S, Boolean> output) { this.output = output; }
    void resumed(boolean value) { resumed = value; update(); }
    void focused(boolean value) { focused = value; update(); }
    void surface(S value) { surface = value; update(); }
    private void update() {
        S next = resumed && focused ? surface : null;
        if (next == attached) return;
        attached = next;
        output.accept(next, next != null);
    }
}
