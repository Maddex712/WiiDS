package dev.thor.mkdd;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * Memory card backups beside saves/ (save-backups/, which offloading a game leaves alone): one each
 * time Double Dash starts (the last {@link #KEEP} are kept) and one on request. A restore is only
 * written down while the game runs; it is applied at the next start, before the game opens its
 * card, after backing up the card it replaces.
 */
final class MkddSaves {
    static final int KEEP = 5;
    private static final String PENDING = "restore-pending";
    /** Folder names are UTC, so a clock or daylight-saving change can't make a new backup sort as old. */
    private static final String STAMP = "yyyyMMdd-HHmmss", NAME = "\\d{8}-\\d{6}(-\\d{2})?";

    private MkddSaves() {}

    static File saves(Context c) { return new File(c.getExternalFilesDir(null), "saves"); }
    static File folder(Context c) { return new File(c.getExternalFilesDir(null), "save-backups"); }

    /** Before the native game starts: a restore asked for in the menu, else this start's backup. */
    static void prepare(Context c) throws IOException {
        if (!applyPending(c)) backUp(c, null);
    }

    /** Copies the memory card files into a new dated backup; null when there is no card yet. */
    static File backUp(Context c) throws IOException { return backUp(c, null); }

    /** As backUp, never pruning [keep] (a restore about to be applied) or the backup just made. */
    private static File backUp(Context c, File keep) throws IOException {
        File[] cards = cards(saves(c));
        if (cards == null || cards.length == 0) return null;
        File root = folder(c);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("No backup folder");
        String stamp = utc().format(new Date());
        File dest = new File(root, stamp);
        for (int n = 1; dest.exists(); n++) dest = new File(root, String.format(Locale.ROOT, "%s-%02d", stamp, n));
        File temp = new File(root, ".incoming");
        MkddArt.deleteTree(temp);
        if (!temp.mkdirs()) throw new IOException("No backup folder");
        for (File card : cards) copy(card, new File(temp, card.getName()));
        if (!temp.renameTo(dest)) { MkddArt.deleteTree(temp); throw new IOException("Backup not saved"); }
        int others = 0;
        for (File b : list(c)) {
            if (b.equals(dest) || b.equals(keep)) continue;
            if (++others > KEEP - 1 - (keep != null ? 1 : 0)) MkddArt.deleteTree(b);
        }
        return dest;
    }

    /** Backups, newest first. */
    static List<File> list(Context c) {
        File[] dirs = folder(c).listFiles(f -> f.isDirectory() && f.getName().matches(NAME));
        List<File> out = new ArrayList<>(dirs == null ? Arrays.<File>asList() : Arrays.asList(dirs));
        out.sort((a, b) -> b.getName().compareTo(a.getName()));
        return out;
    }

    /** Local time to the second, "Nov 2 10:31:05 PM": backups made in the same minute stay distinct; the game's lettering has no comma. */
    static String label(File backup) {
        try {
            Date d = utc().parse(backup.getName().substring(0, STAMP.length()));
            return new SimpleDateFormat("MMM d h:mm:ss a", Locale.US).format(d);
        } catch (Exception e) {
            return backup.getName();
        }
    }

    static void requestRestore(Context c, File backup) throws IOException {
        File root = folder(c);
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("No backup folder");
        Files.write(new File(root, PENDING).toPath(), backup.getName().getBytes(StandardCharsets.UTF_8));
    }

    private static boolean applyPending(Context c) throws IOException {
        File marker = new File(folder(c), PENDING);
        if (!marker.isFile()) return false;
        String name = new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).trim();
        File backup = new File(folder(c), name);
        if (!name.matches(NAME) || !backup.isDirectory()) { marker.delete(); return false; }
        File[] cards = cards(backup);
        // The marker stays for the next start to retry, rather than being dropped as if it had worked.
        if (cards == null || cards.length == 0) throw new IOException("Backup " + name + " has no card");
        backUp(c, backup);
        File saves = saves(c);
        if (!saves.isDirectory() && !saves.mkdirs()) throw new IOException("No saves folder");
        for (File card : cards) {
            File temp = new File(saves, card.getName() + ".restoring");
            copy(card, temp);
            if (!temp.renameTo(new File(saves, card.getName()))) throw new IOException("Restore not applied");
        }
        marker.delete();
        return true;
    }

    /** The memory card images only, never temporary files left beside them. */
    private static File[] cards(File dir) { return dir.listFiles(f -> f.isFile() && f.getName().endsWith(".raw")); }

    private static SimpleDateFormat utc() {
        SimpleDateFormat f = new SimpleDateFormat(STAMP, Locale.ROOT);
        f.setTimeZone(TimeZone.getTimeZone("UTC"));
        return f;
    }

    private static void copy(File from, File to) throws IOException {
        try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buffer = new byte[64 << 10];
            for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
            out.getFD().sync();
        }
    }
}
