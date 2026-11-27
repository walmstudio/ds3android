package com.ds3port.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.util.Collections;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Physical gamepad digital remap, same idea as WOLF PROJECT 0.8 ControlsStore.
 * Preferences keys are m_<button> with values auto|none|<target>. */
final class PadMap {
    static final String[] BUTTONS = {
            "a", "b", "x", "y", "lb", "rb", "lt", "rt",
            "back", "start", "up", "down", "left", "right", "l3", "r3"
    };
    static final String[] ACTIONS = {
            "auto", "none", "a", "b", "x", "y", "lb", "rb", "lt", "rt",
            "back", "start", "up", "down", "left", "right", "l3", "r3"
    };
    private static final String[] LABELS = {
            "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
            "Back", "Start", "↑", "↓", "←", "→", "L3", "R3"
    };
    private static final String[] ACTION_LABELS = {
            "Авто", "Нет", "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
            "Back", "Start", "↑", "↓", "←", "→", "L3", "R3"
    };

    static final class Applied {
        int buttons;
        float lt, rt;
    }

    private PadMap() {}

    static String normalize(String value) {
        if (value == null || value.isEmpty()) return "auto";
        String v = value.toLowerCase(Locale.ROOT);
        for (String a : ACTIONS) if (a.equals(v)) return a;
        return "auto";
    }

    static int bit(String name) {
        switch (normalize(name)) {
        case "a": return Pad.A;
        case "b": return Pad.B;
        case "x": return Pad.X;
        case "y": return Pad.Y;
        case "lb": return Pad.LB;
        case "rb": return Pad.RB;
        case "back": return Pad.BACK;
        case "start": return Pad.START;
        case "up": return Pad.UP;
        case "down": return Pad.DOWN;
        case "left": return Pad.LEFT;
        case "right": return Pad.RIGHT;
        case "l3": return Pad.LTHUMB;
        case "r3": return Pad.RTHUMB;
        default: return 0;
        }
    }

    static Map<String, String> fromPrefs(SharedPreferences p) {
        if (p == null) return Collections.emptyMap();
        Map<String, String> map = new HashMap<>();
        for (String b : BUTTONS) {
            String v = normalize(p.getString("m_" + b, "auto"));
            if (!"auto".equals(v)) map.put(b, v);
        }
        return map;
    }

    static Applied apply(int buttons, float lt, float rt, Map<String, String> map) {
        Applied a = new Applied();
        a.buttons = buttons;
        a.lt = lt;
        a.rt = rt;
        if (map == null || map.isEmpty()) return a;
        String ltAct = normalize(map.get("lt"));
        String rtAct = normalize(map.get("rt"));
        if (!"auto".equals(ltAct)) a.lt = 0;
        if (!"auto".equals(rtAct)) a.rt = 0;
        a.buttons = 0;
        for (String btn : BUTTONS) {
            boolean src;
            if ("lt".equals(btn)) src = lt > 0.5f;
            else if ("rt".equals(btn)) src = rt > 0.5f;
            else src = (buttons & bit(btn)) != 0;
            if (!src) continue;
            String act = normalize(map.get(btn));
            if ("none".equals(act)) continue;
            if ("auto".equals(act)) {
                if ("lt".equals(btn)) a.lt = lt;
                else if ("rt".equals(btn)) a.rt = rt;
                else a.buttons |= bit(btn);
            } else if ("lt".equals(act)) a.lt = 1;
            else if ("rt".equals(act)) a.rt = 1;
            else a.buttons |= bit(act);
        }
        return a;
    }

    static void show(final Activity a, final SharedPreferences p, final Runnable changed) {
        final float dp = a.getResources().getDisplayMetrics().density;
        final LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding((int) (20 * dp), (int) (8 * dp), (int) (20 * dp), (int) (8 * dp));
        TextView info = new TextView(a);
        info.setText(L.t("Назначения цифровых кнопок физического геймпада. «Авто» — как на Xbox, «Нет» — кнопка игнорируется. После паузы игры удержанная кнопка не залипает: нужно отпустить и нажать снова."));
        info.setTextSize(13);
        info.setTextColor(Color.GRAY);
        info.setPadding(0, 0, 0, (int) (8 * dp));
        box.addView(info);
        final Spinner[] spinners = new Spinner[BUTTONS.length];
        for (int i = 0; i < BUTTONS.length; i++) {
            LinearLayout row = new LinearLayout(a);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, (int) (4 * dp), 0, (int) (4 * dp));
            TextView lab = new TextView(a);
            lab.setText(LABELS[i]);
            lab.setTextSize(15);
            lab.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            Spinner sp = new Spinner(a);
            ArrayAdapter<String> ad = new ArrayAdapter<>(a, android.R.layout.simple_spinner_item, ACTION_LABELS);
            ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
            sp.setAdapter(ad);
            String cur = normalize(p.getString("m_" + BUTTONS[i], "auto"));
            for (int k = 0; k < ACTIONS.length; k++) if (ACTIONS[k].equals(cur)) { sp.setSelection(k); break; }
            spinners[i] = sp;
            row.addView(lab);
            row.addView(sp);
            box.addView(row);
        }
        Button reset = new Button(a);
        reset.setText(L.t("Сбросить назначения"));
        reset.setOnClickListener(v -> {
            for (Spinner sp : spinners) sp.setSelection(0);
        });
        box.addView(reset);
        ScrollView sv = new ScrollView(a);
        sv.addView(box);
        new AlertDialog.Builder(a)
                .setTitle(L.t("Назначение кнопок геймпада"))
                .setView(sv)
                .setNegativeButton(L.t("Отмена"), null)
                .setPositiveButton(L.t("Сохранить"), (d, w) -> {
                    SharedPreferences.Editor e = p.edit();
                    for (int i = 0; i < BUTTONS.length; i++) {
                        String act = ACTIONS[spinners[i].getSelectedItemPosition()];
                        if ("auto".equals(act)) e.remove("m_" + BUTTONS[i]);
                        else e.putString("m_" + BUTTONS[i], act);
                    }
                    e.apply();
                    if (changed != null) changed.run();
                })
                .show();
    }
}
