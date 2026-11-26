package com.ds3port.launcher;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

final class RelativeTouchpadTest implements RelativeTouchpad.Sink {
    final List<String> out = new ArrayList<>();
    final RelativeTouchpad pad = new RelativeTouchpad(this);
    public void relative(float dx, float dy) { out.add(dx + ":" + dy); }
    public void click(int button) { out.add("click:" + button); }
    void expect(String... events) {
        if (!out.equals(Arrays.asList(events))) throw new AssertionError("expected " + Arrays.toString(events) + ", got " + out);
        out.clear();
    }
    void down(int id, float x, long at) { pad.down(id, x, 200, at); }
    void up(int id, float x, long at) { pad.up(id, x, 200, at, 10, true); }
    void run() {
        down(5, 400, 0); expect(); up(5, 402, 100); expect("click:0");
        down(9, 700, 200); pad.move(9, 706, 200, 10); expect();
        pad.move(9, 712, 204, 10); pad.move(9, 709, 202, 10);
        pad.up(9, 709, 202, 300, 10, true); expect("12.0:4.0", "-3.0:-2.0");
        down(9, 500, 400); down(3, 850, 440); up(9, 500, 490); expect();
        pad.move(3, 851, 200, 10); up(3, 851, 515); expect("click:1");
        down(9, 500, 550); down(3, 850, 570); pad.move(3, 880, 200, 10);
        up(3, 880, 605); up(9, 500, 625); expect();
        down(9, 500, 650); down(3, 850, 670); down(4, 600, 680);
        up(4, 600, 690); up(3, 850, 700); up(9, 500, 730); expect();
        down(9, 500, 750); down(3, 800, 770); pad.cancel();
        up(9, 500, 780); up(3, 800, 790); expect();
        down(9, 500, 800); pad.up(9, 500, 200, 845, 10, false); expect();
        down(9, 500, 900); up(9, 500, 1300); expect();
        down(9, 500, 1350); up(9, 520, 1425); expect("20.0:0.0");
        down(9, 500, 1500); pad.move(9, 520, 200, 10); down(3, 800, 1550);
        pad.move(9, 530, 200, 10); up(3, 800, 1600); up(9, 530, 1610);
        expect("20.0:0.0", "10.0:0.0");
    }
    public static void main(String[] args) {
        new RelativeTouchpadTest().run();
        System.out.println("relative touchpad gestures: PASS");
    }
}
