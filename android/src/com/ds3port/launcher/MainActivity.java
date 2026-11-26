package com.ds3port.launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.InputType;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Consumer;

/** Settings for the Android launcher and port, not the Windows game's configuration. */
public class MainActivity extends Activity {
    private static final int ACCENT = Color.rgb(0x4F, 0xA3, 0xE0);
    private static final int WARN = Color.rgb(0xF0, 0xB0, 0x40);
    private static final int REQ_STORAGE = 1;
    private static final int REQ_VULKAN_ICD = 419;
    private static final String READ = "android.permission.READ_EXTERNAL_STORAGE";
    private static final String WRITE = "android.permission.WRITE_EXTERNAL_STORAGE";
    private static final String APP_ALL_FILES = "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION";
    private static final String ALL_FILES = "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION";
    private File sdRoot;
    private GameLocator loc;
    private SharedPreferences prefs;
    // Serialize private ZIP staging across launcher instances; a refresh can cancel its own read-only scan.
    private static final ExecutorService DRIVER_IO = Executors.newSingleThreadExecutor();
    private Future<?> driverScan;
    private LinearLayout gpuBox;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = L.prefs(this);
        L.init(this);
        if (!hasStorage() && !prefs.getBoolean("asked", false)) requestStorage();
    }

    @Override protected void onResume() {
        super.onResume();
        refresh();
    }

    @Override protected void onDestroy() {
        if (driverScan != null) driverScan.cancel(true);
        super.onDestroy();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        super.onRequestPermissionsResult(code, perms, res);
        if (code == REQ_STORAGE) refresh();
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        if (req == REQ_VULKAN_ICD) {
            if (res == RESULT_OK && data != null && data.getData() != null) importVulkanIcd(data.getData());
            return;
        }
        if (!CtlProfile.onResult(this, req, res, data)) super.onActivityResult(req, res, data);
    }

    private boolean hasStorage() {
        return checkSelfPermission(READ) == PackageManager.PERMISSION_GRANTED
                || hasAllFilesAccess();
    }

    private static boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30) return false;
        try {
            // The launcher also compiles against android-29/android.jar.
            return Boolean.TRUE.equals(Environment.class.getMethod("isExternalStorageManager").invoke(null));
        } catch (ReflectiveOperationException | SecurityException ignored) { return false; }
    }

    private void requestStorage() {
        prefs.edit().putBoolean("asked", true).apply();
        requestPermissions(new String[] { READ, WRITE }, REQ_STORAGE);
    }

    private void locate() {
        sdRoot = Environment.getExternalStorageDirectory();
        List<File> roots = new ArrayList<>();
        roots.add(sdRoot);
        for (File f : getExternalFilesDirs(null)) {
            if (f == null) continue;
            String p = f.getAbsolutePath();
            int i = p.indexOf("/Android/data/");
            if (i > 0) {
                File root = new File(p.substring(0, i));
                if (!roots.contains(root)) roots.add(root);
            }
        }
        File priv = getExternalFilesDir(null);
        if (priv == null) priv = getFilesDir();
        loc = GameLocator.locate(roots, priv, prefs.getString("gameDir", null));
    }

    private void refresh() {
        locate();
        setContentView(buildUi());
    }

    private void chooseLanguage() {
        new AlertDialog.Builder(this).setTitle(L.t("Язык интерфейса"))
                .setSingleChoiceItems(L.NAMES, L.cur(), (d, i) -> {
                    d.dismiss();
                    L.set(this, i);
                    refresh();
                }).setNegativeButton(L.t("Отмена"), null).show();
    }

    private String gameLocale() {
        return "ru_RU".equals(prefs.getString("gameLocale", "en_EN")) ? "ru_RU" : "en_EN";
    }

    private void chooseGameLanguage() {
        new AlertDialog.Builder(this).setTitle(L.t("Язык игры"))
                .setSingleChoiceItems(new String[]{"Русский", "English"}, "ru_RU".equals(gameLocale()) ? 0 : 1,
                        (d, i) -> {
                            prefs.edit().putString("gameLocale", i == 0 ? "ru_RU" : "en_EN").apply();
                            d.dismiss();
                            refresh();
                        }).setNegativeButton(L.t("Отмена"), null).show();
    }

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(12), dp(16), dp(24));
        root.addView(text(L.t("Dead Space 3 — порт для Android"), 22, Color.WHITE, true));
        Button play = button(L.t("Запустить игру"), v -> startGame(false));
        play.setTextSize(20);
        play.setMinHeight(dp(60));
        play.setBackgroundTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        play.setEnabled(loc.found);
        root.addView(play);

        root.addView(section(L.t("Файлы игры")));
        root.addView(text(L.t("Папка игры") + (loc.manual ? L.t(" (выбрана вручную)") : "") + ":\n" + pretty(loc.gameDir), 14, Color.WHITE, false));
        root.addView(text(compatibilityMessage(),
                13, loc.found ? Color.rgb(0x8B, 0xD3, 0x6B) : WARN, false));
        if (loc.legacyPrivate) root.addView(text(L.t("Папка приложения будет удалена при его удалении."), 13, WARN, false));
        if (!hasStorage()) {
            root.addView(button(L.t("Разрешить доступ к файлам"), v -> {
                if (shouldShowRequestPermissionRationale(READ) || !prefs.getBoolean("asked", false)) requestStorage();
                else startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, packageUri()));
            }));
        }
        if (Build.VERSION.SDK_INT >= 30 && !hasAllFilesAccess() && (!loc.found || sdRoot.list() == null)) {
            root.addView(text(L.t("Если общая папка не видна, разрешите приложению доступ ко всем файлам в настройках Android."), 13, WARN, false));
            root.addView(button(L.t("Открыть настройки доступа"), v -> {
                Intent access = new Intent(APP_ALL_FILES, packageUri());
                try { startActivity(access); }
                catch (android.content.ActivityNotFoundException ignored) {
                    startActivity(new Intent(ALL_FILES));
                }
            }));
        }
        root.addView(button(L.t("Изменить папку"), v -> chooseFolder()));
        root.addView(button(L.t("Проверить"), v -> showCheck()));

        root.addView(section(L.t("Настройки лаунчера и порта")));
        root.addView(button(L.t("Язык интерфейса") + ": " + L.NAMES[L.cur()], v -> chooseLanguage()));
        root.addView(button(L.t("Язык игры") + ": " + ("ru_RU".equals(gameLocale()) ? "Русский" : "English"), v -> chooseGameLanguage()));
        PortDisplayOptions display = displayOptions();
        String limit = display.fpsLimit == 0 ? L.t("Без ограничения") : Integer.toString(display.fpsLimit);
        root.addView(button(L.t("Лимит FPS") + ": " + limit, v -> chooseFpsLimit(display.fpsLimit)));
        String[] scaleLabels = { L.t("Вписать (с пропорциями)"), L.t("Заполнить (обрезать края)"), L.t("Растянуть (без пропорций)") };
        root.addView(button(L.t("Масштабирование изображения") + ": " + scaleLabels[display.scale.ordinal()], v ->
                new AlertDialog.Builder(this).setTitle(L.t("Масштабирование изображения"))
                    .setSingleChoiceItems(scaleLabels, display.scale.ordinal(), (d, i) -> {
                        prefs.edit().putString(PortDisplayOptions.SCALE,
                                PortDisplayOptions.scaleValue(PortDisplayOptions.Scale.values()[i])).apply();
                        d.dismiss();
                        refresh();
                    }).setNegativeButton(L.t("Отмена"), null).show()));
        Switch fps = new Switch(this);
        fps.setText(L.t("Счётчик FPS в окне игры"));
        fps.setChecked(display.showFps);
        fps.setOnCheckedChangeListener((b, on) -> prefs.edit().putBoolean(PortDisplayOptions.SHOW_FPS, on).apply());
        root.addView(fps);
        root.addView(button("GEARMASS: " + gearmassLabel(Ds3GearmassOptions.Option.MODE,
                new Ds3GearmassOptions(prefs.getAll()).value(Ds3GearmassOptions.Option.MODE)), v -> showGearmass()));
        root.addView(button(L.t("Экранное управление — настройка"), v -> ControlsSettings.show(this, null, null)));
        root.addView(text(L.t("В окне игры кнопка ⊕ показывает экранные кнопки. Эти настройки относятся к оболочке; полная поддержка управления в игре пока не подтверждена."), 13, Color.LTGRAY, false));

        root.addView(section(L.t("Настройки игры")));
        root.addView(text("Game settings are managed in Dead Space 3. Opt-in GEARMASS can lower the user resolution between sessions.", 13, Color.LTGRAY, false));

        root.addView(section(L.t("Графический драйвер")));
        gpuBox = new LinearLayout(this);
        gpuBox.setOrientation(LinearLayout.VERTICAL);
        root.addView(gpuBox);
        refreshGpu();

        root.addView(section(L.t("Диагностика")));
        root.addView(text(pretty(loc.docsDir), 13, Color.LTGRAY, false));
        root.addView(button(L.t("Лог последнего запуска"), v -> showLastLog()));
        Button test = button(L.t("Диагностический запуск без окна"), v -> startGame(true));
        test.setEnabled(loc.found);
        root.addView(test);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.rgb(0x10, 0x14, 0x1A));
        sv.addView(root);
        return sv;
    }

    private Uri packageUri() { return Uri.parse("package:" + getPackageName()); }

    private String gearmassLabel(Ds3GearmassOptions.Option option, String value) {
        if (option == Ds3GearmassOptions.Option.MODE && value.isEmpty()) return "Default (№45 conservative)";
        return option == Ds3GearmassOptions.Option.THERMAL ? value + "%" : value;
    }

    private void showGearmass() {
        Ds3GearmassOptions options = new Ds3GearmassOptions(prefs.getAll());
        LinearLayout fields = new LinearLayout(this);
        fields.setOrientation(LinearLayout.VERTICAL);
        fields.setPadding(20, 12, 20, 12);
        fields.addView(text("Applies on next launch. In AUTO with a fixed FPS cap, auto-resolution can step down from 1080p to 900p to 720p if a matching GPU-bound session report for the same renderer and driver was saved in the last 24 hours. Change the size manually in-game; turning this off does not restore the old size.", 13, Color.LTGRAY, false));
        String[] labels = {"Mode", "Automatic resolution (AUTO only)", "GEARMASS HUD", "Thermal threshold (AUTO only)",
                "Helper workers (not CPU cores)", "Render ahead", "Spin", "Prime core", "Anisotropy"};
        Ds3GearmassOptions.Option[] all = Ds3GearmassOptions.Option.values();
        List<View> inputs = new ArrayList<>();
        Spinner mode = null;
        for (Ds3GearmassOptions.Option o : all) {
            String value = options.value(o);
            if (o.toggle()) {
                Switch sw = new Switch(this);
                sw.setText(labels[o.ordinal()]);
                sw.setChecked(value.equals("1"));
                fields.addView(sw);
                inputs.add(sw);
            } else {
                fields.addView(text(labels[o.ordinal()], 14, Color.WHITE, true));
                Spinner sp = new Spinner(this);
                List<String> names = new ArrayList<>();
                for (String choice : o.choices) names.add(gearmassLabel(o, choice));
                ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, names);
                adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
                sp.setAdapter(adapter);
                sp.setSelection(names.indexOf(gearmassLabel(o, value)), false);
                fields.addView(sp);
                inputs.add(sp);
                if (o == Ds3GearmassOptions.Option.MODE) mode = sp;
            }
        }
        mode.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View item, int pos, long id) {
                String selected = Ds3GearmassOptions.Option.MODE.choices[pos];
                for (int i = Ds3GearmassOptions.Option.WORKERS.ordinal(); i < inputs.size(); ++i)
                    inputs.get(i).setEnabled(selected.equals("custom"));
                inputs.get(Ds3GearmassOptions.Option.AUTORES.ordinal()).setEnabled(selected.equals("auto"));
                inputs.get(Ds3GearmassOptions.Option.THERMAL.ordinal()).setEnabled(selected.equals("auto"));
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });
        ScrollView scroll = new ScrollView(this);
        scroll.addView(fields);
        new AlertDialog.Builder(this).setTitle("GEARMASS").setView(scroll)
                .setNegativeButton(android.R.string.cancel, null)
                .setNeutralButton("Reset all", (d, n) -> {
                    SharedPreferences.Editor edit = prefs.edit();
                    for (Ds3GearmassOptions.Option o : all) edit.remove(o.key);
                    edit.apply();
                    refresh();
                }).setPositiveButton(android.R.string.ok, (d, n) -> {
                    SharedPreferences.Editor edit = prefs.edit();
                    for (Ds3GearmassOptions.Option o : all) {
                        View input = inputs.get(o.ordinal());
                        if (!input.isEnabled()) continue;
                        if (o.toggle()) {
                            boolean on = ((Switch) input).isChecked();
                            if (options.changed(o, on ? "1" : "0")) edit.putBoolean(o.key, on);
                        }
                        else {
                            int pos = ((Spinner) input).getSelectedItemPosition();
                            if (pos < 0) continue;
                            String selected = o.choices[pos];
                            if (!options.changed(o, selected)) continue;
                            if (selected.isEmpty()) edit.remove(o.key);
                            else edit.putString(o.key, selected);
                        }
                    }
                    edit.apply();
                    refresh();
                }).show();
    }

    private PortDisplayOptions displayOptions() {
        String docs = loc.docsDir.getAbsolutePath();
        return new PortDisplayOptions(prefs.getAll(), id -> {
            // Read-only fallback for the three allow-listed options, never a legacy settings load or save.
            try { return Native.portOption(docs, id); }
            catch (LinkageError ignored) { return null; }
        });
    }

    private void chooseFpsLimit(int current) {
        EditText value = new EditText(this);
        value.setInputType(InputType.TYPE_CLASS_NUMBER);
        value.setText(Integer.toString(current));
        value.setSelectAllOnFocus(true);
        String help = L.t("Введите 10–240 FPS или 0, чтобы снять лимит.");
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle(L.t("Лимит FPS"))
                .setMessage(help).setView(value).setPositiveButton("OK", null)
                .setNegativeButton(L.t("Отмена"), null).create();
        dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            int n;
            try { n = Integer.parseInt(value.getText().toString()); }
            catch (NumberFormatException ignored) { n = -1; }
            if (!PortDisplayOptions.validLimit(n)) { value.setError(help); return; }
            prefs.edit().putInt(PortDisplayOptions.FPS_LIMIT, n).apply();
            dialog.dismiss();
            refresh();
        }));
        dialog.show();
    }

    private String pretty(File f) {
        String p = f.getAbsolutePath(), r = sdRoot.getAbsolutePath();
        return p.equals(r) || p.startsWith(r + "/") ? "/sdcard" + p.substring(r.length()) : p;
    }

    private void chooseFolder() {
        EditText path = new EditText(this);
        path.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        path.setText(pretty(loc.gameDir));
        path.setSelectAllOnFocus(true);
        new AlertDialog.Builder(this).setTitle(L.t("Папка игры"))
                .setMessage(L.t("Введите абсолютный путь к папке с deadspace3.exe. «Авто» включает автоматический поиск."))
                .setView(path).setPositiveButton(L.t("Выбрать"), (d, w) -> {
                    String p = path.getText().toString().trim();
                    if (!p.startsWith("/")) { toast(L.t("Нужен абсолютный путь к папке.")); return; }
                    if (p.equals("/sdcard") || p.startsWith("/sdcard/")) p = sdRoot.getAbsolutePath() + p.substring(7);
                    prefs.edit().putString("gameDir", p).apply();
                    refresh();
                    if (!loc.found) showCheckResult();
                }).setNeutralButton(L.t("Авто"), (d, w) -> {
                    prefs.edit().remove("gameDir").apply();
                    refresh();
                }).setNegativeButton(L.t("Отмена"), null).show();
    }

    private File icdDir() { return new File(getFilesDir(), "ds3-vulkan-icd"); }
    private File zipDir() { return new File(getFilesDir(), "ds3-vulkan-zip"); }

    private void refreshGpu() {
        if (driverScan != null) driverScan.cancel(true);
        LinearLayout box = gpuBox;
        File game = loc.gameDir;
        File privateCache = getCacheDir();
        renderGpu(new ArrayList<>(), true);
        driverScan = DRIVER_IO.submit(() -> {
            List<GpuDrivers.Driver> drivers = GpuDrivers.scan(game, privateCache);
            if (!Thread.currentThread().isInterrupted()) runOnUiThread(() -> {
                if (!isDestroyed() && gpuBox == box && loc.gameDir.equals(game)) renderGpu(drivers, false);
            });
        });
    }

    private void renderGpu(List<GpuDrivers.Driver> drivers, boolean scanning) {
        gpuBox.removeAllViews();
        String mode = prefs.getString(Ds3VideoOptions.MODE, "gles");
        String selectedId = prefs.getString(Ds3VideoOptions.ZIP_ID, null);
        String selectedName = prefs.getString(Ds3VideoOptions.ZIP_NAME, "ZIP");
        String cachedGpu = prefs.getString("gl_" + Build.FINGERPRINT.hashCode(), "");
        int sep = cachedGpu.indexOf('\t');
        final String gpu = sep < 0 ? "" : cachedGpu.substring(0, sep);
        List<String> labels = new ArrayList<>(), modes = new ArrayList<>(), details = new ArrayList<>(), warnings = new ArrayList<>();
        List<GpuDrivers.Driver> packages = new ArrayList<>();
        labels.add(L.t("Системный OpenGL ES (по умолчанию)")); modes.add("gles"); packages.add(null);
        details.add(L.t("Проверенный системный драйвер. Рекомендуемый вариант.")); warnings.add("");
        labels.add(L.t("Системный Vulkan (экспериментально)")); modes.add("vulkan"); packages.add(null);
        details.add(L.t("Экспериментальный рендерер Direct3D 9 → Vulkan с системным драйвером. Фактический backend указан в логе запуска.")); warnings.add("");
        int current = "vulkan".equals(mode) ? 1 : 0;
        boolean matchedZip = false;
        for (GpuDrivers.Driver driver : drivers) {
            String warning = driver.error != null ? driver.error : GpuDrivers.compat(driver, gpu, Build.VERSION.SDK_INT, Build.VERSION.RELEASE);
            if ("zip".equals(mode) && selectedId != null && selectedId.equals(driver.id) && selectedName.equals(driver.src.getName())) {
                current = labels.size(); matchedZip = true;
            }
            labels.add("ZIP · " + driver.label() + (driver.error != null ? " ✕" : warning.isEmpty() ? "" : " ⚠"));
            modes.add("zip"); packages.add(driver);
            details.add(driver.details() + "\n" + L.t("Только standalone Android ARM64 Vulkan ICD. Совместимость с устройством не подтверждена; окончательная проверка — при запуске.")
                    + (gpu.isEmpty() ? "\n" + L.t("GPU устройства неизвестен; сравнение семейств GPU недоступно.") : ""));
            warnings.add(warning);
        }
        if ("zip".equals(mode) && !matchedZip) {
            current = labels.size();
            labels.add("ZIP · " + selectedName);
            modes.add("missing"); packages.add(null); details.add("");
            warnings.add(scanning ? L.t("Чтение списка драйверов…") : L.t("Выбранный ZIP отсутствует, изменён или недоступен в списке. Другой драйвер не будет выбран автоматически."));
        }
        if ("custom".equals(mode) || prefs.contains(Ds3VideoOptions.ICD)) {
            if ("custom".equals(mode)) current = labels.size();
            labels.add(L.t("Vulkan · ранее импортированный standalone ICD (.so)")); modes.add("custom"); packages.add(null);
            String problem = "";
            try { Ds3VideoOptions.putEnv(new LinkedHashMap<>(), "custom", prefs.getString(Ds3VideoOptions.ICD, null), icdDir()); }
            catch (IOException e) { problem = e.getMessage(); }
            details.add(L.t("Локальный импорт .so — дополнительный вариант без ZIP.")); warnings.add(problem);
        }
        final int active = current;
        gpuBox.addView(text(L.t("Выбрано для запуска: ") + labels.get(active), 14, Color.WHITE, true));
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        spinner.setSelection(active, false);
        gpuBox.addView(spinner);
        TextView info = text("", 13, Color.LTGRAY, false), warning = text("", 13, WARN, false);
        gpuBox.addView(info);
        gpuBox.addView(warning);
        Button apply = button(L.t("Выбрать показанный драйвер"), v -> {
            int pos = spinner.getSelectedItemPosition();
            if (pos < 0) return;
            String next = modes.get(pos);
            GpuDrivers.Driver driver = packages.get(pos);
            if ("zip".equals(next)) {
                if (driver.error == null && driver.minApi <= Build.VERSION.SDK_INT) confirmZip(driver, warnings.get(pos), warning);
            } else if (!"missing".equals(next) && warnings.get(pos).isEmpty()) {
                prefs.edit().putString(Ds3VideoOptions.MODE, next).apply();
                renderGpu(drivers, scanning);
            }
        });
        gpuBox.addView(apply);
        Consumer<Integer> preview = pos -> {
            info.setText(details.get(pos));
            warning.setText(warnings.get(pos));
            warning.setVisibility(warnings.get(pos).isEmpty() ? View.GONE : View.VISIBLE);
            GpuDrivers.Driver driver = packages.get(pos);
            apply.setEnabled(!"missing".equals(modes.get(pos)) && (driver == null
                    ? pos != active && warnings.get(pos).isEmpty() : driver.error == null && driver.minApi <= Build.VERSION.SDK_INT));
        };
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> view, View item, int pos, long id) { preview.accept(pos); }
            @Override public void onNothingSelected(AdapterView<?> view) { apply.setEnabled(false); }
        });
        preview.accept(active);
        gpuBox.addView(text(L.t("Поместите локальные ZIP (meta.json + ICD .so) в:") + "\n" + pretty(GpuDrivers.folder(loc.gameDir))
                + "\n" + L.t("Папку нужно создать вручную. Пакеты, требующие Android HAL или специальных загрузчиков, не поддерживаются."), 12, Color.LTGRAY, false));
        Button reload = button(scanning ? L.t("Чтение списка драйверов…") : L.t("Обновить список драйверов"), v -> refreshGpu());
        reload.setEnabled(!scanning);
        gpuBox.addView(reload);
        gpuBox.addView(button(L.t("Дополнительно: импортировать локальный ICD (.so)"), v -> chooseStandalone()));
    }

    private void confirmZip(GpuDrivers.Driver driver, String warning, TextView status) {
        final File base = zipDir();
        final String keep = prefs.getString(Ds3VideoOptions.ZIP_ID, null);
        new AlertDialog.Builder(this).setTitle(L.t("Доверие к нативному драйверу"))
                .setMessage(driver.details() + "\n\n" + L.t("Драйвер выполняет нативный код с правами приложения при запуске. Продолжайте только для доверенного пакета, предназначенного для вашего GPU.")
                        + (warning.isEmpty() ? "" : "\n\n" + warning))
                .setNegativeButton(L.t("Отмена"), null).setPositiveButton(L.t("Доверяю: проверить и выбрать"), (dialog, which) ->
                    driverWork(L.t("Проверка ZIP-драйвера…"), () -> {
                        GpuDrivers.prune(base, keep);
                        return GpuDrivers.install(base, driver);
                    }, staged -> {
                        prefs.edit().putString(Ds3VideoOptions.MODE, "zip").putString(Ds3VideoOptions.ZIP_ID, driver.id)
                                .putString(Ds3VideoOptions.ZIP_NAME, driver.src.getName()).apply();
                        refreshGpu();
                    }, error -> {
                        String message = L.t("ZIP не выбран: ") + error;
                        status.setText(message);
                        status.setVisibility(View.VISIBLE);
                        showText(L.t("Не удалось выбрать ZIP-драйвер"), message);
                    })).show();
    }

    private <T> void driverWork(String message, Callable<T> task, Consumer<T> done, Consumer<String> error) {
        AlertDialog progress = new AlertDialog.Builder(this).setMessage(message).setCancelable(false).show();
        DRIVER_IO.submit(() -> {
            try {
                T result = task.call();
                runOnUiThread(() -> { if (!isDestroyed()) { progress.dismiss(); done.accept(result); } });
            } catch (Exception e) {
                runOnUiThread(() -> { if (!isDestroyed()) { progress.dismiss(); error.accept(e.toString()); } });
            }
        });
    }

    private void chooseStandalone() {
        new AlertDialog.Builder(this).setTitle(L.t("Доверие к нативному драйверу"))
                .setMessage(L.t("Драйвер выполняет нативный код с правами приложения. Импортируйте только доверенный standalone ARM64 Vulkan ICD для своего GPU. HAL-драйверы не поддерживаются."))
                .setNegativeButton(L.t("Отмена"), null).setPositiveButton(L.t("Выбрать локальный файл"), (confirm, which) -> {
                    Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                            .setType("*/*").putExtra(Intent.EXTRA_LOCAL_ONLY, true);
                    try { startActivityForResult(pick, REQ_VULKAN_ICD); }
                    catch (android.content.ActivityNotFoundException e) { toast(L.t("Системный выбор файла недоступен")); }
                }).show();
    }

    private void importVulkanIcd(Uri uri) {
        try {
            if (!"content".equals(uri.getScheme())) throw new IOException("Требуется локальный документ");
            String name = "";
            try (android.database.Cursor c = getContentResolver().query(uri,
                    new String[]{android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (c != null && c.moveToFirst() && !c.isNull(0)) name = c.getString(0);
            }
            String supplied = GpuDrivers.family(name), actual = GpuDrivers.family(DeviceInfo.get(this, prefs).gpu);
            if (!supplied.isEmpty() && !actual.isEmpty() && !supplied.equals(actual))
                throw new IOException("Драйвер «" + name + "» не подходит для " + GpuDrivers.familyName(actual));
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) throw new IOException("Не удалось открыть локальный документ");
                String id = GpuDrivers.importIcd(icdDir(), in);
                prefs.edit().putString(Ds3VideoOptions.ICD, id).putString(Ds3VideoOptions.MODE, "custom").apply();
            }
            refresh();
        } catch (IOException | RuntimeException e) {
            showText("Не удалось импортировать Vulkan ICD", e.getMessage() + "\nВыберите системный Vulkan или GLES.");
        }
    }

    private void showCheck() {
        refresh();
        showCheckResult();
    }

    private void showCheckResult() {
        showText(L.t("Проверка папки игры"), pretty(loc.gameDir) + "\n\n" + compatibilityMessage());
    }

    private String compatibilityMessage() {
        switch (loc.compatibility.status) {
            case SUPPORTED: return L.t("✓ Файлы Dead Space 3 найдены");
            case UNSUPPORTED_EXE: return L.t("Unsupported Dead Space 3 executable (this port requires the dcc26a3d build).");
            case UNREADABLE_EXE: return L.t("Could not read deadspace3.exe.");
            default: return L.t("Отсутствуют, пусты или недоступны для чтения:") + "\n" + String.join("\n", loc.compatibility.missing);
        }
    }

    private void startGame(boolean test) {
        locate();
        if (!loc.found) { showCheck(); return; }
        if (!GameLocator.safeDocuments(loc.gameDir, loc.docsDir)) {
            showText(L.t("Ошибка запуска"), L.t("Unsafe Documents or drive_c folder.") + "\n" + pretty(loc.docsDir));
            return;
        }
        File exe = new File(getApplicationInfo().nativeLibraryDir, "libds3run.so");
        if (test && !exe.isFile()) { toast(L.t("В этой сборке APK нет libds3run.so")); return; }
        String mode = prefs.getString(Ds3VideoOptions.MODE, "gles");
        Map<String, String> video = new LinkedHashMap<>();
        if ("zip".equals(mode)) {
            File game = loc.gameDir, base = zipDir();
            String name = prefs.getString(Ds3VideoOptions.ZIP_NAME, null), id = prefs.getString(Ds3VideoOptions.ZIP_ID, null);
            driverWork(L.t("Проверка выбранного ZIP…"), () -> {
                Ds3VideoOptions.putZipEnv(video, game, base, name, id);
                return video;
            }, ready -> {
                if (loc.gameDir.equals(game)) launchGame(test, exe, ready);
            }, error -> showText(L.t("Ошибка запуска"), error));
            return;
        }
        try { Ds3VideoOptions.putEnv(video, mode, prefs.getString(Ds3VideoOptions.ICD, null), icdDir()); }
        catch (IOException e) { showText(L.t("Ошибка запуска"), e.getMessage()); return; }
        launchGame(test, exe, video);
    }

    private void launchGame(boolean test, File exe, Map<String, String> video) {
        try {
            File image = stageImage();
            File log = RunDiagnostics.newLog(loc.docsDir);
            Map<String, String> env = gameEnv(image, RunDiagnostics.frameDir(loc.docsDir), test, video);
            if (test) {
                runExeTest(exe, log, env);
            } else {
                Intent it = new Intent(this, GameActivity.class);
                it.putExtra("args", new String[] { loc.gameDir.getAbsolutePath(), loc.docsDir.getAbsolutePath(), "-" });
                it.putExtra("env", env.entrySet().stream().map(e -> e.getKey() + "=" + e.getValue()).toArray(String[]::new));
                if (log != null) it.putExtra("log", log.getAbsolutePath());
                startActivity(it);
            }
        } catch (IOException e) {
            showText(L.t("Ошибка запуска"), e.toString());
        }
    }

    private File stageImage() throws IOException {
        File image = new File(getFilesDir(), "ds3-image.bin");
        File temporary = new File(getFilesDir(), "ds3-image.bin.tmp");
        try (InputStream in = getAssets().open("ds3-image.bin"); FileOutputStream out = new FileOutputStream(temporary)) {
            byte[] block = new byte[65536];
            int n;
            while ((n = in.read(block)) != -1) out.write(block, 0, n);
            out.getFD().sync();
        } catch (IOException e) {
            temporary.delete();
            throw new IOException("Private DS3 image unavailable; build private-dev-apk", e);
        }
        if (!temporary.renameTo(image)) throw new IOException("Cannot stage DS3 image");
        return image;
    }

    private Map<String, String> gameEnv(File image, File frames, boolean test, Map<String, String> video) throws IOException {
        Map<String, String> env = new LinkedHashMap<>();
        env.put("DS3_IMAGE", image.getAbsolutePath());
        env.putAll(video);
        env.put("EAGameLocale", gameLocale());
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(dm);
        env.put("ME1_SCREEN", Math.max(dm.widthPixels, dm.heightPixels) + "x" + Math.min(dm.widthPixels, dm.heightPixels));
        if (frames != null) {
            env.put("ME1_FRAME_DUMP", frames.getAbsolutePath());
            env.put("ME1_FRAME_DUMP_EVERY", test ? "300" : "1800");
            if (test) env.put("ME1_D3D_DUMP", new File(frames, "shaders").getAbsolutePath());
        }
        File cache = new File(loc.docsDir, "gl_cache");
        if (!cache.isDirectory() && !cache.mkdirs()) {
            cache = new File(getFilesDir(), "gl_cache"); // keep the old Android fallback intact
            cache.mkdirs();
        }
        env.put("ME1_GL_CACHE", cache.getAbsolutePath());
        new Ds3GearmassOptions(prefs.getAll()).putEnv(env, test);
        // Mode 2 lets actual physical and touch-pad owners report connection.
        // Mode 1 would advertise a gamepad even while using the KB/M layout.
        env.put("ME1_XINPUT", "2");
        displayOptions().putEnv(env, !test);
        if (test) {
            env.put("ME1_MAX_SECONDS", "90");
            env.put("ME1_MAX_FRAMES", "20000");
        }
        return env;
    }

    private void runExeTest(File exe, File log, Map<String, String> env) {
        String noLog = L.t("Лог не записывается: папка недоступна для записи.");
        TextView out = showText(L.t("Диагностический запуск без окна"), (log == null ? noLog : log.getAbsolutePath()) + "\n");
        File game = loc.gameDir, docs = loc.docsDir;
        new Thread(() -> {
            Thread watchdog = null;
            try (RunDiagnostics.LogWriter writer = new RunDiagnostics.LogWriter(log)) {
                if (log != null && !writer.available()) post(out, noLog);
                ProcessBuilder pb = new ProcessBuilder(exe.getAbsolutePath(), game.getAbsolutePath(), docs.getAbsolutePath(), "-");
                pb.redirectErrorStream(true).directory(getFilesDir());
                pb.environment().putAll(env);
                Process p = pb.start();
                watchdog = new Thread(() -> {
                    try { Thread.sleep(1800000); p.destroy(); }
                    catch (InterruptedException ignored) { }
                });
                watchdog.setDaemon(true);
                watchdog.start();
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        if (!writer.write(line)) post(out, noLog);
                        if (interesting(line)) post(out, shorten(line));
                    }
                }
                post(out, L.t("Код завершения:") + " " + p.waitFor());
            } catch (IOException | InterruptedException e) { post(out, e.toString()); }
            finally { if (watchdog != null) watchdog.interrupt(); }
        }).start();
    }

    private void showLastLog() {
        File log = RunDiagnostics.lastLog(loc.docsDir);
        if (log == null) {
            showText(L.t("Лог последнего запуска"), L.t("Нет лога: ") + pretty(loc.docsDir));
            return;
        }
        ArrayDeque<String> tail = new ArrayDeque<>();
        try (BufferedReader reader = Files.newBufferedReader(log.toPath(), StandardCharsets.UTF_8)) {
            String line;
            while ((line = reader.readLine()) != null) if (interesting(line)) {
                tail.add(shorten(line));
                if (tail.size() > 400) tail.removeFirst();
            }
        } catch (IOException e) { tail.add(L.t("Нет лога: ") + e.getMessage()); }
        TextView out = showText(L.t("Лог последнего запуска"), log.getAbsolutePath() + "\n\n" + String.join("\n", tail));
        ScrollView sv = (ScrollView) out.getParent();
        sv.post(() -> sv.fullScroll(View.FOCUS_DOWN));
    }

    private static boolean interesting(String line) {
        return line.startsWith("[ds3") || line.startsWith("[run]") || line.startsWith("[rt]") || line.startsWith("[exe]")
                || line.startsWith("[d3d") || line.startsWith("[gl]") || line.startsWith("[vk]") || line.startsWith("[bink]")
                || line.startsWith("[hb]") || line.startsWith("[log]") || line.startsWith("[dinput]") || line.startsWith("[ui]")
                || line.startsWith("[vk]")
                || line.startsWith("[al]") || line.startsWith("[mm]") || line.startsWith("[hle] MessageBox") || line.contains("distinct APIs called")
                || line.contains("FAULT") || line.contains("CRASH") || line.startsWith("  ") || line.startsWith("[hle] unimplemented") || line.startsWith("[hle] missing");
    }

    private static String shorten(String s) { return s.length() > 400 ? s.substring(0, 400) + " …" : s; }
    private void post(TextView t, String s) { runOnUiThread(() -> t.append(s + "\n")); }
    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }
    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
    private TextView text(String s, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private TextView section(String s) {
        TextView t = text(s, 16, ACCENT, true);
        t.setPadding(0, dp(18), 0, dp(4));
        return t;
    }

    private Button button(String s, View.OnClickListener l) {
        Button bt = new Button(this);
        bt.setText(s);
        bt.setAllCaps(false);
        bt.setOnClickListener(l);
        return bt;
    }

    private TextView showText(String title, String value) {
        TextView t = text(value, 12, Color.WHITE, false);
        t.setTypeface(Typeface.MONOSPACE);
        t.setTextIsSelectable(true);
        t.setPadding(dp(12), dp(8), dp(12), dp(8));
        ScrollView sv = new ScrollView(this);
        sv.addView(t);
        new AlertDialog.Builder(this).setTitle(title).setView(sv).setPositiveButton("OK", null).show();
        return t;
    }
}
