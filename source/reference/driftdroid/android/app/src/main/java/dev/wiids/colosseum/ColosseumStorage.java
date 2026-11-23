package dev.wiids.colosseum;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.function.BooleanSupplier;

public final class ColosseumStorage {
    private static final long DISC_BYTES = 1459978240L;
    private static final String DISC_SHA256 =
        "78c4074429fa598b97bfac296bf8e0f7e93f5cb6d8679f8d2653ca902820f6c5";
    private static final String VERIFIED = ".gc6e01-verified";
    private static final String MODULE = "libgGC6E01_recomp.so";

    private ColosseumStorage() {}

    public static File root(Context context) {
        File external = context.getExternalFilesDir(null);
        return new File(external != null ? external : new File(context.getFilesDir(), "data"),
                        "pokemon-colosseum");
    }

    public static File game(Context context) { return new File(root(context), "game"); }
    public static File iso(Context context) { return new File(game(context), "game.iso"); }
    public static File config(Context context) { return new File(root(context), "config"); }
    public static File card(Context context) { return new File(root(context), "saves/MemoryCardA.USA.raw"); }
    public static File module(Context context) { return new File(context.getApplicationInfo().nativeLibraryDir, MODULE); }

    public static boolean imported(Context context) {
        File image = iso(context);
        return image.isFile() && image.length() == DISC_BYTES && new File(game(context), VERIFIED).isFile();
    }

    public static boolean ready(Context context) {
        return imported(context) && module(context).isFile()
            && new File(context.getApplicationInfo().nativeLibraryDir, "libwiids_colosseum_host.so").isFile();
    }

    /** Runs against the staged disc before DiscInstallCommit can replace an existing import. */
    public static String verifyStaged(File folder, BooleanSupplier cancelled) throws IOException {
        File image = new File(folder, "game.iso");
        String wrongDisc = "WiiDS needs an unchanged USA Pokémon Colosseum revision 0 disc (GC6E01).";
        if (image.length() != DISC_BYTES) return wrongDisc;
        final MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
        try (FileInputStream input = new FileInputStream(image)) {
            byte[] buffer = new byte[1024 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (cancelled.getAsBoolean()) return "Import cancelled.";
                digest.update(buffer, 0, count);
            }
        }
        if (cancelled.getAsBoolean()) return "Import cancelled.";
        StringBuilder hash = new StringBuilder(64);
        for (byte value : digest.digest()) {
            hash.append(Character.forDigit((value >>> 4) & 15, 16));
            hash.append(Character.forDigit(value & 15, 16));
        }
        if (!DISC_SHA256.contentEquals(hash)) return wrongDisc;
        if (!new File(folder, VERIFIED).createNewFile()) throw new IOException("Could not record disc verification.");
        return null;
    }
}
