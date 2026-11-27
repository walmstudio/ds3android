package com.ds3port.launcher;

import java.util.Map;
import java.util.function.Function;

/** The three display options that the DS3 Android runtime consumes. */
final class PortDisplayOptions {
    static final String FPS_LIMIT = "ds3FpsLimit", SCALE = "ds3Scale", SHOW_FPS = "ds3ShowFps";
    static final int DEFAULT_LIMIT = 30;
    enum Scale { FIT, FILL, STRETCH }

    final int fpsLimit;
    final Scale scale;
    final boolean showFps;

    PortDisplayOptions(Map<String, ?> prefs, Function<String, String> legacy) {
        Object limit = prefs.get(FPS_LIMIT);
        fpsLimit = limit instanceof Integer && validLimit((int) limit)
                ? (int) limit : legacyLimit(legacy.apply("port.fps_limit"));
        Scale savedScale = parseScale(prefs.get(SCALE));
        Scale oldScale = savedScale == null ? parseScale(legacy.apply("port.stretch_mode")) : savedScale;
        scale = oldScale == null ? Scale.FIT : oldScale;
        Object counter = prefs.get(SHOW_FPS);
        showFps = counter instanceof Boolean ? (boolean) counter : "1".equals(legacy.apply("port.show_fps"));
    }

    static boolean validLimit(int n) { return n == 0 || (n >= 10 && n <= 240); }

    private static int legacyLimit(String s) {
        if (s == null || !s.matches("[0-9]+")) return DEFAULT_LIMIT;
        try {
            int n = Integer.parseInt(s);
            return n == 0 ? 0 : Math.max(10, Math.min(240, n));
        } catch (NumberFormatException ignored) { return DEFAULT_LIMIT; }
    }

    private static Scale parseScale(Object value) {
        if ("fit".equals(value)) return Scale.FIT;
        if ("fill".equals(value)) return Scale.FILL;
        if ("stretch".equals(value)) return Scale.STRETCH;
        return null;
    }

    static String scaleValue(Scale scale) { return scale.name().toLowerCase(java.util.Locale.ROOT); }

    void putEnv(Map<String, String> env, boolean windowed) {
        env.put("ME1_FPS_LIMIT", Integer.toString(fpsLimit));
        env.put("ME1_STRETCH_MODE", scaleValue(scale));
        if (windowed && showFps) env.put("ME1_SHOW_FPS", "1");
    }
}
