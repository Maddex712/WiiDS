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
        File root = context.getExternalFilesDir(null);
        if (root == null) throw new IllegalStateException("Eclipse storage is unavailable.");
        return root;
    }
    public static void validateDisc(Context context) throws IOException {
        validateDisc(new File(root(context), "disc/game.iso"));
    }
    public static void validateDisc(File disc) throws IOException {
        if (disc.length() != 1459978240L) throw new IOException("Import the verified Super Mario Eclipse 1.1.0 disc first.");
        try (RandomAccessFile in = new RandomAccessFile(disc, "r")) {
            byte[] h = new byte[32]; in.readFully(h);
            if (!new String(h, 0, 6, java.nio.charset.StandardCharsets.US_ASCII).equals("GMSE04")
                || h[6] != 0 || h[7] != 0 || h[28] != (byte)0xc2 || h[29] != 0x33
                || h[30] != (byte)0x9f || h[31] != 0x3d)
                throw new IOException("Eclipse requires its patched GMSE04 disc.");
        }
    }
    public static void setLibrary(Context context, String name) {
        if (!"io.github.maddex712.wiids".equals(name) && !"com.driftdroid.android.thor".equals(name))
            name = "io.github.maddex712.wiids";
        context.getSharedPreferences("transport", Context.MODE_PRIVATE).edit().putString("library", name).apply();
    }
    public static boolean returnToLibrary(Context context) {
        try {
            context.startActivity(new Intent().setComponent(new android.content.ComponentName(
                context.getSharedPreferences("transport", Context.MODE_PRIVATE).getString("library", "io.github.maddex712.wiids"), "com.driftdroid.android.ModePickerActivity"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            return true;
        } catch (android.content.ActivityNotFoundException | SecurityException e) {
            android.util.Log.w("SMS-Thor", "Shared library is unavailable", e);
            return false;
        }
    }
}
