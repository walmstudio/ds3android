package com.ds3port.launcher;

import java.util.ArrayList;
import java.util.Arrays;

final class TouchMouseTest {
    public static void main(String[] args) {
        ArrayList<Integer> actions = new ArrayList<>();
        TouchMouse mouse = new TouchMouse((action, x, y) -> actions.add(action));
        mouse.down(5, 10, 10, true);
        mouse.down(2, 20, 20, false);
        mouse.up(5, 10, 10);
        mouse.up(2, 20, 20); // second finger has shifted from index 1 to index 0
        mouse.cancel();
        mouse.down(99, 0, 0, false); // orphaned pointer-down after a layer reconfiguration
        mouse.down(9, 0, 0, true);
        mouse.down(3, 0, 0, false);
        mouse.cancel();
        if (!actions.equals(Arrays.asList(0, 3, 1, 4, 0, 3, 1, 4))) throw new AssertionError(actions);
        System.out.println("touch pointer ownership: PASS");
    }
}
