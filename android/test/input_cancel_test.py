"""Run the Android dispatch methods with host event stubs and the real TouchMouse."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'android/src/com/ds3port/launcher'


def method(source, signature, optional=False):
    if optional and signature not in source:
        return ''
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    for end in range(body + 1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError(signature)


activity = (SRC / 'GameActivity.java').read_text()
overlay = (SRC / 'ControlsOverlay.java').read_text().replace('android.view.InputDevice', 'InputDevice')
fixture = '''package com.ds3port.launcher;
import java.util.*;
final class InputDevice { static final int SOURCE_MOUSE = 1, SOURCE_TOUCHSCREEN = 2; }
final class ControlsSettings { static String layout(String layout) { return layout; } }
final class MotionEvent {
    static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3,
        ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6;
    static final int FLAG_CANCELED = 32;
    final int action; final boolean hardware;
    int flags; float firstX = 100;
    MotionEvent(int action, boolean hardware) { this.action = action; this.hardware = hardware; }
    int getActionMasked() { return action; }
    boolean isFromSource(int src) { return src == (hardware ? InputDevice.SOURCE_MOUSE : InputDevice.SOURCE_TOUCHSCREEN); }
    int getFlags() { return flags; }
    int getActionIndex() { return getPointerCount() - 1; }
    int getPointerId(int idx) { return idx == 0 ? 5 : 9; }
    int findPointerIndex(int id) { return id == 5 ? 0 : -1; }
    int getPointerCount() { return action == ACTION_POINTER_DOWN || action == ACTION_POINTER_UP ? 2 : 1; }
    float getX(int idx) { return idx == 0 ? firstX : 200; }
    float getY(int idx) { return 100; }
    int getHistorySize() { return 0; }
    float getHistoricalX(int idx, int h) { throw new AssertionError(); }
    float getHistoricalY(int idx, int h) { throw new AssertionError(); }
    long getEventTime() { return action == ACTION_UP ? 100 : 0; }
}
class GameActivity {
    boolean inputActive = true;
    ControlsOverlay overlay;
    final List<Integer> touch = new ArrayList<>(), hardware = new ArrayList<>();
    final TouchMouse touchMouse = new TouchMouse((action, x, y) -> touch.add(action));
    final TouchMouse hardwareMouse = new TouchMouse((action, x, y) -> hardware.add(action));
    boolean acceptsTouchInput() { return inputActive; }
    %s
    %s
    %s
}
class ControlsOverlay {
    interface Mouse { void touch(MotionEvent e); }
    final GameActivity context = new GameActivity();
    final Mouse mouse = context::mouseTouch;
    final boolean strict;
    boolean shown, editing, customMode = true, skyMode, longFired;
    boolean tapClick = true;
    String prefs = "custom";
    final List<Integer> clicks = new ArrayList<>();
    final List<Float> deltas = new ArrayList<>();
    final RelativeTouchpad touchpad = new RelativeTouchpad(new RelativeTouchpad.Sink() {
        public void relative(float dx, float dy) { deltas.add(dx); }
        public void click(int button) { clicks.add(button); }
    });
    int togglePtr = -1, kbdPtr = -1, released;
    float dp = 1;
    static class Button { boolean hidden; float r, x, y; }
    static class HapticFeedbackConstants { static final int KEYBOARD_TAP = 0; }
    final Button toggleBtn = new Button(), kbdBtn = new Button();
    final Runnable longPress = () -> {};
    final CustomControls cc = new CustomControls();
    static class CustomControls {
        boolean down(int id, float x, float y) { return false; }
        boolean move(int id, float x, float y) { return false; }
        boolean contains(float x, float y) { return false; }
        boolean controlCloserThan(float x, float y, float cx, float cy) { return false; }
        boolean keyboardDown(int id, float x, float y) { return false; }
        boolean up(int id, float x, float y) { return false; }
        void edit(int act, int id, float x, float y, MotionEvent e) {}
    }
    ControlsOverlay(boolean strict) { this.strict = strict; context.overlay = this; }
    GameActivity getContext() { return context; }
    boolean strictXinput() { return strict; }
    void releaseAll() { released++; context.cancelMouseTouch(); touchpad.cancel(); }
    void invalidate() {}
    void postDelayed(Runnable r, int delay) {}
    void removeCallbacks(Runnable r) {}
    void finishEdit() {}
    void setShown(boolean s) { shown = s; }
    boolean hit(Button b, float x, float y, float extra) { return false; }
    void haptic(int flag) {}
    void keyboard() {}
    void edit(int act, int id, float x, float y, MotionEvent e) {}
    void down(int id, float x, float y) {}
    void move(int id, float x, float y) {}
    void up(int id, float x, float y, boolean cancel) {}
    %s
    %s
}
final class MouseCancelTest {
    static void check(List<Integer> actions, String name, Integer... expected) {
        if (!actions.equals(Arrays.asList(expected))) throw new AssertionError(name + ": " + actions);
    }
    public static void main(String[] a) {
        ControlsOverlay o = new ControlsOverlay(false);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, true));
        check(o.context.hardware, "hidden KB/M mouse down", 0);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, true));
        check(o.context.hardware, "hidden KB/M mouse cancel must release hardware button", 0, 1);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, true));
        check(o.context.hardware, "unowned duplicate cancel is silent", 0, 1);

        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, true));
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, false));
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, false));
        check(o.context.touch, "touch cancel releases only touch pointer", 0, 1);
        check(o.context.hardware, "touch cancel preserves hardware hold", 0, 1, 0);
        o.shown = true; // cancel after a visibility change must still reach the old owner
        int released = o.released;
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, true));
        check(o.context.hardware, "mouse cancel follows owner across visibility change", 0, 1, 0, 1);
        if (o.released != released) throw new AssertionError("mouse cancel with hardware owner reset overlay controls");

        o = new ControlsOverlay(true);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, true));
        check(o.context.hardware, "strict unowned hardware cancel is silent");
        if (o.released != 0) throw new AssertionError("strict mouse cancel reset touch controls");
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, true));
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, false));
        check(o.context.hardware, "strict touch cancel does not release hardware", 0);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_CANCEL, true));
        check(o.context.hardware, "strict hardware cancel still releases hardware", 0, 1);
        for (boolean shown : new boolean[] {false, true}) {
            o = new ControlsOverlay(false); o.prefs = "kbm"; o.shown = shown;
            o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, false));
            check(o.clicks, "Layout 1 must not click before release");
            o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_UP, false));
            check(o.clicks, "Layout 1 tap at current cursor", 0);
            check(o.context.touch, "Layout 1 must not use absolute touch");
            if (!o.deltas.isEmpty()) throw new AssertionError("tap moved the cursor");
            o.clicks.clear();
            o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, false));
            MotionEvent swipe = new MotionEvent(MotionEvent.ACTION_MOVE, false);
            swipe.firstX = 132; o.onTouchEvent(swipe);
            MotionEvent end = new MotionEvent(MotionEvent.ACTION_UP, false);
            end.firstX = 135; o.onTouchEvent(end);
            if (!o.deltas.equals(Arrays.asList(32f, 3f))) throw new AssertionError("lost swipe, overlay shown=" + shown);
            check(o.clicks, "swipe does not click");
        }
        o = new ControlsOverlay(false); o.prefs = "kbm";
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, false));
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_POINTER_DOWN, false));
        MotionEvent palmUp = new MotionEvent(MotionEvent.ACTION_POINTER_UP, false);
        palmUp.flags = MotionEvent.FLAG_CANCELED; o.onTouchEvent(palmUp);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_UP, false));
        check(o.clicks, "rejected finger must not click");

        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_DOWN, false));
        MotionEvent secondDown = new MotionEvent(MotionEvent.ACTION_POINTER_DOWN, false);
        secondDown.firstX = 120; o.onTouchEvent(secondDown);
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_POINTER_UP, false));
        o.onTouchEvent(new MotionEvent(MotionEvent.ACTION_UP, false));
        check(o.clicks, "movement at pointer transition must not become RMB");
        if (!o.deltas.equals(Arrays.asList(20f, -20f))) throw new AssertionError("lost drag: " + o.deltas);
        System.out.println("overlay mouse cancel routing: PASS");
    }
}
''' % (method(activity, 'void mouseTouch('), method(activity, 'void cancelMouseTouch('),
       method(activity, 'boolean cancelHardwareMouse(', optional=True), method(overlay, 'public boolean onTouchEvent('),
       method(overlay, 'private void touchpadEvent('))

(ROOT / 'tmp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='input-cancel.', dir=ROOT / 'tmp') as work:
    java = Path(work) / 'MouseCancelTest.java'
    java.write_text(fixture)
    subprocess.run(['javac', '-d', work, java, SRC / 'TouchMouse.java', SRC / 'RelativeTouchpad.java'], check=True)
    subprocess.run(['java', '-cp', work, 'com.ds3port.launcher.MouseCancelTest'], check=True)
