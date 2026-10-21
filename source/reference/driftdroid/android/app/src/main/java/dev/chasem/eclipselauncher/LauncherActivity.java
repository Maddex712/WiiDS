package dev.chasem.eclipselauncher;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.graphics.Color;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.*;

/** Android's document picker keeps ROMs and saves private without storage permissions. */
public class LauncherActivity extends Activity {
    private static final int DISC = 1, IMPORT_SAVE = 2, EXPORT_SAVE = 3, EXPORT_LOG = 4;
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private TextView status;
    private LinearLayout content;
    private boolean busy;
    private SharedPreferences prefs;
    private File root, disc, card, game;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        root = ThorStorage.root(this); disc = ThorStorage.disc(this);
        card = new File(root, "saves/card-a"); game = new File(root, "game");
        disc.getParentFile().mkdirs(); card.mkdirs(); game.mkdirs(); new File(game, "cache").mkdirs();
        prefs = getSharedPreferences("eclipse_settings", MODE_PRIVATE);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setBackgroundColor(Color.rgb(12, 26, 42));
        LinearLayout columns = new LinearLayout(this);
        columns.setOrientation(LinearLayout.HORIZONTAL);
        columns.setPadding(dp(8), dp(8), dp(8), 0);
        layout.addView(columns, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        TextView build = new TextView(this);
        build.setText(BuildInfo.summary()); build.setTextSize(13); build.setTextColor(Color.LTGRAY);
        build.setPadding(dp(20), dp(6), dp(20), dp(6));
        layout.addView(build);
        layout.setOnApplyWindowInsetsListener((view, insets) -> {
            android.graphics.Insets safe = android.os.Build.VERSION.SDK_INT >= 30
                ? insets.getInsets(android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout())
                : android.graphics.Insets.of(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            return insets;
        });
        setContentView(layout);
        content = panel(columns, "Super Mario Eclipse");
        label("Import your patched Eclipse 1.1.0 disc image, then play with your handheld's controls or a connected controller.", 14);
        status = label("", 16);
        button("Play", () -> {
            try {
                ThorStorage.validateDisc(disc);
                writeSettings();
                Intent launch = new Intent(this, GameActivity.class).putExtra("thor_library", true);
                if (BuildConfig.DEBUG && getIntent().hasExtra("testEnvironment"))
                    launch.putExtra("testEnvironment", getIntent().getStringArrayExtra("testEnvironment"));
                if (BuildConfig.DEBUG) launch.putExtra("gxSelftest", getIntent().getBooleanExtra("gxSelftest", false));
                startActivity(launch);
            }
            catch (IOException e) { message(e.getMessage()); }
        });
        button("Main Menu", () -> { if (ThorStorage.returnToLibrary(this)) finish(); });
        button("Import disc image", () -> pick(DISC));
        label("Saves and diagnostics", 18);
        button("Input test (controllers and keyboards)", () -> startActivity(new Intent(this, InputMonitorActivity.class)));
        button("Import save backup (.zip)", () -> pick(IMPORT_SAVE));
        button("Export save backup", () -> create(EXPORT_SAVE, "application/zip", "eclipse-save.zip"));
        button("Export game log", () -> create(EXPORT_LOG, "text/plain", "eclipse-game.log"));
        content = panel(columns, "Game settings");
        button("Controller configuration", () -> startActivity(new Intent(this, ControllerConfigActivity.class)));
        choice("Frame rate", "frame_rate", new String[]{"30", "60", "120"}, "60");
        choice("Resolution scale", "resolution", new String[]{"1", "2", "1080p", "3"}, "1080p");
        choice("Aspect ratio", "widescreen", new String[]{"off", "16:9"}, "16:9");
        choice("Volume", "volume", new String[]{"0", "25", "50", "75", "100"}, "100");
        toggle("Android performance hints", "performance_hints", false);
        toggle("Use performance CPU cores", "performance_cores", false);
        toggle("Request performance headroom", "performance_headroom", false);
        toggle("Match display refresh to game FPS", "match_display_rate", false);
        toggle("Show performance overlay", "performance_overlay", false);
        toggle("Log performance timings", "timing_log", false);
        label("Connect a Bluetooth or USB controller, or use the built-in controls on an Android handheld. Android Back returns to the launcher. Disc images and save backups are copied into private app storage; uninstalling removes them.", 15);
        refresh();
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private LinearLayout panel(LinearLayout columns, String title) {
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1);
        size.setMargins(dp(6), 0, dp(6), 0); columns.addView(card, size);
        android.graphics.drawable.GradientDrawable background = new android.graphics.drawable.GradientDrawable();
        background.setColor(Color.rgb(18, 36, 52)); background.setCornerRadius(dp(12));
        card.setBackground(background); card.setClipToOutline(true);
        TextView heading = new TextView(this); heading.setText(title); heading.setTextSize(22);
        heading.setTextColor(Color.WHITE); heading.setPadding(dp(16), dp(10), dp(16), 0); card.addView(heading);
        TextView hint = new TextView(this); hint.setText("Swipe up or down to scroll"); hint.setTextSize(12);
        hint.setTextColor(Color.LTGRAY); hint.setPadding(dp(16), dp(2), dp(16), dp(8)); card.addView(hint);
        ScrollView scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(true); scroll.setScrollbarFadingEnabled(false);
        scroll.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_INSET);
        android.graphics.drawable.GradientDrawable thumb = new android.graphics.drawable.GradientDrawable();
        thumb.setColor(Color.rgb(140, 170, 190)); thumb.setCornerRadius(dp(2)); thumb.setSize(dp(4), dp(24));
        scroll.setVerticalScrollbarThumbDrawable(thumb);
        card.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        LinearLayout panel = new LinearLayout(this); panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(16), 0, dp(16), dp(12)); scroll.addView(panel);
        return panel;
    }
    @Override protected void onResume() { super.onResume(); if (status != null && !busy) refresh(); }
    @Override protected void onDestroy() { io.shutdown(); super.onDestroy(); }
    private TextView label(String text, int size) {
        TextView v = new TextView(this); v.setText(text); v.setTextSize(size); v.setTextColor(Color.WHITE);
        v.setPadding(0, dp(3), 0, dp(4)); content.addView(v); return v;
    }
    private void button(String text, Runnable action) {
        Button b = new Button(this); b.setText(text); b.setAllCaps(false);
        b.setOnClickListener(v -> { if (!busy) action.run(); }); content.addView(b);
    }
    private void choice(String title, String key, String[] values, String fallback) {
        label(title, 16);
        Spinner s = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, values);
        s.setBackgroundColor(Color.rgb(40, 60, 80)); s.setAdapter(adapter);
        String value = prefs.getString(key, fallback);
        for (int i = 0; i < values.length; i++) if (values[i].equals(value)) s.setSelection(i);
        s.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onItemSelected(AdapterView<?> p, View v, int i, long id) { prefs.edit().putString(key, values[i]).apply(); }
            public void onNothingSelected(AdapterView<?> p) {}
        }); content.addView(s);
    }
    private void toggle(String title, String key, boolean fallback) {
        CheckBox box = new CheckBox(this);
        box.setText(title); box.setTextColor(Color.WHITE);
        box.setChecked(prefs.getBoolean(key, fallback));
        box.setOnCheckedChangeListener((view, checked) -> prefs.edit().putBoolean(key, checked).apply());
        content.addView(box);
    }
    private void writeSettings() throws IOException { prepareGame(this); }
    public static void prepareGame(android.content.Context context) throws IOException {
        File root = ThorStorage.root(context), game = new File(root, "game");
        new File(root, "disc").mkdirs(); new File(root, "saves/card-a").mkdirs();
        game.mkdirs(); new File(game, "cache").mkdirs();
        SharedPreferences prefs = context.getSharedPreferences("eclipse_settings", MODE_PRIVATE);
        // Thor defaults (owner request): 16:9, 1080 rendered lines, 60 FPS. Applied once over the
        // earlier 4:3 / 640x528 / 30 FPS values; later choices in Game settings are kept.
        if (prefs.getInt("thor_defaults", 0) < 1)
            prefs.edit().putString("frame_rate", "60").putString("resolution", "1080p")
                .putString("widescreen", "16:9").putInt("thor_defaults", 1).commit();
        ControllerProfiles.writeGameFile(context);
        // 1080p: the GameCube's 528-line frame scaled to the Thor's 1080 lines.
        String resolution = prefs.getString("resolution", "1080p");
        if ("1080p".equals(resolution)) resolution = "2.0455";
        String text = "frame_rate=" + prefs.getString("frame_rate", "60") + "\nresolution=" + resolution
            + "\nwidescreen=" + prefs.getString("widescreen", "16:9") + "\nvolume=" + prefs.getString("volume", "100")
            + "\nmsaa=0\nfullscreen=on\nvsync=on\nbutton_prompts=auto\n"
            + "SMS_ANDROID_PERFORMANCE_HINTS=" + (prefs.getBoolean("performance_hints", false) ? "1" : "0")
            + "\nSMS_ANDROID_PERFORMANCE_CORES=" + (prefs.getBoolean("performance_cores", false) ? "1" : "0")
            + "\nSMS_ANDROID_PERFORMANCE_HEADROOM=" + (prefs.getBoolean("performance_headroom", false) ? "1" : "0")
            + "\nSMS_TIMING_LOG=" + (prefs.getBoolean("timing_log", false) ? "1" : "0")
            + "\nSMS_OVERLAY=" + (prefs.getBoolean("performance_overlay", false) ? "1" : "0") + "\n";
        // Debug builds only: diagnostic lines (renderer switches) from game/settings-extra.txt.
        File extra = new File(game, "settings-extra.txt");
        if (BuildConfig.DEBUG && extra.isFile())
            text += new String(java.nio.file.Files.readAllBytes(extra.toPath()), java.nio.charset.StandardCharsets.UTF_8) + "\n";
        try (FileOutputStream out = new FileOutputStream(new File(game, "settings.txt"))) { out.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8)); }
    }
    private void refresh() { message(disc.isFile() ? "Disc ready · " + (disc.length() / 1048576) + " MB" : "No disc imported yet."); }
    private void message(String s) { status.setText(s == null ? "Operation failed." : s); }
    private void pick(int request) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        startActivityForResult(i, request);
    }
    private void create(int request, String type, String title) {
        startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType(type).putExtra(Intent.EXTRA_TITLE, title), request);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData(); busy = true; message("Working… Keep the launcher open.");
        io.execute(() -> {
            String done;
            try {
                if (request == DISC) { importDisc(uri); done = "Disc imported. Ready to play."; }
                else if (request == IMPORT_SAVE) { importSave(uri); done = "Save imported. The previous save is kept in the local backup."; }
                else if (request == EXPORT_SAVE) { exportSave(uri); done = "Save backup exported."; }
                else { exportLog(uri); done = "Game log exported."; }
            } catch (Exception e) { done = "Could not finish: " + e.getMessage(); }
            final String text = done; runOnUiThread(() -> { busy = false; message(text); });
        });
    }
    private InputStream input(Uri uri) throws IOException {
        InputStream in = getContentResolver().openInputStream(uri);
        if (in == null) throw new IOException("Document could not be opened."); return in;
    }
    private OutputStream output(Uri uri) throws IOException {
        OutputStream out = getContentResolver().openOutputStream(uri, "wt");
        if (out == null) throw new IOException("Destination could not be opened."); return out;
    }
    private static long copy(InputStream in, OutputStream out, long limit) throws IOException {
        byte[] buf = new byte[262144]; long total = 0; int n;
        while ((n = in.read(buf)) != -1) { total += n; if (total > limit) throw new IOException("File is too large."); out.write(buf, 0, n); }
        return total;
    }
    private void importDisc(Uri uri) throws IOException {
        File temp = new File(disc.getParentFile(), "import.part");
        try {
            try (InputStream in = input(uri); OutputStream out = new FileOutputStream(temp)) { copy(in, out, 2L << 30); }
            ThorStorage.validateDisc(temp);
            try {
                java.security.MessageDigest digest = java.security.MessageDigest.getInstance("MD5");
                try (InputStream checked = new FileInputStream(temp)) {
                    byte[] buffer = new byte[1048576]; int n;
                    while ((n = checked.read(buffer)) != -1) digest.update(buffer, 0, n);
                }
                StringBuilder actual = new StringBuilder();
                for (byte b : digest.digest()) actual.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
                if (!"caa546309e0443f7b47632b040a250d7".equals(actual.toString()))
                    throw new IOException("This is not the verified Eclipse 1.1.0 disc.");
            } catch (java.security.NoSuchAlgorithmException impossible) { throw new IOException(impossible); }
            Files.move(temp.toPath(), disc.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } finally { temp.delete(); }
    }
    private void importSave(Uri uri) throws IOException {
        File staged = new File(root, "saves/import"); erase(staged); staged.mkdirs();
        try {
            try (ZipInputStream zip = new ZipInputStream(input(uri))) {
                ZipEntry e; int count = 0; long total = 0;
                while ((e = zip.getNextEntry()) != null) {
                    String name = e.getName(); if (name.startsWith("card-a/")) name = name.substring(7);
                    if (e.isDirectory()) continue;
                    if (name.contains("/") || name.contains("\\") || name.equals(".") || name.equals("..") || ++count > 32)
                        throw new IOException("Backup must contain the card-a files, without nested folders.");
                    File f = new File(staged, name);
                    if (f.exists()) throw new IOException("Duplicate file in backup.");
                    try (OutputStream out = new FileOutputStream(f)) { total += copy(zip, out, (16L << 20) - total); }
                }
            }
            if (!new File(staged, "index.txt").isFile() || !new File(staged, "GMSE04_super_mario_sunshine.dat").isFile()
                || !new File(staged, "GMSE04_super_mario_sunshine.stat").isFile()) throw new IOException("Backup is missing Eclipse card files.");
            File backup = new File(root, "saves/previous"); erase(backup);
            if (!card.renameTo(backup)) throw new IOException("Previous save could not be backed up.");
            if (!staged.renameTo(card)) { backup.renameTo(card); throw new IOException("Save could not be installed."); }
        } finally { erase(staged); }
    }
    private void exportSave(Uri uri) throws IOException {
        File[] files = card.listFiles(); if (files == null || files.length == 0) throw new IOException("No save yet. Save in the game first.");
        try (ZipOutputStream zip = new ZipOutputStream(output(uri))) {
            for (File f : files) if (f.isFile()) { zip.putNextEntry(new ZipEntry(f.getName())); try (InputStream in = new FileInputStream(f)) { copy(in, zip, 16L << 20); } zip.closeEntry(); }
        }
    }
    private void exportLog(Uri uri) throws IOException {
        try (InputStream in = new FileInputStream(new File(game, "game.log")); OutputStream out = output(uri)) { copy(in, out, 64L << 20); }
    }
    private static void erase(File f) throws IOException {
        if (!f.exists()) return;
        if (f.isDirectory()) { File[] files = f.listFiles(); if (files == null) throw new IOException("Cannot read " + f.getName()); for (File child : files) erase(child); }
        if (!f.delete()) throw new IOException("Cannot remove " + f.getName());
    }
}
