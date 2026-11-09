package dev.thor.mkdd;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.function.BooleanSupplier;

/**
 * Double Dash's profile inside the one WiiDS package. Its external files are <external>/mkdd:
 * game/game.iso (the imported disc, replaced whole by the importer), saves/memcard_a.raw,
 * shaders.bin and lower-art/ (built from the disc by MkddArt).
 */
public final class MkddStorage extends ContextWrapper {
    static final long DISC_BYTES = 1459978240L;
    static final String DISC_SHA1 = "34bdef709a1ae9799fdabe8e54cff1c7d3665f2b";

    private final boolean plus;
    public MkddStorage(Context base) { this(base, false); }
    MkddStorage(Context base, boolean plus) { super(base); this.plus = plus; }

    @Override public File getFilesDir() {
        File f = new File(super.getFilesDir(), plus ? "mkdd/plus" : "mkdd");
        if (!f.isDirectory() && !f.mkdirs()) throw new IllegalStateException("Double Dash storage unavailable");
        return f;
    }
    @Override public File getExternalFilesDir(String type) {
        File base = super.getExternalFilesDir(null);
        if (base == null) return null;
        String profile = plus ? "mkdd/plus" : "mkdd";
        File f = new File(base, type == null ? profile : profile + "/" + type);
        if (!f.isDirectory() && !f.mkdirs()) throw new IllegalStateException("Double Dash data storage unavailable");
        return f;
    }
    @Override public SharedPreferences getSharedPreferences(String name, int mode) {
        return super.getSharedPreferences((plus ? "mkdd_plus_" : "mkdd_") + name, mode);
    }

    public static File root(Context c) {
        File base = c.getApplicationContext().getExternalFilesDir(null);
        return new File(base != null ? base : new File(c.getApplicationContext().getFilesDir(), "data"), "mkdd");
    }
    public static File iso(Context c) { return new File(root(c), "game/game.iso"); }
    public static File art(Context c) { return new File(root(c), "lower-art"); }

    /** Fast readiness for the shelf: the importer already checked the exact disc. */
    public static boolean ready(Context c) {
        File iso = iso(c);
        return com.driftdroid.android.GameStorage.INSTANCE.blocker(c) == null && iso.isFile() && iso.length() == DISC_BYTES;
    }

    static final String CANCELLED = "Import cancelled.";
    private static final String MISMATCH = "This image isn't the supported Mario Kart: Double Dash!! USA disc "
            + "(GM4E01, revision 0). Scrubbed, trimmed or modified dumps aren't accepted. Your existing game and saves were kept.";

    /**
     * The importer's check of a freshly extracted image (in [root]/game.iso); null when it is the
     * supported disc. Hashing 1.4 GB takes a while, so [cancelled] is polled throughout.
     */
    public static String validate(File root, BooleanSupplier cancelled) {
        File iso = new File(root, "game.iso");
        if (iso.length() != DISC_BYTES) return MISMATCH;
        try (InputStream in = new FileInputStream(iso)) {
            MessageDigest digest = MessageDigest.getInstance("SHA-1");
            byte[] buffer = new byte[1 << 20];
            for (int n; (n = in.read(buffer)) > 0; ) {
                if (cancelled.getAsBoolean()) return CANCELLED;
                digest.update(buffer, 0, n);
            }
            StringBuilder s = new StringBuilder();
            for (byte b : digest.digest()) s.append(String.format("%02x", b & 255));
            return DISC_SHA1.equals(s.toString()) ? null : MISMATCH;
        } catch (Exception e) {
            return "The imported image couldn't be read back to check it (" + e.getMessage()
                    + "). Your existing game and saves were kept.";
        }
    }
}
