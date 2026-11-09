package dev.wiids.brawl;

import android.content.Context;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Copies of Brawl's save data (NAND/title/00010000/52534245/data: the save, records and snapshots) in
 * files/SaveBackups/<yyyyMMdd-HHmmss>/. One is made at every start (when the save changed since the last), the
 * newest KEEP are kept, and a restore is applied at the next start, before the game opens its save.
 */
final class SaveBackups {
    private static final String TAG = "Brawl-Saves";
    static final int KEEP = 5;
    private static final String PENDING = "pending_restore";

    private SaveBackups() {}

    static File saveDir(Context context) {
        return new File(AppFiles.dataDir(context), "NAND/title/00010000/52534245/data");
    }

    static File root(Context context) {
        return new File(AppFiles.dataDir(context), "SaveBackups");
    }

    /** Backups, newest first. */
    static List<File> list(Context context) {
        File[] dirs = root(context).listFiles(f -> f.isDirectory()
                && f.getName().matches("[0-9]{8}-[0-9]{6}(-[0-9]+)?"));
        List<File> out = new ArrayList<>();
        if (dirs == null) return out;
        Arrays.sort(dirs, (a, b) -> {
            String an = a.getName(), bn = b.getName();
            int stamp = bn.substring(0, 15).compareTo(an.substring(0, 15));
            if (stamp != 0) return stamp;
            return Integer.compare(sequence(bn), sequence(an));
        });
        out.addAll(Arrays.asList(dirs));
        return out;
    }

    /** "Oct 29, 11:40 PM" for a backup folder's name. */
    static String label(File backup) {
        try {
            Date when = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).parse(backup.getName());
            return new SimpleDateFormat("MMM d, h:mm a", Locale.getDefault()).format(when);
        } catch (Exception e) {
            return backup.getName();
        }
    }

    private static int sequence(String name) {
        try { return name.length() > 15 ? Integer.parseInt(name.substring(16)) : 1; }
        catch (NumberFormatException e) { return 1; }
    }

    private static List<File> payloads(File dir) throws IOException {
        List<File> files = new ArrayList<>();
        collect(dir, dir, files);
        return files;
    }

    private static void collect(File root, File dir, List<File> files) throws IOException {
        File[] children = dir.listFiles();
        if (children == null) throw new IOException("Cannot read save folder " + dir);
        Arrays.sort(children);
        for (File f : children) {
            String name = f.getName();
            if (name.startsWith(".") || name.endsWith(".tmp") || name.endsWith(".restoring")) continue;
            if (java.nio.file.Files.isSymbolicLink(f.toPath())) throw new IOException("Unexpected link in save: " + name);
            if (f.isDirectory()) collect(root, f, files);
            else if (f.isFile()) files.add(f);
        }
    }

    /** Hash the actual bytes, including nested snapshots; timestamps can stay unchanged after a write. */
    private static String signature(File dir) throws IOException {
        StringBuilder out = new StringBuilder("sha256-v1\n");
        for (File f : payloads(dir)) {
            MessageDigest hash;
            try { hash = MessageDigest.getInstance("SHA-256"); }
            catch (NoSuchAlgorithmException e) { throw new IOException(e); }
            try (InputStream in = new FileInputStream(f)) {
                byte[] buffer = new byte[65536]; int n;
                while ((n = in.read(buffer)) != -1) hash.update(buffer, 0, n);
            }
            out.append(dir.toPath().relativize(f.toPath()).toString()).append(':').append(f.length()).append(':');
            for (byte b : hash.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 255));
            out.append('\n');
        }
        return out.toString();
    }

    /**
     * Copies the save into a new backup (or, with `force` false, only when it changed since the newest backup);
     * returns the backup made, or null. Called before the game starts, or with the game frozen.
     */
    static File backUp(Context context, boolean force) {
        File save = saveDir(context);
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File target = new File(root(context), stamp);
        // Never replace a backup made the same second (a restore's safety copy, then this start's).
        for (int n = 2; target.exists(); n++) target = new File(root(context), stamp + "-" + n);
        File staging = new File(target.getPath() + ".partial");
        try {
            if (!save.isDirectory()) return null;
            List<File> files = payloads(save);
            if (files.isEmpty()) return null;
            String signature = signature(save);
            List<File> existing = list(context);
            if (!force && !existing.isEmpty() && signature.equals(read(new File(existing.get(0), ".signature")))) return null;
            AppFiles.deleteRecursively(staging);
            if (!staging.mkdirs()) throw new IOException("cannot create " + staging);
            for (File f : files) {
                copy(f, new File(staging, save.toPath().relativize(f.toPath()).toString()));
            }
            if (!signature.equals(signature(staging)) || !signature.equals(signature(save)))
                throw new IOException("Save changed while making the backup");
            write(new File(staging, ".signature"), signature);
            if (!staging.renameTo(target)) throw new IOException("cannot finish " + target);
        } catch (IOException e) {
            Log.w(TAG, "backup failed", e);
            AppFiles.deleteRecursively(staging);
            return null;
        }
        List<File> all = list(context);
        for (int i = KEEP; i < all.size(); i++) AppFiles.deleteRecursively(all.get(i));
        Log.i(TAG, "backed up the save to " + target.getName());
        return target;
    }

    /** Restores `backup` at the next start (GameActivity calls applyPending before the game runs). */
    static void requestRestore(Context context, File backup) {
        context.getSharedPreferences("save_backups", Context.MODE_PRIVATE).edit()
                .putString(PENDING, backup.getName()).commit();
    }

    /**
     * A requested restore: the current save is backed up first, then the backup's files replace it. Returns whether
     * one was applied (the start's own backup is then skipped: it would only copy the backup just restored).
     */
    static boolean applyPending(Context context) throws IOException {
        android.content.SharedPreferences prefs = context.getSharedPreferences("save_backups", Context.MODE_PRIVATE);
        File save = saveDir(context);
        File staging = new File(save.getParentFile(), ".wiids-restore-new");
        File previous = new File(save.getParentFile(), ".wiids-restore-old");
        // Recover a process interruption between the two directory renames. A complete new directory was
        // verified before the original moved. Never boot the game with a half-copied set of files.
        if (previous.exists()) {
            if (save.isDirectory()) {
                if (!prefs.edit().remove(PENDING).commit()) throw new IOException("Cannot finish restore request");
                AppFiles.deleteRecursively(previous);
                AppFiles.deleteRecursively(staging);
                return true;
            }
            if (!previous.renameTo(save)) throw new IOException("Cannot recover the original save");
        }
        AppFiles.deleteRecursively(staging);
        String name = prefs.getString(PENDING, null);
        if (name == null) return false;
        if (!name.matches("[0-9]{8}-[0-9]{6}(-[0-9]+)?")) throw new IOException("Invalid backup name");
        File backup = new File(root(context), name);
        List<File> files = payloads(backup);
        if (files.isEmpty()) throw new IOException("Backup is empty or no longer available");
        if (!staging.mkdirs()) throw new IOException("Cannot prepare the restore");
        try {
            String expected = signature(backup);
            String recorded = read(new File(backup, ".signature"));
            // Old builds recorded timestamps. Hash and verify those copies now, while retaining compatibility.
            if (recorded != null && recorded.startsWith("sha256-v1\n") && !recorded.equals(expected))
                throw new IOException("Backup verification failed");
            for (File f : files) {
                copy(f, new File(staging, backup.toPath().relativize(f.toPath()).toString()));
            }
            if (!expected.equals(signature(staging))) throw new IOException("Restored copy verification failed");
            // Stage the chosen backup before retention runs: it may be the oldest of the five.
            if (save.isDirectory() && !payloads(save).isEmpty() && backUp(context, true) == null)
                throw new IOException("Cannot back up the current save; restore cancelled");
            if (previous.exists()) throw new IOException("Previous restore still needs recovery");
            if (save.exists() && !save.renameTo(previous)) throw new IOException("Cannot preserve the current save");
            if (!staging.renameTo(save)) {
                if (previous.exists() && !previous.renameTo(save)) throw new IOException("Restore interrupted; original kept for recovery");
                throw new IOException("Cannot activate the restored save");
            }
            if (!prefs.edit().remove(PENDING).commit()) throw new IOException("Cannot finish restore request");
            AppFiles.deleteRecursively(previous);
            Log.i(TAG, "restored the save from " + name);
            return true;
        } catch (IOException e) {
            Log.w(TAG, "restore failed", e);
            AppFiles.deleteRecursively(staging);
            throw e;
        }
    }

    private static void copy(File from, File to) throws IOException {
        File parent = to.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(to)) {
            AppFiles.copy(in, out);
            ((FileOutputStream) out).getFD().sync();
        }
        to.setLastModified(from.lastModified());
    }

    private static String read(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] bytes = new byte[(int) Math.min(f.length(), 1 << 16)];
            int n = AppFiles.readFully(in, bytes);
            return new String(bytes, 0, Math.max(0, n), "UTF-8");
        } catch (IOException e) {
            return null;
        }
    }

    private static void write(File f, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(text.getBytes("UTF-8"));
            ((FileOutputStream) out).getFD().sync();
        }
    }
}
