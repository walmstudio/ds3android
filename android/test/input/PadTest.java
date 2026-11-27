package com.ds3port.launcher;

import android.view.KeyEvent;
import android.view.InputDevice;
import android.view.MotionEvent;
import java.util.HashMap;
import java.util.Map;

final class PadTest {
    static void eq(int expected, int actual, String label) {
        if (expected != actual) throw new AssertionError(label + ": expected " + expected + ", got " + actual);
    }
    public static void main(String[] args) {
        Pad pad = new Pad();
        switch (args[0]) {
        case "triggers":
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_L2, true);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_R2, true);
            pad.physAxes(1, 0, .5f, 0, 0, .25f, .5f, 0, 0);
            eq(255, GameActivity.state[1], "held digital LT survives analog motion");
            eq(255, GameActivity.state[2], "held digital RT survives analog motion");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_L2, false);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_R2, false);
            eq(64, GameActivity.state[1], "releasing key retains analog LT");
            eq(128, GameActivity.state[2], "releasing key retains analog RT");
            break;
        case "hat":
            eq(1, PadAxes.isControllerKey(InputDevice.SOURCE_DPAD, KeyEvent.KEYCODE_DPAD_UP) ? 1 : 0, "DPAD-only source accepted");
            eq(0, PadAxes.isControllerKey(InputDevice.SOURCE_KEYBOARD, KeyEvent.KEYCODE_DPAD_UP) ? 1 : 0, "keyboard arrows stay KB/M");
            pad.physKey(1, KeyEvent.KEYCODE_DPAD_UP, true);
            pad.physAxes(1, 0, 0, 0, 0, 0, 0, 1, -1);
            pad.physKey(1, KeyEvent.KEYCODE_DPAD_UP, false);
            eq(Pad.UP | Pad.RIGHT, GameActivity.state[0], "key release preserves diagonal HAT");
            pad.physKey(1, KeyEvent.KEYCODE_DPAD_UP, true);
            pad.physAxes(1, 0, 0, 0, 0, 0, 0, 0, 0);
            eq(Pad.UP, GameActivity.state[0], "HAT release preserves key");
            pad.physAxes(1, 1, 0, 0, 0, 0, 0, 0, 0);
            pad.physHat(1, -1, 0);
            eq(Pad.UP | Pad.LEFT, GameActivity.state[0], "dedicated DPAD HAT merges with digital directions");
            eq(32767, GameActivity.state[3], "DPAD-only motion does not reset sticks");
            break;
        case "touch":
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            pad.virtLeft(1, 0); pad.virtRight(0, -.5f); pad.virtTrigger(2, true);
            eq(32767, GameActivity.state[3], "touch left stick");
            eq(16384, GameActivity.state[6], "touch right stick");
            eq(255, GameActivity.state[2], "touch trigger with both sticks");
            pad.releaseTouch();
            eq(Pad.A, GameActivity.state[0], "touch cancel preserves physical button");
            break;
        case "reset":
            pad.releaseAll();
            eq(0, GameActivity.padWrites, "idle release must not attach a pad");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            pad.releaseAll();
            eq(0, GameActivity.connected ? 1 : 0, "pause disconnects guest pad");
            break;
        case "axes": {
            Map<Integer, PadAxes.Range> ranges = new HashMap<>();
            Map<Integer, Float> values = new HashMap<>();
            ranges.put(MotionEvent.AXIS_RX, new PadAxes.Range(-100, 200, 20));
            ranges.put(MotionEvent.AXIS_RY, new PadAxes.Range(-50, 50, 5));
            ranges.put(MotionEvent.AXIS_LTRIGGER, new PadAxes.Range(-1, 1, 0));
            ranges.put(MotionEvent.AXIS_Z, new PadAxes.Range(0, 1023, 0));
            ranges.put(MotionEvent.AXIS_RZ, new PadAxes.Range(0, 1023, 0));
            values.put(MotionEvent.AXIS_RX, 110f);
            values.put(MotionEvent.AXIS_RY, -50f);
            values.put(MotionEvent.AXIS_Z, 0f);
            values.put(MotionEvent.AXIS_RZ, 512f);
            PadAxes.apply(pad, 1, ranges::get, a -> values.getOrDefault(a, 0f), false);
            eq(16384, GameActivity.state[5], "RX-only half after flat");
            eq(32767, GameActivity.state[6], "RY negative end maps to positive full scale");
            eq(128, GameActivity.state[1], "signed trigger midpoint");
            eq(128, GameActivity.state[2], "positive-only RZ as trigger");
            values.put(MotionEvent.AXIS_RX, -18f);
            values.put(MotionEvent.AXIS_RY, 50f);
            ranges.put(MotionEvent.AXIS_Z, new PadAxes.Range(-1, 1, .1f));
            ranges.remove(MotionEvent.AXIS_RX);
            values.put(MotionEvent.AXIS_Z, .1f);
            PadAxes.apply(pad, 1, ranges::get, a -> values.getOrDefault(a, 0f), false);
            eq(0, GameActivity.state[5], "split Z/RZ not a stick pair; RX unavailable");
            eq(-32768, GameActivity.state[6], "RY positive end maps to negative full scale");
            ranges.remove(MotionEvent.AXIS_Z);
            ranges.put(MotionEvent.AXIS_RX, new PadAxes.Range(-100, 200, 20));
            PadAxes.apply(pad, 1, ranges::get, a -> values.getOrDefault(a, 0f), false);
            eq(0, GameActivity.state[5], "RX inside the flat region");
            ranges.put(MotionEvent.AXIS_Z, new PadAxes.Range(-1, 1, .1f));
            ranges.put(MotionEvent.AXIS_RZ, new PadAxes.Range(-1, 1, .1f));
            values.put(MotionEvent.AXIS_Z, -.55f);
            values.put(MotionEvent.AXIS_RZ, 1f);
            PadAxes.apply(pad, 1, ranges::get, a -> values.getOrDefault(a, 0f), false);
            eq(-16384, GameActivity.state[5], "conventional paired Z/RZ right stick");
            eq(-32768, GameActivity.state[6], "RZ full endpoint");
            break;
        }
        case "sourceOwnership":
            pad.physAxes(1, 0, 0, 0, 0, 0, 0, 0, 0);
            eq(0, GameActivity.padWrites, "idle joystick report is not ownership");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            pad.physKey(2, KeyEvent.KEYCODE_BUTTON_B, true);
            pad.physAxes(1, 0, 0, 0, 0, 0, 0, 0, 0);
            eq(2, pad.rumbleDevice(), "unchanged joystick snapshot does not steal rumble");
            pad.virtTrigger(1, true);
            pad.removeDevice(1);
            eq(Pad.B, GameActivity.state[0], "unplug clears only that device");
            pad.removeDevice(2);
            eq(255, GameActivity.state[1], "touch stays held through unplug");
            eq(-1, pad.rumbleDevice(), "unplug releases rumble target");
            pad.releaseTouch();
            eq(0, GameActivity.connected ? 1 : 0, "last owner released disconnects");
            break;
        case "quantizedOwnership":
            pad.virtButton(Pad.A, true);
            pad.physAxes(1, 0, 0, 0, 0, 1f / 1023, 0, 0, 0);
            eq(0, GameActivity.state[1], "tiny LT quantizes to zero");
            pad.releaseTouch();
            eq(0, GameActivity.connected ? 1 : 0, "quantized-neutral physical sample cannot keep touch pad attached");
            eq(-1, pad.rumbleDevice(), "quantized-neutral physical sample owns no rumble target");
            pad.physAxes(1, 0, 0, 0, 0, 3f / 1023, 0, 0, 0);
            eq(1, GameActivity.state[1], "real physical input attaches and publishes");
            pad.physAxes(1, 0, 0, 0, 0, 0, 0, 0, 0);
            eq(1, GameActivity.connected ? 1 : 0, "established physical device remains attached at rest");
            pad.virtRight(.5f, 0);
            pad.physAxes(1, 0, 0, .000001f, 0, 0, 0, 0, 0);
            eq(16384, GameActivity.state[5], "sub-LSB physical stick does not suppress touch stick");
            break;
        case "stickOwnership": {
            Object one = new Object(), two = new Object();
            pad.virtStick(one, true, .5f, 0);
            pad.virtStick(two, true, 0, -.75f);
            eq(24575, GameActivity.state[6], "stronger virtual stick selected as a vector");
            pad.virtStick(two, true, 0, 0);
            eq(16384, GameActivity.state[5], "releasing virtual stick restores other owner");
            pad.virtDirection(true, 0, true);
            pad.virtDirection(true, 1, true);
            eq(32767, GameActivity.state[5], "right held alongside up");
            eq(32767, GameActivity.state[6], "up held alongside right");
            pad.virtDirection(true, 1, false);
            eq(32767, GameActivity.state[6], "releasing right preserves up");
            pad.physAxes(1, 0, 0, .25f, 0, 0, 0, 0, 0);
            eq(8192, GameActivity.state[5], "non-neutral physical stick takes priority");
            eq(0, GameActivity.state[6], "stick components are not taken from different sources");
            break;
        }
        case "sinks":
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            pad.configure(false, true, false);
            eq(0, GameActivity.connected ? 1 : 0, "physical XInput -> KB/M releases XInput");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            pad.virtTrigger(2, true);
            eq(255, GameActivity.state[2], "stock touch stays XInput in physical KB/M mode");
            eq(1, GameActivity.kbm.size(), "stock touch generated no KB/M event");
            pad.configure(true, true, false);
            eq(255, GameActivity.state[2], "physical mode change preserves the touch sink");
            if (!GameActivity.kbm.equals(java.util.Arrays.asList("33:1", "33:0")))
                throw new AssertionError("old keyboard sink did not release: " + GameActivity.kbm);
            break;
        case "remap": {
            java.util.Map<String, String> map = new java.util.HashMap<>();
            map.put("a", "b");
            map.put("b", "none");
            map.put("lt", "a");
            pad.setButtonMap(map);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            eq(Pad.B, GameActivity.state[0], "A remapped to B");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, false);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_B, true);
            eq(0, GameActivity.state[0], "B remapped to none");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_B, false);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_L2, true);
            eq(Pad.A, GameActivity.state[0], "LT remapped to A");
            eq(0, GameActivity.state[1], "LT no longer a trigger");
            PadMap.Applied auto = PadMap.apply(Pad.A, 0, 0, java.util.Collections.emptyMap());
            eq(Pad.A, auto.buttons, "empty map keeps A");
            break;
        }
        case "rearm":
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            eq(Pad.A, GameActivity.state[0], "A held before pause");
            pad.requireNeutral();
            eq(0, GameActivity.state[0], "held A must not rearm after resume");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            eq(0, GameActivity.state[0], "still-held A ignored until release");
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, false);
            pad.physKey(1, KeyEvent.KEYCODE_BUTTON_A, true);
            eq(Pad.A, GameActivity.state[0], "fresh press after neutral is accepted");
            break;
        default: throw new AssertionError(args[0]);
        }
        System.out.println(args[0] + ": PASS");
    }
}
