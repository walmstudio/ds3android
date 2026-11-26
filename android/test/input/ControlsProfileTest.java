package com.ds3port.launcher;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import java.io.File;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.Map;

/** Real profile parser, selection, and file round-trip; only framework storage is supplied by the host. */
final class ControlsProfileTest {
    private static void check(boolean ok, String label) { if (!ok) throw new AssertionError(label); }
    private static Context context(File dir) {
        Map<String, Object> values = new HashMap<>();
        SharedPreferences.Editor edit = (SharedPreferences.Editor) Proxy.newProxyInstance(
                SharedPreferences.class.getClassLoader(), new Class<?>[]{SharedPreferences.Editor.class}, (proxy, m, a) -> {
                    switch (m.getName()) {
                    case "putBoolean": case "putString": values.put((String) a[0], a[1]); return proxy;
                    case "remove": values.remove(a[0]); return proxy;
                    case "apply": return null;
                    default: throw new AssertionError(m);
                    }
                });
        SharedPreferences prefs = (SharedPreferences) Proxy.newProxyInstance(
                SharedPreferences.class.getClassLoader(), new Class<?>[]{SharedPreferences.class}, (proxy, m, a) -> {
                    switch (m.getName()) {
                    case "getBoolean": case "getString": return values.getOrDefault(a[0], a[1]);
                    case "contains": return values.containsKey(a[0]);
                    case "edit": return edit;
                    default: throw new AssertionError(m);
                    }
                });
        return new Context() {
            final AssetManager assets = new AssetManager(new File("android/assets"));
            @Override public AssetManager getAssets() { return assets; }
            @Override public File getFilesDir() { return dir; }
            @Override public SharedPreferences getSharedPreferences(String name, int mode) { return prefs; }
        };
    }
    public static void main(String[] args) throws Exception {
        File dir = new File(args[0]);
        Context ctx = context(new File(dir, "new"));
        SharedPreferences prefs = ControlsSettings.prefs(ctx);
        CtlProfile stock = CtlProfile.active(ctx);
        check(ControlsSettings.physicalXinput(prefs), "physical defaults to XInput");
        check(ControlsSettings.shown(prefs) && stock.strictXinput(), "new touch defaults to visible stock XInput");
        check(stock.els.size() == 15, "stock element count");
        for (CtlProfile.El el : stock.els) for (int i = 0; i < el.bind.length; i++) {
            int kind = el.res[i].kind;
            check("NONE".equals(el.bind[i]) ? kind == Bind.NONE : kind >= Bind.PAD && kind <= Bind.RSTICK,
                    "resolved stock binding: " + el.bind[i]);
        }
        stock.els.get(0).x = .42f;
        stock.els.get(1).hidden = true;
        check(CtlProfile.save(ctx, stock), "save stock edits");
        CtlProfile reloaded = CtlProfile.active(ctx);
        check(reloaded.els.get(0).x == .42f && reloaded.els.get(1).hidden && reloaded.strictXinput(), "reload stock edits");
        prefs.edit().putBoolean("xinput", false).apply();
        check(!ControlsSettings.physicalXinput(prefs) && CtlProfile.active(ctx).strictXinput(), "physical KB/M leaves stock touch unchanged");
        prefs.edit().putString("layout", "kbm").putBoolean("xinput", true).apply();
        CtlProfile kbm = CtlProfile.active(ctx);
        check(!kbm.strictXinput() && !ControlsSettings.legacyTouchXinput(prefs) && ControlsSettings.physicalXinput(prefs),
                "explicit touch KB/M leaves physical XInput unchanged");
        check(kbm.els.stream().anyMatch(e -> e.res[0].kind == Bind.KEY), "explicit touch KB/M has keyboard keys");
        assertArrows(kbm);
        CtlProfile importProfile = kbm.copy("User custom");
        check(CtlProfile.save(ctx, importProfile), "save edited user profile");
        prefs.edit().putString("layout", "custom").putString("profile", importProfile.file).apply();
        check(CtlProfile.active(ctx).file.equals(importProfile.file), "user profile remains selected");

        Context upgraded = context(new File(dir, "upgraded"));
        File controls = CtlProfile.dir(upgraded);
        java.nio.file.Files.copy(new File("android/assets/layouts/layout1.icp").toPath(), new File(controls, "layout1.icp").toPath());
        check("layout1.icp".equals(CtlProfile.active(upgraded).file), "implicit saved Gen45 layout preserved");
        CtlProfile old = CtlProfile.load(upgraded,"layout1.icp");
        old.els.get(0).x = .42f;
        check(CtlProfile.save(upgraded,old), "save old custom position");
        CtlProfile merged = CtlProfile.active(upgraded);
        assertArrows(merged);
        check(merged.els.get(0).x == .42f, "saved positions unchanged");
        check(CtlProfile.save(upgraded,merged), "save supplemented layout");
        assertArrows(CtlProfile.active(upgraded));
        merged.els.removeIf(e -> "ds3_menu_arrows".equals(e.tag));
        check(CtlProfile.save(upgraded,merged), "save intentional removal");
        check(CtlProfile.active(upgraded).els.stream().noneMatch(e -> "ds3_menu_arrows".equals(e.tag)), "intentional removal respected");
        System.out.println("profile defaults, resolved bindings, and persistence: PASS");
    }
    private static void assertArrows(CtlProfile paper) {
        java.util.List<CtlProfile.El> arrows = new java.util.ArrayList<>();
        for (CtlProfile.El el : paper.els) if ("ds3_menu_arrows".equals(el.tag)) arrows.add(el);
        check(arrows.size() == 1, "one menu d-pad in layout 1");
        check(java.util.Arrays.equals(arrows.get(0).bind, new String[]{"KEY_UP", "KEY_RIGHT", "KEY_DOWN", "KEY_LEFT"}),
                "keyboard directional arrows, not XInput");
    }
}
