package com.ds3port.launcher;

import java.util.LinkedHashMap;
import java.util.Map;

public final class Ds3GearmassOptionsTest {
    private static void check(boolean ok, String text) {
        if (!ok) throw new AssertionError(text);
    }

    private static Map<String, String> env(Map<String, ?> prefs, boolean headless) {
        Map<String, String> env = new LinkedHashMap<>();
        new Ds3GearmassOptions(prefs).putEnv(env, headless);
        return env;
    }

    public static void main(String[] args) {
        check(env(Map.of(), false).isEmpty(), "missing mode must not select AUTO or write defaults");
        Map<String, Object> saved = new LinkedHashMap<>(Map.of(
                "Gearmass", "custom", "GearmassAutoResolution", true, "GearmassHud", true,
                "GearmassThermal", 80, "GearmassWorkers", "6", "GearmassAhead", 2,
                "GearmassSpin", "0", "GearmassPrime", true, "GearmassAniso", 16));
        check(env(saved, false).equals(Map.of("ME1_GEARMASS", "custom", "ME1_GM_AUTORES", "1",
                "ME1_GM_HUD", "1", "ME1_GM_THERMAL", "0.80", "ME1_GM_WORKERS", "6",
                "ME1_GM_AHEAD", "2", "ME1_GM_SPIN", "0", "ME1_GM_PRIME", "1", "ME1_GM_ANISO", "16")),
                "all nine saved settings round-trip into launch environment");
        Map<String, String> headless = new LinkedHashMap<>(env(saved, true));
        check("1".equals(headless.remove("ME1_GM_RES_SKIP")) && headless.equals(env(saved, false)),
                "headless gets the same policy plus telemetry skip");
        for (String mode : new String[]{"auto", "eco", "balance", "perf", "max", "off"}) {
            saved.put("Gearmass", mode);
            check(env(saved, false).equals(Map.of("ME1_GEARMASS", mode,
                    "ME1_GM_AUTORES", "1", "ME1_GM_HUD", "1", "ME1_GM_THERMAL", "0.80")),
                    mode + " does not export stale custom knobs");
        }
        check(env(Map.of("Gearmass", "custom", "GearmassWorkers", "5", "GearmassAhead", 9,
                "GearmassSpin", "bogus", "GearmassAniso", -1), false).equals(Map.of(
                        "ME1_GEARMASS", "custom", "ME1_GM_WORKERS", "auto", "ME1_GM_AHEAD", "1",
                        "ME1_GM_SPIN", "1", "ME1_GM_PRIME", "0", "ME1_GM_ANISO", "1")),
                "corrupt custom prefs fall back, workers auto is non-numeric");
        check(env(Map.of("Gearmass", 1, "GearmassThermal", "broken", "GearmassHud", false,
                "GearmassAutoResolution", false), false).equals(Map.of("ME1_GM_HUD", "0", "ME1_GM_AUTORES", "0")),
                "invalid values cannot select a mode, explicit false overrides env");
        check("1.00".equals(env(Map.of("GearmassThermal", "100"), false).get("ME1_GM_THERMAL")), "100 percent is 1.00");
        Ds3GearmassOptions untouched = new Ds3GearmassOptions(Map.of("Gearmass", "auto"));
        for (Ds3GearmassOptions.Option option : Ds3GearmassOptions.Option.values())
            check(!untouched.changed(option, untouched.value(option)), "OK alone must not materialize " + option);
        check(untouched.changed(Ds3GearmassOptions.Option.HUD, "1") &&
                untouched.changed(Ds3GearmassOptions.Option.THERMAL, "85") &&
                untouched.changed(Ds3GearmassOptions.Option.AUTORES, "1"), "real edits are written");
        Ds3GearmassOptions enabled = new Ds3GearmassOptions(Map.of("GearmassHud", true, "GearmassAutoResolution", true));
        check(enabled.changed(Ds3GearmassOptions.Option.HUD, "0") &&
                enabled.changed(Ds3GearmassOptions.Option.AUTORES, "0"), "real edits can turn saved options off");
        System.out.println("DS3 GEARMASS options: OK");
    }
}
