package dev.thor.galaxy;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import com.galaxy.installer.CookerEngine;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Sets up Super Mario Galaxy on the device from the player's own disc image, as the PC import
 * does: the disc's game partition is extracted (nodtool, the upstream installer's extractor),
 * checked to be USA RMGE01 revision 0, and cooked by our own cooker scripts in the embedded
 * Python (Build-Importer.py) into GalaxyData.pending. Only then are GalaxyData/files and /sys
 * replaced; saves, settings and logs in GalaxyData stay. Runs in its own process (:import).
 *
 * Started by WiiDS (Your games, Super Mario Galaxy) with the disc's path in "disc_path". Reading
 * a disc anywhere on the device needs All files access, asked for once. "verify_only" (tests)
 * cooks into GalaxyData.pending and leaves the installed game untouched.
 */
public class GalaxyImportActivity extends Activity {
    protected boolean kaizoProfile() { return false; }
    protected String gameTitle() { return "Super Mario Galaxy"; }
    protected void prepareExtraction(File root) throws Exception { }
    protected boolean prepareStart() { return true; }
    @Override protected void attachBaseContext(android.content.Context base) { super.attachBaseContext(new GalaxyStorage(base, kaizoProfile())); }
    static final String TAG = "GalaxyImport";
    static final String ACTION = "dev.thor.galaxy.action.IMPORT_DISC";
    // Space needed beyond the disc itself, with and without the cutscene movies (2.3 GB each,
    // extracted and cooked): extraction plus the cooked copy, and a margin.
    static final long NEED_WITH_MOVIES = 7_200L << 20, NEED_WITHOUT_MOVIES = 2_600L << 20;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private TextView title, status, log;
    private ProgressBar bar;
    private Button action;
    private final ArrayDeque<String> lines = new ArrayDeque<>();
    private String discPath;
    // A disc chosen in a file app that WiiDS can't name by path (another file manager, or no All
    // files access): read through its content URI and copied next to the import first.
    private Uri discUri;
    private String discName;
    private boolean verifyOnly, started, finished;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Intent intent = getIntent();
        discPath = intent.getStringExtra("disc_path");
        discUri = intent.getData();
        discName = intent.getStringExtra("disc_name");
        if (discName == null || discName.isEmpty()) discName = "the chosen disc";
        verifyOnly = intent.getBooleanExtra("verify_only", false);
        buildUi();
        if ((discPath == null || discPath.isEmpty()) && discUri == null) {
            finish(false, "No disc was given. Choose Super Mario Galaxy in WiiDS, Your games.");
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (started || finished || (discPath == null && discUri == null)) return;
        if (!prepareStart()) return;
        if (discUri == null && !Environment.isExternalStorageManager()) {
            status.setText("To read your Galaxy disc image, WiiDS needs All files access. "
                + "Only the disc you chose is read.");
            action.setText("Allow access");
            action.setVisibility(View.VISIBLE);
            action.setOnClickListener(v -> askForAccess());
            return;
        }
        start();
    }

    private void askForAccess() {
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName())));
        } catch (RuntimeException unavailable) {
            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        }
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        // Back leaves only once the import has stopped: an import is not cancelled half way.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && !finished && started) return true;
        return super.dispatchKeyEvent(event);
    }

    // ------------------------------------------------------------------ the import
    private void start() {
        started = true;
        action.setVisibility(View.GONE);
        File disc = discUri == null ? new File(discPath) : null;
        if (disc != null && !disc.isFile()) { finish(false, "Can't read " + disc.getName() + ". Choose it again in Your games."); return; }
        File external = getExternalFilesDir(null);
        if (external == null) { finish(false, "Storage isn't available right now."); return; }
        long free = new StatFs(external.getPath()).getAvailableBytes();
        if (discUri != null) {
            long size = documentSize(discUri);
            if (size <= 0) { finish(false, "Can't read " + discName + ". Choose it again in Your games."); return; }
            free -= size; // the disc is copied next to the import before it is read
        }
        final boolean movies;
        if (free >= NEED_WITH_MOVIES) movies = true;
        else if (free >= NEED_WITHOUT_MOVIES) movies = false;
        else {
            finish(false, String.format(java.util.Locale.ROOT,
                "Not enough space: %.1f GB free, %.1f GB needed (%.1f GB without the cutscene movies).",
                free / 1e9, NEED_WITH_MOVIES / 1e9, NEED_WITHOUT_MOVIES / 1e9));
            return;
        }
        if (!movies) line("Space is short, so the cutscene movies are left out (the game skips them).");
        Thread worker = new Thread(() -> run(disc, external, movies), "GalaxyImport");
        worker.setPriority(Thread.NORM_PRIORITY - 1);
        worker.start();
    }

    private void run(File chosen, File external, boolean movies) {
        File copyDir = new File(external, "GalaxyImport.disc");
        File disc = chosen;
        File work = new File(external, "GalaxyImport");
        File extracted = new File(work, "extracted");
        File pending = new File(external, "GalaxyData.pending");
        boolean workOwned = false;
        try (java.io.RandomAccessFile guard = new java.io.RandomAccessFile(new File(getFilesDir(), ".galaxy-runtime.lock"), "rw");
             java.nio.channels.FileLock lock = guard.getChannel().tryLock()) {
            if (lock == null) throw new IOException("Close Galaxy before importing or replacing its data.");
            workOwned = true;
            GalaxyMigration.recover(getFilesDir(), external);
            GalaxyInstall.recover(new File(external, "GalaxyData"));
            if (!CookerEngine.load()) throw new IOException("The importer is missing from this add-on build.");
            step(2, "Preparing the converter...");
            File python = new File(getFilesDir(), "pythonhome"), scripts = new File(getFilesDir(), "cooker-scripts");
            unpackOnce("galaxy/pythonhome.zip", python);
            unpackOnce("galaxy/cooker-scripts.zip", scripts);
            if (disc == null) disc = copyChosenDisc(copyDir);

            step(5, "Reading " + disc.getName() + "...");
            deleteTree(work);
            deleteTree(pending);
            if (!extracted.mkdirs()) throw new IOException("Can't create " + extracted);
            extract(disc, extracted, movies);
            deleteTree(copyDir);
            File root = dataRoot(extracted);
            checkDisc(root);
            prepareExtraction(root);
            if (!movies) deleteTree(new File(root, "files/MovieData"));

            step(35, "Converting the game files (this takes a while)...");
            if (!pending.mkdirs()) throw new IOException("Can't create " + pending);
            Progress progress = new Progress(this);
            int result = CookerEngine.runCooker(python.getPath(), scripts.getPath(), root.getPath(), pending.getPath(),
                movies, progress);
            if (result != 0) throw new IOException("The conversion stopped (code " + result + "). See the log above.");
            if (!new File(pending, "sys/fst.bin").isFile() || !new File(pending, "files/ObjectData/Coin.arc").isFile())
                throw new IOException("The conversion finished without the game's files.");
            if (verifyOnly) {
                Log.i(TAG, "verify_only: cooked into " + pending);
                done(true, "Converted into GalaxyData.pending (test run: the installed game was not changed).");
                return;
            }
            deleteTree(work);
            step(98, "Installing...");
            GalaxyInstall.install(new File(external, "GalaxyData"), pending);
            done(true, gameTitle() + " is ready. Your saves were kept.");
        } catch (Exception failure) {
            Log.e(TAG, "import failed", failure);
            if (workOwned) { deleteTree(work); deleteTree(pending); deleteTree(copyDir); }
            done(false, failure.getMessage() != null ? failure.getMessage() : failure.toString());
        }
    }

    /** Byte size of the chosen document, or -1 when the provider can't say. */
    private long documentSize(Uri uri) {
        try (android.content.res.AssetFileDescriptor fd = getContentResolver().openAssetFileDescriptor(uri, "r")) {
            return fd == null ? -1 : fd.getLength();
        } catch (Exception unreadable) {
            return -1;
        }
    }

    /** Copies the chosen document next to the import, under its own name, so both readers can open it by path. */
    private File copyChosenDisc(File dir) throws IOException {
        deleteTree(dir);
        if (!dir.mkdirs()) throw new IOException("Can't create " + dir);
        String safe = discName.replaceAll("[^A-Za-z0-9._ ()-]", "_");
        File copy = new File(dir, safe.isEmpty() ? "disc.iso" : safe);
        long total = documentSize(discUri), done = 0;
        try (InputStream in = getContentResolver().openInputStream(discUri);
             FileOutputStream out = new FileOutputStream(copy)) {
            if (in == null) throw new IOException("Can't read " + discName + ".");
            byte[] buffer = new byte[1 << 20];
            int lastPercent = -1;
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
                done += n;
                int percent = total > 0 ? (int) (done * 100 / total) : 0;
                if (percent != lastPercent) { lastPercent = percent; step(3, "Copying " + discName + "... " + percent + "%"); }
            }
            out.getFD().sync();
        }
        if (total > 0 && done != total) throw new IOException("Only part of " + discName + " could be read. Choose it again.");
        return copy;
    }

    /** nodtool (the upstream installer's extractor) for any format; the built-in reader for ISO/WBFS otherwise. */
    private void extract(File disc, File out, boolean movies) throws Exception {
        File tool = new File(getApplicationInfo().nativeLibraryDir, "libnodtool.so");
        // The low-space path must omit movies during extraction, not after writing them.
        if (movies && tool.isFile()) {
            Process process = new ProcessBuilder(tool.getPath(), "extract", "-p", "data", disc.getPath(), out.getPath())
                .redirectErrorStream(true).start();
            int count = 0;
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                for (String text; (text = reader.readLine()) != null; ) {
                    if (text.startsWith("Extracting ")) {
                        if (++count % 40 == 0) step(5 + Math.min(28, count / 80), "Extracting: " + text.substring(11).trim());
                    } else if (!text.trim().isEmpty()) {
                        line(text.trim());
                    }
                }
            }
            if (process.waitFor() == 0 && dataRootOrNull(out) != null) return;
            line("The disc extractor failed; trying the built-in reader.");
            deleteTree(out);
            out.mkdirs();
        }
        int result = CookerEngine.extractDisc(disc.getPath(), out.getPath(), movies, new Progress(this));
        if (result != 0 || dataRootOrNull(out) == null) throw new IOException("Can't read the game from " + disc.getName() + ".");
    }

    private static File dataRootOrNull(File out) {
        for (File candidate : new File[]{out, new File(out, "DATA")}) {
            if (new File(candidate, "sys/fst.bin").isFile() && new File(candidate, "files").isDirectory()) return candidate;
        }
        return null;
    }

    private static File dataRoot(File out) throws IOException {
        File root = dataRootOrNull(out);
        if (root == null) throw new IOException("The disc image holds no game partition.");
        return root;
    }

    /** Only the USA disc, revision 0, matches what the game and its patches are built for. */
    private static void checkDisc(File root) throws IOException {
        byte[] head = new byte[8];
        try (FileInputStream in = new FileInputStream(new File(root, "sys/boot.bin"))) {
            if (in.read(head) != 8) throw new IOException("The disc header is unreadable.");
        }
        String id = new String(head, 0, 6, java.nio.charset.StandardCharsets.US_ASCII);
        if (!id.equals("RMGE01")) throw new IOException("This is " + id + ", not Super Mario Galaxy for the USA (RMGE01).");
        if (head[7] != 0) throw new IOException("This is revision " + head[7] + " of RMGE01. WiiDS needs revision 0.");
    }

    /** Unpacks an asset zip into [dir] once per add-on build. */
    private void unpackOnce(String asset, File dir) throws Exception {
        long version = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        File marker = new File(dir, ".unpacked-" + version);
        if (marker.isFile()) return;
        deleteTree(dir);
        if (!dir.mkdirs()) throw new IOException("Can't create " + dir);
        String root = dir.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = getAssets().open(asset); ZipInputStream zip = new ZipInputStream(in)) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                File out = new File(dir, entry.getName());
                if (!out.getCanonicalPath().startsWith(root)) throw new IOException("Bad entry in " + asset);
                if (entry.isDirectory()) { out.mkdirs(); continue; }
                out.getParentFile().mkdirs();
                try (FileOutputStream file = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) file.write(buffer, 0, n);
                }
                if (out.getName().endsWith(".so")) out.setWritable(false, false);
            }
        }
        if (!marker.createNewFile()) throw new IOException("Can't mark " + dir);
    }

    static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        // Never follow a link out of the tree.
        if (file.isDirectory() && !java.nio.file.Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        file.delete();
    }

    /** The cooker's callback (named: this d8 rejects anonymous classes at -source 8). */
    static final class Progress implements CookerEngine.CookerCallback {
        private final GalaxyImportActivity owner;
        Progress(GalaxyImportActivity owner) { this.owner = owner; }
        @Override public void onLog(String line) { owner.line(line); }
        @Override public void onProgress(int percent, String message) {
            owner.step(percent < 0 ? -1 : 35 + percent * 62 / 100, message);
        }
        @Override public void onFinished(boolean success, String message) { owner.line(message); }
    }

    // ------------------------------------------------------------------ screen
    private void buildUi() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(80, 60, 80, 60);
        column.setBackgroundColor(Color.rgb(0x0b, 0x18, 0x40));
        title = text("Setting up " + gameTitle(), 30, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(title);
        status = text("Starting...", 20, Color.rgb(0xcf, 0xd8, 0xff));
        status.setPadding(0, 24, 0, 16);
        column.addView(status);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        column.addView(bar, new LinearLayout.LayoutParams(-1, 24));
        log = text("", 14, Color.rgb(0x8f, 0xa0, 0xd8));
        log.setTypeface(Typeface.MONOSPACE);
        log.setPadding(0, 24, 0, 24);
        column.addView(log, new LinearLayout.LayoutParams(-1, 0, 1));
        action = new Button(this);
        action.setAllCaps(false);
        action.setVisibility(View.GONE);
        LinearLayout.LayoutParams at = new LinearLayout.LayoutParams(-2, -2);
        at.gravity = Gravity.END;
        column.addView(action, at);
        setContentView(column);
    }

    private TextView text(String value, int sp, int color) {
        TextView v = new TextView(this);
        v.setText(value);
        v.setTextSize(sp);
        v.setTextColor(color);
        return v;
    }

    void step(int percent, String message) {
        Log.i(TAG, (percent >= 0 ? percent + "% " : "") + message);
        ui.post(() -> {
            if (finished) return;
            status.setText(message);
            bar.setIndeterminate(percent < 0);
            if (percent >= 0) bar.setProgress(percent);
        });
    }

    void line(String text) {
        Log.i(TAG, text);
        ui.post(() -> {
            lines.addLast(text);
            while (lines.size() > 14) lines.removeFirst();
            log.setText(String.join("\n", lines));
        });
    }

    private void done(boolean ok, String message) { ui.post(() -> finish(ok, message)); }

    private void finish(boolean ok, String message) {
        finished = true;
        Log.i(TAG, (ok ? "done: " : "failed: ") + message);
        title.setText(gameTitle() + (ok ? " is set up" : " couldn't be set up"));
        status.setText(message);
        bar.setIndeterminate(false);
        bar.setProgress(ok ? 100 : 0);
        setResult(ok ? RESULT_OK : RESULT_CANCELED);
        action.setText(ok ? "Done" : "Close");
        action.setVisibility(View.VISIBLE);
        action.setOnClickListener(v -> finish());
        action.requestFocus();
    }
}
