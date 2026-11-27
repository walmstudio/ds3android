package com.ds3port.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Locale;

/** Settings of the on-screen controls (v0.9.6), shared by the launcher and the game (long press on ⊕).
 * Stored in SharedPreferences "me1controls" (multi-process: the game runs in ":game"):
 * alpha (20..100 %), size (50..200 %), stick (50..200 %), look (camera sensitivity x10, 2..40), invertY, tapClick,
 * haptic, mirror (stick on the right), badge (shader compilation indicator), hidden (comma-separated button labels),
 * p_<label> / p_stick / p_padstick ("fx,fy": custom positions as fractions of the screen, set in the layout editor);
 * xinput (physical pad XInput, default true; off = emulates keyboard and mouse), rumble, rumblePhone;
 * v1.1.3: layout ("classic" | "skyrim": the on-screen Xbox-style pad of the Skyrim port - A/B/X/Y, bumpers, triggers,
 * Back/Start, a fixed stick driven from the whole left half of the screen, a phone-keyboard key; its own positions
 * s_<id> / s_stick and hidden ids "s:<id>"), nintendo (physical A/B and X/Y swapped),
 * m_<button> (physical digital remap, PadMap);
 * v1.2: layout "custom" (Winlator-style profiles, CustomControls / CtlProfile), profile (file of the active profile),
 * seeded_<asset> (a built-in profile was copied once - not again after the user deleted it). */
final class ControlsSettings {
    static final String PREFS = "me1controls";
    static final String[] BUTTONS = { "ЛКМ", "ПКМ", "E", "Пробел", "Shift", "Q", "G", "Tab", "1", "2", "3", "4", "M", "I", "J", "Esc" };
    static final String[] PAD_BUTTONS = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "↑", "↓", "←", "→", "L3", "R3" };
    static final String[] SKY_BUTTONS = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "kbd" };   /* ids of the "skyrim" layout */
    static final String STOCK = "ds3_xinput";
    static String layout(SharedPreferences p) {
        String value = p.getString("layout", p.contains("profile") ? "custom" : STOCK);
        if ("skyrim".equals(value)) return STOCK;
        if ("classic".equals(value)) return "kbm";
        return value;
    }
    static boolean sky(SharedPreferences p) { return "skyrim".equals(layout(p)); }
    static boolean custom(SharedPreferences p) { return "custom".equals(layout(p)); }
    static boolean shown(SharedPreferences p) { return p.getBoolean("shown", STOCK.equals(layout(p))); }
    static boolean physicalXinput(SharedPreferences p) { return p.getBoolean("xinput", true); }
    static boolean legacyTouchXinput(SharedPreferences p) { return !"kbm".equals(layout(p)) && p.getBoolean("xinput", false); }
    private ControlsSettings() {}

    @SuppressWarnings("deprecation")
    static SharedPreferences prefs(Context c) {
        SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_MULTI_PROCESS);
        if (!p.contains("layout") && !p.contains("profile")) {
            // Gen45 wrote out the implicit classic layout too. Keep it on upgrades, not on a fresh setup.
            java.io.File d = new java.io.File(c.getFilesDir(), "controls");
            if (new java.io.File(d, "layout1.icp").isFile() || new java.io.File(d, "layout1x.icp").isFile())
                p.edit().putString("layout", "classic").apply();
        }
        return p;
    }
    static HashSet<String> hidden(SharedPreferences p) {
        HashSet<String> h = new HashSet<>();
        for (String s : p.getString("hidden", "").split(",")) if (!s.isEmpty()) h.add(s);
        return h;
    }

    /** changed: called after every change (the overlay re-reads the settings); edit: starts the layout editor (game only) */
    static void show(final Activity a, final Runnable changed, final Runnable edit) {
        final SharedPreferences p = prefs(a);
        final float dp = a.getResources().getDisplayMetrics().density;
        final LinearLayout box = new LinearLayout(a); box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding((int) (20 * dp), (int) (8 * dp), (int) (20 * dp), (int) (8 * dp));
        final Runnable ch = () -> { if (changed != null) changed.run(); };
        /* v1.1.3: layout preset */
        TextView lh = new TextView(a); lh.setText(L.t("Раскладка экранных кнопок")); lh.setTextSize(15);
        box.addView(lh);
        final RadioGroup rg = new RadioGroup(a);
        final RadioButton r0 = new RadioButton(a), r2 = new RadioButton(a), stock = new RadioButton(a);
        stock.setText(L.t("Dead Space 3 — полный геймпад XInput")); stock.setId(View.generateViewId());
        r0.setText(L.t("Раскладка 1 — клавиши и кнопки мыши")); r0.setId(View.generateViewId());
        r2.setText(L.t("Раскладка 3 — своя: профили .icp как в Winlator; больше свободы, нужна настройка"));
        r2.setId(View.generateViewId());
        rg.addView(stock); rg.addView(r0); rg.addView(r2);
        final AlertDialog[] dlg = new AlertDialog[1];
        final Button profBtn = btn(a, L.t("Профили своей раскладки…"), v -> CtlDialogs.profiles(a, ch,
                edit == null ? null : () -> { if (dlg[0] != null) dlg[0].dismiss(); edit.run(); }));
        final TextView skyInfo = new TextView(a);
        skyInfo.setTextSize(12); skyInfo.setTextColor(Color.GRAY); skyInfo.setPadding(0, (int) (2 * dp), 0, (int) (6 * dp));
        final Runnable skyText = () -> {
            profBtn.setVisibility(custom(p) ? View.VISIBLE : View.GONE);
            if (custom(p)) {
                skyInfo.setVisibility(View.VISIBLE);
                CtlProfile active = CtlProfile.active(a);
                skyInfo.setText(L.t("Профиль: ") + active.name + (active.strictXinput()
                        ? L.t(". Только XInput, без мыши и клавиатуры.")
                        : L.t(". Свободное место экрана — камера, короткое касание — щелчок. Расставить и настроить элементы: «Профили своей раскладки» → «Редактировать раскладку на экране» (в игре).")));
                return;
            }
            skyInfo.setVisibility(View.GONE);
        };
        (STOCK.equals(layout(p)) ? stock : custom(p) ? r2 : r0).setChecked(true); skyText.run();
        rg.setOnCheckedChangeListener((g, id) -> {
            boolean cu = id == r2.getId();
            boolean full = id == stock.getId();
            SharedPreferences.Editor e = p.edit().putString("layout", full ? STOCK : cu ? "custom" : "kbm");
            e.putBoolean("shown", true);
            e.apply(); skyText.run(); ch.run();
        });
        box.addView(rg); box.addView(skyInfo); box.addView(profBtn);
        slider(a, box, p, L.t("Прозрачность кнопок"), "alpha", 20, 100, 100, v -> v + " %", ch);
        slider(a, box, p, L.t("Размер кнопок"), "size", 50, 200, 100, v -> v + " %", ch);
        slider(a, box, p, L.t("Размер стика"), "stick", 50, 200, 100, v -> v + " %", ch);
        slider(a, box, p, L.t("Чувствительность камеры"), "look", 2, 40, 12, v -> String.format(Locale.ROOT, "%.1f", v / 10.0), ch);
        toggle(a, box, p, L.t("Инвертировать камеру по вертикали"), "invertY", false, ch);
        toggle(a, box, p, L.t("Короткое касание свободного места = щелчок"), "tapClick", true, ch);
        toggle(a, box, p, L.t("Вибрация при нажатии"), "haptic", true, ch);
        toggle(a, box, p, L.t("Стик справа, кнопки слева (зеркально)"), "mirror", false, ch);
        toggle(a, box, p, L.t("Индикатор «компиляция шейдеров» в игре"), "badge", true, ch);
        /* v1.1.5: the game's mouse cursor (CursorView) */
        TextView ch0 = new TextView(a); ch0.setText(L.t("Курсор мыши")); ch0.setTextSize(15); ch0.setPadding(0, (int) (8 * dp), 0, 0);
        box.addView(ch0);
        final String[] curK = { "auto", "always", "off" };
        final String[] curL = { L.t("Когда игра его показывает (меню)"), L.t("Всегда"), L.t("Не показывать") };
        final RadioGroup cg = new RadioGroup(a);
        final String cur0 = p.getString("cursor", "auto");
        for (int i = 0; i < curK.length; i++) {
            RadioButton rb = new RadioButton(a); rb.setText(curL[i]); rb.setId(View.generateViewId()); rb.setTag(curK[i]);
            cg.addView(rb); if (curK[i].equals(cur0)) rb.setChecked(true);
        }
        if (cg.getCheckedRadioButtonId() == View.NO_ID) ((RadioButton) cg.getChildAt(0)).setChecked(true);
        cg.setOnCheckedChangeListener((g, id) -> {
            View rb = g.findViewById(id);
            if (rb != null) { p.edit().putString("cursor", (String) rb.getTag()).apply(); ch.run(); }
        });
        box.addView(cg);
        toggle(a, box, p, L.t("Передавать ввод физического геймпада через XInput вместо эмуляции клавиатуры и мыши"), "xinput", true, ch);
        toggle(a, box, p, L.t("Раскладка Nintendo на физическом геймпаде (A/B и X/Y поменяны местами)"), "nintendo", false, ch);
        box.addView(btn(a, L.t("Назначение кнопок физического геймпада…"), v -> PadMap.show(a, p, ch)));
        toggle(a, box, p, L.t("Вибрация геймпада (отдача, XInput)"), "rumble", true, ch);
        toggle(a, box, p, L.t("…на телефоне, если у геймпада нет вибромотора"), "rumblePhone", true, ch);
        LinearLayout row = new LinearLayout(a); row.setOrientation(LinearLayout.VERTICAL); row.setPadding(0, (int) (10 * dp), 0, 0);
        row.addView(btn(a, L.t("Какие кнопки показывать…"), v -> pickButtons(a, p, ch)));
        if (edit != null) row.addView(btn(a, L.t("Редактировать раскладку на экране"), v -> { if (dlg[0] != null) dlg[0].dismiss(); edit.run(); }));
        row.addView(btn(a, L.t("Сбросить расположение"), v -> {
            if (STOCK.equals(layout(p))) new java.io.File(CtlProfile.dir(a), CtlProfile.STOCK_LAYOUT).delete();
            else if (custom(p)) { CtlProfile current = CtlProfile.active(a); if (current.builtin) { new java.io.File(CtlProfile.dir(a), current.file).delete(); CtlProfile.seedOne(a, current.file); } }
            else { for (String f : new String[]{"layout1.icp", "layout1x.icp", "layout2.icp"}) new java.io.File(CtlProfile.dir(a), f).delete(); }
            SharedPreferences.Editor e = p.edit();
            for (String k : p.getAll().keySet()) if (k.startsWith("p_") || k.startsWith("s_") || k.equals("hidden")) e.remove(k);
            e.apply(); ch.run(); toast(a, L.t("Кнопки на стандартных местах"));
        }));
        box.addView(row);
        TextView info = new TextView(a);
        info.setText(edit != null ? L.t("Изменения применяются сразу.") : L.t("В игре: долгое нажатие на ⊕ открывает эти же настройки и режим ")
                + L.t("перестановки кнопок (перетаскивание пальцем)."));
        info.setTextSize(12); info.setTextColor(Color.GRAY); info.setPadding(0, (int) (8 * dp), 0, 0);
        box.addView(info);
        ScrollView sv = new ScrollView(a); sv.addView(box);
        dlg[0] = new AlertDialog.Builder(a).setTitle(L.t("Экранное управление")).setView(sv).setPositiveButton(L.t("Готово"), null).create();
        dlg[0].show();
    }

    interface Fmt { String f(int v); }
    private static void slider(Activity a, LinearLayout box, final SharedPreferences p, String label, final String key, final int min, int max,
                               int def, final Fmt fmt, final Runnable ch) {
        LinearLayout line = new LinearLayout(a); line.setOrientation(LinearLayout.HORIZONTAL);
        TextView l = new TextView(a); l.setText(label); l.setTextSize(15);
        final TextView val = new TextView(a); val.setTextSize(15); val.setTextColor(Color.rgb(0x4F, 0xA3, 0xE0));
        line.addView(l, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)); line.addView(val);
        SeekBar sb = new SeekBar(a); sb.setMax(max - min);
        int v0 = Math.max(min, Math.min(max, p.getInt(key, def)));
        sb.setProgress(v0 - min); val.setText(fmt.f(v0));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar b, int pr, boolean user) {
                if (!user) return;
                val.setText(fmt.f(pr + min)); p.edit().putInt(key, pr + min).apply(); ch.run();
            }
            @Override public void onStartTrackingTouch(SeekBar b) {}
            @Override public void onStopTrackingTouch(SeekBar b) {}
        });
        box.addView(line); box.addView(sb);
    }
    private static Switch toggle(Activity a, LinearLayout box, final SharedPreferences p, String label, final String key, boolean def, final Runnable ch) {
        Switch s = new Switch(a); s.setText(label); s.setTextSize(15); s.setChecked(p.getBoolean(key, def));
        s.setPadding(0, (int) (6 * a.getResources().getDisplayMetrics().density), 0, 0);
        s.setOnCheckedChangeListener((b, on) -> { p.edit().putBoolean(key, on).apply(); ch.run(); });
        box.addView(s);
        return s;
    }
    private static Button btn(Activity a, String t, android.view.View.OnClickListener l) {
        Button b = new Button(a); b.setText(t); b.setAllCaps(false); b.setOnClickListener(l); return b;
    }
    private static void pickButtons(Activity a, final SharedPreferences p, final Runnable ch) {
        final CtlProfile profile = CtlProfile.active(a);
        final String[] names = new String[profile.els.size()]; final boolean[] checked = new boolean[names.length];
        for (int i = 0; i < names.length; i++) { CtlProfile.El e = profile.els.get(i); String label = e.text;
            if (label.isEmpty()) { if (e.is("RANGE_BUTTON")) label = e.range; else if (e.is("STICK")) label = L.t("Стик"); else if (e.is("D_PAD")) label = L.t("Крестовина"); else label = Bind.of(e.bind[0]).label(); }
            names[i] = label; checked[i] = !e.hidden;
        }
        new AlertDialog.Builder(a).setTitle(L.t("Кнопки на экране")).setMultiChoiceItems(names, checked, (d,i,on) -> checked[i] = on)
            .setPositiveButton("OK", (d,w) -> { for (int i = 0; i < checked.length; i++) profile.els.get(i).hidden = !checked[i]; CtlProfile.save(a, profile); ch.run(); })
            .setNeutralButton(L.t("Все"), (d,w) -> { for (CtlProfile.El e : profile.els) e.hidden = false; CtlProfile.save(a, profile); ch.run(); })
            .setNegativeButton(L.t("Отмена"), null).show();
    }
    private static void pickButtonsLegacy(Activity a, final SharedPreferences p, final Runnable ch) {
        final HashSet<String> hid = hidden(p);
        final String[] ids, names;   /* ids as stored in "hidden"; the "skyrim" layout has its own ("s:A", ...) */
        if (sky(p)) {
            ids = new String[SKY_BUTTONS.length]; names = new String[SKY_BUTTONS.length];
            for (int i = 0; i < ids.length; i++) { ids[i] = "s:" + SKY_BUTTONS[i]; names[i] = "kbd".equals(SKY_BUTTONS[i]) ? L.t("⌨ Клавиатура телефона") : SKY_BUTTONS[i]; }
        } else { ids = p.getBoolean("xinput", false) ? PAD_BUTTONS : BUTTONS; names = L.arr(ids); }
        final boolean[] on = new boolean[ids.length];
        for (int i = 0; i < ids.length; i++) on[i] = !hid.contains(ids[i]);
        new AlertDialog.Builder(a).setTitle(L.t("Кнопки на экране")).setMultiChoiceItems(names, on, (d, i, c) -> on[i] = c)
                .setPositiveButton("OK", (d, w) -> {
                    ArrayList<String> h = others(hid, ids);
                    for (int i = 0; i < ids.length; i++) if (!on[i]) h.add(ids[i]);
                    p.edit().putString("hidden", String.join(",", h)).apply(); ch.run();
                })
                .setNeutralButton(L.t("Все"), (d, w) -> { p.edit().putString("hidden", String.join(",", others(hid, ids))).apply(); ch.run(); })
                .setNegativeButton(L.t("Отмена"), null).show();
    }
    /** hidden ids of the other layouts/profiles (kept as they are) */
    private static ArrayList<String> others(HashSet<String> hid, String[] ids) {
        ArrayList<String> h = new ArrayList<>();
        for (String s : hid) { boolean mine = false; for (String b : ids) if (b.equals(s)) mine = true; if (!mine) h.add(s); }
        return h;
    }
    private static void toast(Activity a, String s) { android.widget.Toast.makeText(a, s, android.widget.Toast.LENGTH_SHORT).show(); }
}
