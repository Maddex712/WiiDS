package dev.chasem.smslauncher;

import android.content.Context;
import android.content.Intent;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/** Thor owns one separate app-scoped data tree; launch intents cannot select paths. */
public final class ThorStorage {
    private ThorStorage() {}
    public static File root(Context context) {
        File external = context.getExternalFilesDir(null);
        if (external == null) throw new IllegalStateException("Sunshine storage is unavailable.");
        File root = new File(external, "sms");
        root.mkdirs();
        return root;
    }
    public static void validateDisc(Context context) throws IOException {
        validateDisc(disc(context));
    }
    /** The ISO the WiiDS Games screen imports (GameCatalog.Disc.SUNSHINE_USA). */
    public static File disc(Context context) {
        return new File(context.getFilesDir(), "games/sms/game.iso");
    }
    public static void validateDisc(File disc) throws IOException {
        if (disc.length() != 1459978240L) throw new IOException("Import the verified US revision 0 Sunshine disc first.");
        try (RandomAccessFile in = new RandomAccessFile(disc, "r")) {
            byte[] h = new byte[32]; in.readFully(h);
            if (!new String(h, 0, 6, java.nio.charset.StandardCharsets.US_ASCII).equals("GMSE01")
                || h[6] != 0 || h[7] != 0 || h[28] != (byte)0xc2 || h[29] != 0x33
                || h[30] != (byte)0x9f || h[31] != 0x3d)
                throw new IOException("Sunshine requires a US revision 0 GameCube disc.");
        }
    }
    public static boolean returnToLibrary(Context context) {
        try {
            context.startActivity(new Intent().setComponent(new android.content.ComponentName(
                context.getPackageName(), "com.driftdroid.android.ModePickerActivity"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            return true;
        } catch (android.content.ActivityNotFoundException | SecurityException e) {
            android.util.Log.w("SMS-Thor", "Shared library is unavailable", e);
            return false;
        }
    }
}
