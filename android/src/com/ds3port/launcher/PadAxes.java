package com.ds3port.launcher;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/** Decode Android ranges; HLE receives only canonical XInput values. */
final class PadAxes {
    interface Ranges { Range get(int axis); }
    interface Values { float get(int axis); }
    static final class Range {
        final float min, max, flat;
        Range(float min, float max, float flat) { this.min = min; this.max = max; this.flat = flat; }
        boolean valid() { return Float.isFinite(min) && Float.isFinite(max) && max > min; }
        boolean centered() { return valid() && min < 0 && max > 0; }
        boolean positive() { return valid() && min >= 0; }
    }
    private PadAxes() {}

    static boolean isControllerKey(int source, int code) {
        if (!Pad.handlesKey(code)) return false;
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || (source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD
                || (code >= KeyEvent.KEYCODE_BUTTON_A && code <= KeyEvent.KEYCODE_BUTTON_MODE);
    }

    static void apply(Pad pad, MotionEvent e, boolean hatOnly) {
        InputDevice device = e.getDevice();
        if (device == null) return;
        apply(pad, e.getDeviceId(), axis -> {
            InputDevice.MotionRange r = device.getMotionRange(axis, e.getSource());
            return r != null ? new Range(r.getMin(), r.getMax(), r.getFlat()) : null;
        }, e::getAxisValue, hatOnly);
    }
    static void apply(Pad pad, int id, Ranges r, Values v, boolean hatOnly) {
        float hx = stick(r.get(MotionEvent.AXIS_HAT_X), v.get(MotionEvent.AXIS_HAT_X));
        float hy = stick(r.get(MotionEvent.AXIS_HAT_Y), v.get(MotionEvent.AXIS_HAT_Y));
        if (hatOnly) { pad.physHat(id, hx, hy); return; }
        Range z = r.get(MotionEvent.AXIS_Z), rz = r.get(MotionEvent.AXIS_RZ);
        boolean zStick = z != null && z.centered() && rz != null && rz.centered();
        int rx = zStick ? MotionEvent.AXIS_Z : MotionEvent.AXIS_RX;
        int ry = zStick ? MotionEvent.AXIS_RZ : MotionEvent.AXIS_RY;
        float lt = Math.max(trigger(r, v, MotionEvent.AXIS_LTRIGGER), trigger(r, v, MotionEvent.AXIS_BRAKE));
        float rt = Math.max(trigger(r, v, MotionEvent.AXIS_RTRIGGER), trigger(r, v, MotionEvent.AXIS_GAS));
        if (z != null && z.positive()) lt = Math.max(lt, normalizeTrigger(z, v.get(MotionEvent.AXIS_Z)));
        if (rz != null && rz.positive()) rt = Math.max(rt, normalizeTrigger(rz, v.get(MotionEvent.AXIS_RZ)));
        pad.physAxes(id, stick(r.get(MotionEvent.AXIS_X), v.get(MotionEvent.AXIS_X)),
                stick(r.get(MotionEvent.AXIS_Y), v.get(MotionEvent.AXIS_Y)),
                stick(zStick ? z : r.get(rx), v.get(rx)), stick(zStick ? rz : r.get(ry), v.get(ry)), lt, rt, hx, hy);
    }
    private static float stick(Range r, float value) {
        if (r == null || !r.centered() || !Float.isFinite(value)) return 0;
        // Android's centered axes report zero at rest; do not shift asymmetric ranges to their midpoint.
        float end = value < 0 ? -r.min : r.max;
        return Math.copySign(normalize(Math.abs(value), end, r.flat), value);
    }
    private static float trigger(Ranges r, Values v, int axis) { return normalizeTrigger(r.get(axis), v.get(axis)); }
    private static float normalizeTrigger(Range r, float value) {
        return r != null && r.valid() && Float.isFinite(value) ? normalize(value - r.min, r.max - r.min, r.flat) : 0;
    }
    private static float normalize(float value, float end, float flat) {
        // Reserve both endpoints even for a malformed Android flat value.
        if (!Float.isFinite(flat) || flat < 0 || flat >= end) flat = 0;
        return Math.min(1, Math.max(0, (value - flat) / (end - flat)));
    }
}
