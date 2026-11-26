package com.ds3port.launcher;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public class PortDisplayOptionsTest {
    private static Map<String, String> env(PortDisplayOptions options, boolean windowed) {
        Map<String, String> result = new HashMap<>();
        options.putEnv(result, windowed);
        return result;
    }

    private static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
        System.out.println("ok  " + name);
    }

    public static void main(String[] args) {
        Map<String, ?> none = Collections.emptyMap();
        PortDisplayOptions defaults = new PortDisplayOptions(none, ignored -> null);
        check(env(defaults, true).equals(Map.of("ME1_FPS_LIMIT", "30", "ME1_STRETCH_MODE", "fit")), "default 30 FPS, fit, counter hidden");
        Map<String, String> old = new HashMap<>(Map.of("port.fps_limit", "45", "port.stretch_mode", "fill", "port.show_fps", "1"));
        PortDisplayOptions inherited = new PortDisplayOptions(none, old::get);
        check(env(inherited, true).equals(Map.of("ME1_FPS_LIMIT", "45", "ME1_STRETCH_MODE", "fill", "ME1_SHOW_FPS", "1")), "three supported legacy values reach the runtime");
        check(!env(inherited, false).containsKey("ME1_SHOW_FPS"), "no HUD for the windowless runner");
        PortDisplayOptions saved = new PortDisplayOptions(Map.of("ds3FpsLimit", 0, "ds3Scale", "stretch", "ds3ShowFps", false),
                id -> { throw new AssertionError("legacy lookup with saved preference: " + id); });
        check(env(saved, true).equals(Map.of("ME1_FPS_LIMIT", "0", "ME1_STRETCH_MODE", "stretch")), "explicit unlimited, stretch and false override old values");
        check(env(new PortDisplayOptions(Map.of("ds3ShowFps", false), old::get), true).equals(Map.of("ME1_FPS_LIMIT", "45", "ME1_STRETCH_MODE", "fill")),
                "each preference overrides independently");
        old.put("port.fps_limit", "0");
        old.put("port.stretch_mode", "invalid");
        check(env(new PortDisplayOptions(none, old::get), true).equals(Map.of("ME1_FPS_LIMIT", "0", "ME1_STRETCH_MODE", "fit", "ME1_SHOW_FPS", "1")),
                "legacy unlimited preserved, invalid scale falls back to fit");
        old.put("port.fps_limit", "9");
        check(new PortDisplayOptions(none, old::get).fpsLimit == 10, "legacy positive cap keeps runtime clamping");
        old.put("port.fps_limit", "invalid");
        check(new PortDisplayOptions(none, old::get).fpsLimit == 30 && !PortDisplayOptions.validLimit(241), "invalid FPS keeps supported range and default");
    }
}
