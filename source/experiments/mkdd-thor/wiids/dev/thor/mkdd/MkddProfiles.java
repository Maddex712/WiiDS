package dev.thor.mkdd;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.io.RandomAccessFile;
import java.security.MessageDigest;

/** Selection belongs to the launcher; each game has its own files, preferences and native process run. */
public final class MkddProfiles {
    static final long PLUS_BYTES = 1074593792L;
    static final String PLUS_DOL_SHA1 = "3cd4cbfa6c9a7d4b95dd66028f14f26d23f6f304";
    private MkddProfiles() {}
    static File plusRoot(Context c) { return new File(MkddStorage.root(c), "plus"); }
    public static File plusArt(Context c) {
        File committed = new File(plusRoot(c), "game/lower-art");
        return committed.isDirectory() ? committed : new File(plusRoot(c), "lower-art");
    }
    public static boolean plusReady(Context c) {
        return new File(c.getApplicationInfo().nativeLibraryDir, "libwiids_mkdd_plus_game.so").isFile()
                && new File(plusRoot(c), "game/game.iso").length() == PLUS_BYTES
                && new File(plusArt(c), "duo_hud.json").isFile();
    }
    /** Once only: start the new profile with the owner's display and control choices, never its card. */
    static void seedSettings(Context c) {
        Context app = c.getApplicationContext();
        SharedPreferences into = app.getSharedPreferences("mkdd_plus_settings", Context.MODE_PRIVATE);
        if (into.contains("profile_initialized")) return;
        SharedPreferences from = app.getSharedPreferences("mkdd_settings", Context.MODE_PRIVATE);
        if (!into.edit().putInt("render_scale", from.getInt("render_scale", 2))
                .putBoolean("widescreen", from.getBoolean("widescreen", false))
                .putBoolean("standard_layout", from.getBoolean("standard_layout", false))
                .putBoolean("profile_initialized", true).commit())
            throw new IllegalStateException("The Plus settings could not be saved");
    }
    /** On the launcher worker, before native code loads: reject other Plus versions and damaged executables. */
    static boolean verifyPlus(Context c) {
        if (!plusReady(c)) return false;
        try (RandomAccessFile f = new RandomAccessFile(new File(plusRoot(c), "game/game.iso"), "r")) {
            f.seek(0x420); long dol = f.readInt() & 0xffffffffL;
            if (dol > f.length() - 3576722L) return false;
            f.seek(dol); byte[] bytes = new byte[3576722]; f.readFully(bytes);
            byte[] digest = MessageDigest.getInstance("SHA-1").digest(bytes);
            StringBuilder text = new StringBuilder();
            for (byte b : digest) text.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            return PLUS_DOL_SHA1.equals(text.toString());
        } catch (Exception e) { return false; }
    }
}
