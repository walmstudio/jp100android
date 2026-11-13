package com.me1port.launcher;

import android.Manifest;
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
import android.util.DisplayMetrics;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Locale;

/** Jurassic Park: The Game, episode 1 (JurassicPark100.exe): checks the files in /sdcard/JPTelltale and starts GameActivity. */
public class MainActivity extends Activity {
    static final String DIR = "JPTelltale";
    static final String EXE = "JurassicPark100.exe";
    static final long EXE_SIZE = 18051196L;
    static final String[] IMAGES = { "fmodex", "bink" };

    SharedPreferences prefs;
    TextView pathView, report;
    Button permBtn, playBtn;
    CheckBox fpsBox;
    EditText envEdit;
    File gameDir;
    boolean ready;

    int dp(float v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }
    static LinearLayout.LayoutParams full() { return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT); }
    static LinearLayout.LayoutParams third() { return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("jp100", MODE_PRIVATE);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        int p = dp(16);
        col.setPadding(p, p, p, p);
        TextView title = new TextView(this);
        title.setText("Jurassic Park: The Game");
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(title, full());
        TextView sub = new TextView(this);
        sub.setText("Эпизод 1 (JurassicPark100.exe) — порт для Android arm64");
        col.addView(sub, full());
        pathView = new TextView(this);
        pathView.setPadding(0, dp(12), 0, 0);
        col.addView(pathView, full());
        permBtn = new Button(this);
        permBtn.setOnClickListener(v -> askStorage(true));
        col.addView(permBtn, full());
        report = new TextView(this);
        report.setTypeface(Typeface.MONOSPACE);
        report.setTextSize(13);
        report.setTextIsSelectable(true);
        report.setPadding(0, dp(8), 0, dp(8));
        col.addView(report, full());
        fpsBox = new CheckBox(this);
        fpsBox.setText("Показывать FPS");
        fpsBox.setChecked(prefs.getBoolean("fps", false));
        col.addView(fpsBox, full());
        TextView gh = new TextView(this);
        gh.setText("Графический драйвер");
        gh.setTextSize(17); gh.setTypeface(Typeface.DEFAULT_BOLD); gh.setPadding(0, dp(12), 0, dp(4));
        col.addView(gh, full());
        gpuBox = new LinearLayout(this);
        gpuBox.setOrientation(LinearLayout.VERTICAL);
        col.addView(gpuBox, full());
        TextView gmh = new TextView(this);
        gmh.setText("Нагрузка (GEARMASS)");
        gmh.setTextSize(17); gmh.setTypeface(Typeface.DEFAULT_BOLD); gmh.setPadding(0, dp(12), 0, dp(4));
        col.addView(gmh, full());
        gmBox = new LinearLayout(this);
        gmBox.setOrientation(LinearLayout.VERTICAL);
        col.addView(gmBox, full());
        TextView el = new TextView(this);
        el.setText("Доп. переменные окружения (KEY=VALUE, по одной в строке):");
        col.addView(el, full());
        envEdit = new EditText(this);
        envEdit.setTypeface(Typeface.MONOSPACE);
        envEdit.setTextSize(13);
        envEdit.setMinLines(2);
        envEdit.setHint("например ME1_BINK=0");
        envEdit.setText(prefs.getString("env", ""));
        col.addView(envEdit, full());
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button check = new Button(this);
        check.setText("Проверить");
        playBtn = new Button(this);
        playBtn.setText("Играть");
        Button logBtn = new Button(this);
        logBtn.setText("Лог");
        row.addView(check, third());
        row.addView(playBtn, third());
        row.addView(logBtn, third());
        col.addView(row, full());
        check.setOnClickListener(v -> refresh());
        playBtn.setOnClickListener(v -> play());
        logBtn.setOnClickListener(v -> showLog());
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        refresh();
        buildGpu();
        buildGm();
        askStorage(false);
    }

    @Override protected void onResume() { super.onResume(); refresh(); buildGpu(); buildGm(); }
    @Override protected void onPause() { super.onPause(); savePrefs(); saveGm(); }
    void savePrefs() { prefs.edit().putBoolean("fps", fpsBox.isChecked()).putString("env", envEdit.getText().toString()).apply(); }
    void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_LONG).show(); }

    /* Environment.isExternalStorageManager() is API 30; we compile against API 29 */
    static boolean allFiles() {
        if (Build.VERSION.SDK_INT < 30) return false;
        try { return (Boolean) Environment.class.getMethod("isExternalStorageManager").invoke(null); }
        catch (Throwable t) { return false; }
    }
    boolean canRead() { return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED; }
    boolean storageOk() { return allFiles() || canRead(); }

    void askStorage(boolean user) {
        if (!canRead() && !allFiles()) {
            requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE }, 1);
            return;
        }
        if (Build.VERSION.SDK_INT >= 30 && !allFiles() && (user || Environment.getExternalStorageDirectory().list() == null)) {
            try { startActivity(new Intent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", Uri.parse("package:" + getPackageName()))); }
            catch (Throwable t) {
                try { startActivity(new Intent("android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION")); }
                catch (Throwable t2) { toast("Разрешите приложению доступ ко всем файлам в настройках"); }
            }
        }
    }

    @Override public void onRequestPermissionsResult(int rc, String[] perms, int[] res) {
        refresh();
        if (canRead()) askStorage(false);
    }

    void refresh() {
        boolean perm = storageOk();
        boolean needAll = Build.VERSION.SDK_INT >= 30 && !allFiles();
        permBtn.setVisibility(!perm || needAll ? View.VISIBLE : View.GONE);
        permBtn.setText(!perm ? "Разрешить доступ к памяти" : "Включить «Доступ ко всем файлам»");
        File root = new File(Environment.getExternalStorageDirectory(), DIR);
        gameDir = findGameDir(root);
        String rel = gameDir.equals(root) ? "" : "/" + gameDir.getName();
        pathView.setText("Папка игры: /sdcard/" + DIR + rel);
        StringBuilder sb = new StringBuilder();
        ready = check(root, gameDir, perm, needAll, sb);
        report.setText(sb.toString());
        playBtn.setEnabled(ready);
    }

    static File findGameDir(File root) {
        if (findCi(root, EXE) != null) return root;
        File[] l = root.listFiles();
        if (l != null) for (File d : l) if (d.isDirectory() && findCi(d, EXE) != null) return d;
        return root;
    }

    static File findCi(File dir, String name) {
        File f = new File(dir, name);
        if (f.isFile()) return f;
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase(name)) { File g = new File(dir, s); if (g.isFile()) return g; }
        return null;
    }

    static String mb(long n) { return String.format(Locale.ROOT, "%.1f МБ", n / 1048576.0); }

    boolean check(File root, File g, boolean perm, boolean needAll, StringBuilder sb) {
        if (!perm) sb.append("✗ нет разрешения на доступ к памяти\n");
        if (!root.isDirectory()) {
            sb.append("✗ папка /sdcard/").append(DIR).append(" не найдена\n\n")
              .append("Скопируйте в неё установленную игру (эпизод 1): ").append(EXE)
              .append(", fmodex.dll, binkw32.dll и папку Pack с архивами .ttarch.\n");
            if (needAll) sb.append("Если папка уже есть — включите «Доступ ко всем файлам».\n");
            return false;
        }
        if (root.list() == null) {
            sb.append("✗ нет доступа к /sdcard/").append(DIR).append(" — нажмите кнопку разрешения выше\n");
            return false;
        }
        boolean ok = perm;
        File exe = findCi(g, EXE);
        if (exe == null) {
            sb.append("✗ ").append(EXE).append(" не найден\n");
            if (needAll) sb.append("  (если файл на месте — включите «Доступ ко всем файлам»)\n");
            ok = false;
        } else if (exe.length() != EXE_SIZE) {
            sb.append("⚠ ").append(exe.getName()).append(": ").append(exe.length()).append(" байт, ожидается ").append(EXE_SIZE)
              .append(" — порт собран под другую версию exe, скорее всего не запустится\n");
        } else sb.append("✓ ").append(exe.getName()).append(" (").append(mb(exe.length())).append(")\n");
        for (String dll : new String[] { "fmodex.dll", "binkw32.dll" }) {
            File f = findCi(g, dll);
            if (f != null) sb.append("✓ ").append(f.getName()).append(" (").append(mb(f.length())).append(")\n");
            else sb.append("⚠ ").append(dll).append(" нет — будет использована копия из APK\n");
        }
        long[] tt = new long[2];
        String[] first = new String[1];
        scan(g, g, 0, tt, first);
        if (tt[0] == 0) {
            sb.append("✗ нет архивов .ttarch (данные игры, обычно в папке Pack)\n");
            ok = false;
        } else sb.append("✓ архивы .ttarch: ").append(tt[0]).append(" шт., ").append(mb(tt[1]))
              .append(first[0] != null ? " (" + first[0] + ")" : "").append("\n");
        sb.append("• steam_api.dll: ").append(findCi(g, "steam_api.dll") != null ? "есть" : "нет").append("\n");
        sb.append("• сохранения и лог: ").append(docsDir(g).getAbsolutePath()).append("\n");
        sb.append(ok ? "\nВсё на месте — можно запускать." : "\nИгра не запустится, пока есть пункты с ✗.");
        return ok;
    }

    static void scan(File base, File d, int depth, long[] tt, String[] first) {
        File[] l = d.listFiles();
        if (l == null) return;
        for (File f : l) {
            if (f.isDirectory()) { if (depth < 4) scan(base, f, depth + 1, tt, first); continue; }
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.endsWith(".ttarch") || n.endsWith(".ttarch2")) {
                tt[0]++;
                tt[1] += f.length();
                if (first[0] == null) {
                    String rp = d.getAbsolutePath().substring(base.getAbsolutePath().length());
                    first[0] = rp.isEmpty() ? "в корне папки" : "в " + rp.substring(1);
                }
            }
        }
    }

    File docsDir(File g) {
        File d = new File(g, "Documents");
        if (d.isDirectory() || d.mkdirs()) {
            File t = new File(d, ".jp_write_test");
            try { new FileOutputStream(t).close(); t.delete(); return d; } catch (Throwable e) { }
        }
        File e = getExternalFilesDir(null);
        if (e == null) e = getFilesDir();
        File d2 = new File(e, "Documents");
        d2.mkdirs();
        return d2;
    }

    void play() {
        savePrefs();
        refresh();
        if (!ready) { toast("Сначала исправьте пункты с ✗"); return; }
        final File g = gameDir, docs = docsDir(g), img = new File(getFilesDir(), "jpimg");
        playBtn.setEnabled(false);
        toast("Запуск…");
        new Thread(() -> {
            String err = null;
            try { extractImages(img); } catch (Throwable t) { err = t.toString(); }
            final String e = err;
            runOnUiThread(() -> {
                playBtn.setEnabled(true);
                if (e != null) toast("Ошибка распаковки: " + e);
                else prepareDriver(drv -> launch(g, docs, img, drv));
            });
        }).start();
    }

    /* the recompiled fmodex.dll / binkw32.dll images ship in the APK: unpack once per install/update */
    void extractImages(File img) throws Exception {
        String stamp = Long.toString(getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime);
        File sf = new File(img, ".stamp");
        if (stamp.equals(readSmall(sf))) return;
        byte[] buf = new byte[1 << 16];
        for (String m : IMAGES) {
            File dir = new File(img, m);
            dir.mkdirs();
            File out = new File(dir, "image.bin"), tmp = new File(dir, "image.tmp");
            try (InputStream in = getAssets().open("jpimg/" + m + "/image.bin"); OutputStream o = new FileOutputStream(tmp)) {
                int n;
                while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            }
            if (!tmp.renameTo(out)) throw new Exception("не удалось записать " + out);
        }
        try (OutputStream o = new FileOutputStream(sf)) { o.write(stamp.getBytes("UTF-8")); }
    }

    static String readSmall(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[64];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, "UTF-8") : "";
        } catch (Throwable t) { return null; }
    }

    /* ---------------- v16: GEARMASS (from me1port launcher "Нагрузка (GEARMASS)", runtime/gearmass.c) ---------------- */
    LinearLayout gmBox;
    static final String[] GM_MODES = { "auto", "eco", "balance", "perf", "max", "custom", "off" };
    static final String[] GM_LABELS = { "Авто — по замерам во время игры (по умолчанию)", "Эконом — меньше потоков, 30 кадров/с",
            "Баланс", "Производительность", "Максимум — все ядра, кадр вперёд для GPU", "Своя — параметры ниже", "Выключен (как до GEARMASS)" };
    /* custom-mode knobs: env, label, hint; empty field = by mode */
    static final String[][] GM_KNOBS = {
        { "ME1_GM_WORKERS", "Рабочие потоки (распаковка текстур, шейдеры)", "0–8" },
        { "ME1_GM_AHEAD", "Кадров вперёд для потока рендера", "0–2" },
        { "ME1_GM_SPIN", "Активное ожидание тяжёлых потоков", "0–2" },
        { "ME1_GM_PRIME", "Игровой поток на самое быстрое ядро", "0–1" },
        { "ME1_FPS_LIMIT", "Ограничение кадров/с (0 — без ограничения)", "0, 10–240" },
        { "ME1_GM_ANISO", "Анизотропия не ниже", "1–16" },
    };
    final EditText[] gmFields = new EditText[GM_KNOBS.length];
    CheckBox gmHud;
    void buildGm() {
        if (gmBox == null) return;
        gmBox.removeAllViews();
        final Spinner sp = new Spinner(this);
        ArrayAdapter<String> ad = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, GM_LABELS);
        ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(ad);
        int cur = java.util.Arrays.asList(GM_MODES).indexOf(prefs.getString("gm_mode", "auto"));
        sp.setSelection(cur < 0 ? 0 : cur, false);
        gmBox.addView(sp, full());
        gmBox.addView(note("GEARMASS распределяет работу порта по ядрам и следит за нагревом. «Авто» сам выбирает режим по замерам "
                + "(загрузка ядер и GPU, нагрев), без списков устройств. ME1_GEARMASS", Color.GRAY), full());
        final LinearLayout knobs = new LinearLayout(this);
        knobs.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < GM_KNOBS.length; i++) {
            TextView l = new TextView(this); l.setText(GM_KNOBS[i][1] + " — " + GM_KNOBS[i][0]); l.setTextSize(13);
            knobs.addView(l, full());
            EditText et = new EditText(this);
            et.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
            et.setHint(GM_KNOBS[i][2] + ", пусто — по режиму");
            et.setText(prefs.getString("gm_" + GM_KNOBS[i][0], ""));
            gmFields[i] = et;
            knobs.addView(et, full());
        }
        knobs.setVisibility("custom".equals(GM_MODES[sp.getSelectedItemPosition()]) ? View.VISIBLE : View.GONE);
        gmBox.addView(knobs, full());
        sp.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> a, View v, int pos, long l) {
                prefs.edit().putString("gm_mode", GM_MODES[pos]).apply();
                knobs.setVisibility("custom".equals(GM_MODES[pos]) ? View.VISIBLE : View.GONE);
            }
            @Override public void onNothingSelected(AdapterView<?> a) {}
        });
        gmHud = new CheckBox(this);
        gmHud.setText("Индикатор GEARMASS (режим, узкое место, ядра, GPU, нагрев — строка под FPS)");
        gmHud.setChecked(prefs.getBoolean("gm_hud", false));
        gmHud.setOnCheckedChangeListener((b, on) -> prefs.edit().putBoolean("gm_hud", on).apply());
        gmBox.addView(gmHud, full());
        String h = gmHint();
        if (!h.isEmpty()) gmBox.addView(note(h, Color.LTGRAY), full());
    }
    void saveGm() {
        if (gmFields[0] == null) return;
        SharedPreferences.Editor e = prefs.edit();
        for (int i = 0; i < GM_KNOBS.length; i++) if (gmFields[i] != null) e.putString("gm_" + GM_KNOBS[i][0], gmFields[i].getText().toString().trim());
        e.apply();
    }
    void gmEnv(ArrayList<String> env) {
        saveGm();
        String mode = prefs.getString("gm_mode", "auto");
        env.add("ME1_GEARMASS=" + mode);
        if (prefs.getBoolean("gm_hud", false)) env.add("ME1_GM_HUD=1");
        if ("custom".equals(mode)) for (String[] k : GM_KNOBS) {
            String v = prefs.getString("gm_" + k[0], "").trim();
            if (!v.isEmpty() && v.matches("[0-9]{1,4}")) env.add(k[0] + "=" + v);
        }
    }
    /** me1_gearmass_last.txt (gm_session_save, every 30 s) in the saves/log folder */
    String gmHint() {
        java.util.Map<String, String> m = new java.util.HashMap<>();
        File f;
        try { f = new File(docsDir(gpuGameDir()), "me1_gearmass_last.txt"); } catch (Throwable t) { return ""; }
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            for (String line; (line = r.readLine()) != null;) { int eq = line.indexOf('='); if (eq > 0) m.put(line.substring(0, eq), line.substring(eq + 1)); }
        } catch (Exception e) { return ""; }
        try {
            String msg = "Последний сеанс GEARMASS: " + m.getOrDefault("fps_avg", "?") + " FPS / " + m.getOrDefault("fps_limit", "?")
                + "\nРежимы (% игрового времени) ECO / BALANCE / PERF / MAX: " + m.getOrDefault("level_eco", "?") + " / " + m.getOrDefault("level_balance", "?")
                + " / " + m.getOrDefault("level_perf", "?") + " / " + m.getOrDefault("level_max", "?") + "%"
                + "\nСцены (% сеанса) GPU / CPU / WAIT / VIDEO: " + m.getOrDefault("class_gpu", "?") + " / " + m.getOrDefault("class_cpu", "?")
                + " / " + m.getOrDefault("class_wait", "?") + " / " + m.getOrDefault("class_video", "?") + "%"
                + "\nНеудачных снижений: " + m.getOrDefault("fails", "?");
            double heat = Double.parseDouble(m.getOrDefault("heat_max", "-1"));
            if (heat >= 0) msg += " · Нагрев: " + (int) heat + "%";
            if (Double.parseDouble(m.getOrDefault("thermal_partial_seconds", "0")) > 0) msg += "\nДанные датчиков были неполными; нагрев мог быть выше.";
            double gpu = Double.parseDouble(m.getOrDefault("class_gpu", "0")), cpu = Double.parseDouble(m.getOrDefault("class_cpu", "0"));
            if (heat >= 90) msg += "\nВысокий нагрев: снизьте настройки или дайте устройству остыть.";
            else if (gpu > cpu && gpu >= 30) msg += "\nЧасто ограничивает GPU: попробуйте снизить разрешение или эффекты.";
            else if (cpu >= 30) msg += "\nЧасто ограничивает CPU: проверьте фоновые приложения.";
            return msg;
        } catch (Exception e) { return ""; }
    }

    /* ---------------- v15: graphics driver (from me1port MainActivity v0.9.8/v0.10) ---------------- */
    LinearLayout gpuBox;
    DeviceInfo dev;
    List<GpuDrivers.Driver> drivers = new ArrayList<>();
    File gpuGameDir() { return gameDir != null ? gameDir : new File(Environment.getExternalStorageDirectory(), DIR); }
    TextView note(String s, int color) {
        TextView t = new TextView(this);
        t.setText(s); t.setTextSize(12); t.setTextColor(color); t.setPadding(0, dp(4), 0, dp(4));
        return t;
    }
    void buildGpu() {
        if (gpuBox == null) return;
        final LinearLayout box = gpuBox; box.removeAllViews();
        if (dev == null) { try { dev = DeviceInfo.get(this, prefs); } catch (Throwable t) { dev = null; } }
        final String gpu = dev != null && dev.gpu != null ? dev.gpu : "";
        final File gd = gpuGameDir();
        try { drivers = GpuDrivers.scan(gd); } catch (Throwable t) { drivers = new ArrayList<>(); }
        final List<String> ids = new ArrayList<>(), labels = new ArrayList<>();
        ids.add(""); labels.add("Системный OpenGL ES (по умолчанию)");
        ids.add("vulkan"); labels.add("Системный Vulkan — свой рендерер D3D9→Vulkan");
        ids.add("zink"); labels.add("Системный Vulkan + Mesa Zink (эксперимент)");
        for (GpuDrivers.Driver d : drivers) {
            ids.add("pkg:" + d.id);
            labels.add(d.label() + (d.error != null ? " ✗" : GpuDrivers.compat(d, gpu, Build.VERSION.SDK_INT, Build.VERSION.RELEASE).isEmpty() ? "" : " ⚠"));
        }
        String cur = prefs.getString("gpu_driver", "");
        if (ids.indexOf(cur) < 0) { ids.add(cur); labels.add(cur.substring(cur.indexOf(':') + 1) + " — нет в папке (будет системный)"); }
        final Spinner sp = new Spinner(this);
        ArrayAdapter<String> ad = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(ad); sp.setSelection(ids.indexOf(cur), false);
        box.addView(sp, full());
        final TextView info = note("", Color.GRAY), warn = note("", Color.rgb(0xF0, 0xB0, 0x40));
        final Switch zk = new Switch(this);
        zk.setText("Через Mesa Zink (OpenGL ES → Vulkan) вместо своего рендерера"); zk.setTextSize(14);
        zk.setChecked("zink".equals(prefs.getString("drv_renderer", "vulkan")));
        zk.setOnCheckedChangeListener((b, on) -> prefs.edit().putString("drv_renderer", on ? "zink" : "vulkan").apply());
        box.addView(warn, full()); box.addView(zk, full()); box.addView(info, full());
        final Runnable show = () -> {
            String id = prefs.getString("gpu_driver", ""), w = "", i;
            GpuDrivers.Driver d = driver(id);
            zk.setVisibility(id.startsWith("pkg:") && d != null && d.error == null ? View.VISIBLE : View.GONE);
            if (id.isEmpty()) i = "Драйвер устройства через OpenGL ES — проверенный вариант.";
            else if (id.equals("vulkan")) i = "Собственный рендерер из этого порта: команды Direct3D 9 идут сразу в Vulkan, без OpenGL ES и Zink. "
                    + "Нужен Vulkan 1.0 (Mali-G52 подходит) и libme1spv.so в APK; шейдеры собираются в SPIR-V и кэшируются. "
                    + "Если Vulkan не запустится — будет системный OpenGL ES (строки «[vk]» в логе).";
            else if (id.equals("zink")) i = "Mesa Zink переводит OpenGL ES в Vulkan системного драйвера (нужна сборка с --gpu). Кадры через CPU — медленнее; для проверки артефактов.";
            else if (d == null) { w = "Пакета нет в " + GpuDrivers.folder(gd).getAbsolutePath() + " — игра запустится с системным OpenGL ES."; i = ""; }
            else {
                if (d.error != null) w = "✗ Пакет не подходит: " + d.error;
                else w = GpuDrivers.compat(d, gpu, Build.VERSION.SDK_INT, Build.VERSION.RELEASE);
                i = d.details() + "\nПо умолчанию — свой рендерер D3D9→Vulkan; с переключателем — Mesa Zink. Если драйвер не загрузится, игра сама перейдёт на OpenGL ES.";
            }
            warn.setText(w); warn.setVisibility(w.isEmpty() ? View.GONE : View.VISIBLE); info.setText(i);
        };
        sp.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> a, View v, int pos, long l) {
                if (!ids.get(pos).equals(prefs.getString("gpu_driver", ""))) { prefs.edit().putString("gpu_driver", ids.get(pos)).apply(); show.run(); }
            }
            @Override public void onNothingSelected(AdapterView<?> a) {}
        });
        show.run();
        String gpuLine = gpu.isEmpty() ? "" : "GPU: " + gpu + ". ";
        box.addView(note(gpuLine + "Свои драйверы: .zip в формате AdrenoTools (meta.json + .so) или папка/.so в " + GpuDrivers.folder(gd).getAbsolutePath()
                + "/. Turnip и драйверы Qualcomm — только Adreno, драйверы Mali — только Mali.", Color.GRAY), full());
        Button rb = new Button(this); rb.setText("Обновить список драйверов");
        rb.setOnClickListener(v -> { buildGpu(); toast("Драйверов в папке: " + drivers.size()); });
        box.addView(rb, full());
    }
    GpuDrivers.Driver driver(String id) {
        for (GpuDrivers.Driver d : drivers) if (("pkg:" + d.id).equals(id)) return d;
        return null;
    }
    interface Go { void run(Map<String, String> drvEnv); }
    /** environment for the selected driver; custom packages are unpacked to app storage first (Android loads code only from there) */
    void prepareDriver(final Go go) {
        final String id = prefs.getString("gpu_driver", "");
        final Map<String, String> env = new LinkedHashMap<>();
        if (id.isEmpty()) { go.run(env); return; }
        final File glc = new File(getFilesDir(), "gl_cache");
        if (id.equals("zink") || id.equals("vulkan")) {
            File c = new File(glc, id); c.mkdirs();
            env.put("ME1_GPU_DRIVER", id); env.put("ME1_GL_CACHE", c.getAbsolutePath()); go.run(env); return;
        }
        try { drivers = GpuDrivers.scan(gpuGameDir()); } catch (Throwable t) { drivers = new ArrayList<>(); }
        final GpuDrivers.Driver d = driver(id);
        if (d == null || d.error != null) {
            toast((d == null ? "Драйвер " + id.substring(4) + " не найден" : "Драйвер не подходит: " + d.error) + " — запуск с системным OpenGL ES");
            go.run(new LinkedHashMap<>()); return;
        }
        final Runnable install = () -> {
            final AlertDialog dlg = new AlertDialog.Builder(this).setTitle("Драйвер").setMessage("Подготовка " + d.label() + "…").setCancelable(false).show();
            new Thread(() -> {
                String err = null;
                try {
                    File base = new File(getFilesDir(), "gpu_drivers");
                    File lib = GpuDrivers.install(base, d);
                    GpuDrivers.cleanup(base, drivers);
                    boolean zink = "zink".equals(prefs.getString("drv_renderer", "vulkan"));
                    File c = new File(glc, "drv_" + d.id + (zink ? "_zink" : "")); c.mkdirs();
                    env.put("ME1_GPU_DRIVER", lib.getAbsolutePath()); env.put("ME1_GPU_DRIVER_NAME", d.label()); env.put("ME1_GL_CACHE", c.getAbsolutePath());
                    if (zink) env.put("ME1_GPU_ZINK", "1");
                } catch (Exception e) { err = e.getMessage(); }
                final String fe = err;
                runOnUiThread(() -> {
                    dlg.dismiss();
                    if (fe == null) { go.run(env); return; }
                    new AlertDialog.Builder(this).setTitle("Драйвер не распакован").setMessage(fe + "\n\nЗапустить с системным OpenGL ES?")
                            .setPositiveButton("Запустить", (x, y) -> go.run(new LinkedHashMap<>())).setNegativeButton("Отмена", null).show();
                });
            }).start();
        };
        String gpu = dev != null && dev.gpu != null ? dev.gpu : "";
        String w = GpuDrivers.compat(d, gpu, Build.VERSION.SDK_INT, Build.VERSION.RELEASE);
        if (w.isEmpty()) { install.run(); return; }
        new AlertDialog.Builder(this).setTitle(d.label()).setMessage(w + "\n\nЕсли драйвер не загрузится, игра сама запустится с системным OpenGL ES.")
                .setPositiveButton("Всё равно попробовать", (x, y) -> install.run())
                .setNeutralButton("Системный OpenGL ES", (x, y) -> go.run(new LinkedHashMap<>()))
                .setNegativeButton("Отмена", null).show();
    }

    void launch(File g, File docs, File img, Map<String, String> drv) {
        DisplayMetrics dm = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(dm);
        int w = Math.max(dm.widthPixels, dm.heightPixels), h = Math.min(dm.widthPixels, dm.heightPixels);
        File cache = new File(getFilesDir(), "gl_cache");
        cache.mkdirs();
        ArrayList<String> env = new ArrayList<>();
        env.add("ME1_SCREEN=" + w + "x" + h);
        if (!drv.containsKey("ME1_GL_CACHE")) env.add("ME1_GL_CACHE=" + cache.getAbsolutePath());
        for (Map.Entry<String, String> e : drv.entrySet()) env.add(e.getKey() + "=" + e.getValue());   /* v15: ME1_GPU_DRIVER etc. */
        gmEnv(env);   /* v16: ME1_GEARMASS, ME1_GM_HUD, ME1_GM_* (the user lines below still override) */
        env.add("ME1_LIST_MISSING=1");
        if (ControlsSettings.prefs(this).getBoolean("xinput", false)) env.add("ME1_XINPUT=1");   /* "xinput" controls profile: the virtual DirectInput gamepad */
        if (fpsBox.isChecked()) env.add("ME1_SHOW_FPS=1");
        for (String line : envEdit.getText().toString().split("[\\r\\n]+")) {
            String s = line.trim();
            if (s.isEmpty() || s.startsWith("#") || s.indexOf('=') <= 0) continue;
            env.add(s);
        }
        Intent it = new Intent(this, GameActivity.class);
        it.putExtra("args", new String[] { g.getAbsolutePath(), docs.getAbsolutePath(), img.getAbsolutePath() });
        it.putExtra("jp100", true);
        it.putExtra("env", env.toArray(new String[0]));
        it.putExtra("log", new File(docs, "jp100_run.log").getAbsolutePath());
        startActivity(it);
    }

    void showLog() {
        File g = gameDir != null ? gameDir : new File(Environment.getExternalStorageDirectory(), DIR);
        File f = new File(docsDir(g), "jp100_run.log");
        String text = f.isFile() ? f.getAbsolutePath() + "\n\n" + tail(f, 48 * 1024) : "Лога пока нет:\n" + f.getAbsolutePath();
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTypeface(Typeface.MONOSPACE);
        tv.setTextSize(11);
        tv.setTextIsSelectable(true);
        int p = dp(12);
        tv.setPadding(p, p, p, p);
        final ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        new AlertDialog.Builder(this).setTitle("Лог запуска").setView(sv).setPositiveButton("OK", null).show();
        sv.post(() -> sv.fullScroll(View.FOCUS_DOWN));
    }

    static String tail(File f, int max) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            long len = r.length(), off = Math.max(0, len - max);
            byte[] b = new byte[(int) (len - off)];
            r.seek(off);
            r.readFully(b);
            return (off > 0 ? "…\n" : "") + new String(b, "UTF-8");
        } catch (Throwable t) { return t.toString(); }
    }
}
