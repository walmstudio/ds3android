package com.ds3port.launcher;

import java.util.Locale;
import java.util.Map;

/** Snapshot of the launcher's GEARMASS preferences, independent of video driver selection. */
final class Ds3GearmassOptions {
    enum Option {
        MODE("Gearmass", "ME1_GEARMASS", "", "", "auto", "eco", "balance", "perf", "max", "custom", "off"),
        AUTORES("GearmassAutoResolution", "ME1_GM_AUTORES", "0"),
        HUD("GearmassHud", "ME1_GM_HUD", "0"),
        THERMAL("GearmassThermal", "ME1_GM_THERMAL", "90", percentChoices()),
        WORKERS("GearmassWorkers", "ME1_GM_WORKERS", "auto", "auto", "0", "1", "2", "3", "4", "6", "8"),
        AHEAD("GearmassAhead", "ME1_GM_AHEAD", "1", "0", "1", "2"),
        SPIN("GearmassSpin", "ME1_GM_SPIN", "1", "0", "1", "2"),
        PRIME("GearmassPrime", "ME1_GM_PRIME", "0"),
        ANISO("GearmassAniso", "ME1_GM_ANISO", "1", "1", "2", "4", "8", "16");

        final String key, env, initial;
        final String[] choices;

        Option(String key, String env, String initial, String... choices) {
            this.key = key;
            this.env = env;
            this.initial = initial;
            this.choices = choices;
        }

        boolean toggle() { return choices.length == 0; }
    }

    private static String[] percentChoices() {
        String[] values = new String[41];
        for (int i = 0; i < values.length; ++i) values[i] = Integer.toString(60 + i);
        return values;
    }

    private final Map<String, ?> prefs;

    Ds3GearmassOptions(Map<String, ?> prefs) { this.prefs = prefs; }

    private String saved(Option option) {
        Object raw = prefs.get(option.key);
        if (option.toggle()) return raw instanceof Boolean ? ((Boolean) raw ? "1" : "0") : null;
        if (!(raw instanceof String) && (option == Option.MODE || !(raw instanceof Integer))) return null;
        String value = raw.toString();
        for (String choice : option.choices) if (value.equals(choice)) return value;
        return null;
    }

    String value(Option option) {
        String saved = saved(option);
        return saved != null ? saved : option.initial;
    }

    boolean changed(Option option, String selected) { return !value(option).equals(selected); }

    void putEnv(Map<String, String> env, boolean headless) {
        String mode = value(Option.MODE);
        for (Option option : Option.values()) {
            if (option.ordinal() >= Option.WORKERS.ordinal()) {
                // The non-numeric "auto" also suppresses the legacy ME1_CMD_THREADS alias.
                if (mode.equals("custom")) env.put(option.env, value(option));
                continue;
            }
            String val = saved(option);
            if (val == null || val.isEmpty()) continue;
            if (option == Option.THERMAL) val = String.format(Locale.ROOT, "%.2f", Integer.parseInt(val) / 100.0);
            env.put(option.env, val);
        }
        if (headless) env.put("ME1_GM_RES_SKIP", "1");
    }
}
