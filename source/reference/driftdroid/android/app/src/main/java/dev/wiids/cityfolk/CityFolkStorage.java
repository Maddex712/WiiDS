package dev.wiids.cityfolk;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.function.BooleanSupplier;

public final class CityFolkStorage {
    public static final String ID = "city-folk";
    public static final String TITLE = "Animal Crossing: Let’s Go to the City";
    private static final String VERIFIED = ".ruup01-r1-verified";
    private static final int CODE_FILES = 188;
    private static final String WRONG_DISC = "Use an unchanged European revision 1 Animal Crossing Wii disc (RUUP01).";

    private CityFolkStorage() {}

    public static File root(Context context) {
        File external = context.getExternalFilesDir(null);
        return new File(external != null ? external : new File(context.getFilesDir(), "data"), ID);
    }

    public static File game(Context context) { return new File(root(context), "game"); }
    public static File saves(Context context) { return new File(root(context), "saves"); }
    public static File cache(Context context) { return new File(context.getCacheDir(), ID); }

    public static boolean imported(Context context) {
        File root = game(context);
        return new File(root, VERIFIED).isFile() && new File(root, "sys/main.dol").isFile()
                && new File(root, "sys/fst.bin").isFile() && new File(root, "files/rels").isDirectory();
    }

    public static boolean ready(Context context) {
        return imported(context) && new File(context.getApplicationInfo().nativeLibraryDir,
                "libwiids_city_folk.so").isFile();
    }

    public static String verifyStaged(Context context, File root, BooleanSupplier cancelled) {
        try (BufferedReader manifest = new BufferedReader(new InputStreamReader(
                context.getAssets().open("city-folk/ruup01-r1-code.txt"), StandardCharsets.US_ASCII))) {
            MessageDigest digest = MessageDigest.getInstance("SHA-1");
            byte[] buffer = new byte[64 * 1024];
            int files = 0;
            String line;
            while ((line = manifest.readLine()) != null) {
                if (cancelled.getAsBoolean()) return "Import cancelled.";
                String[] entry = line.split(" ", 2);
                if (entry.length != 2) return "Animal Crossing verification data is incomplete.";
                File data = new File(root, entry[1]);
                if (!data.isFile()) return WRONG_DISC;
                try (FileInputStream input = new FileInputStream(data)) {
                    int n;
                    while ((n = input.read(buffer)) != -1) {
                        if (cancelled.getAsBoolean()) return "Import cancelled.";
                        digest.update(buffer, 0, n);
                    }
                }
                StringBuilder hash = new StringBuilder(40);
                for (byte value : digest.digest()) {
                    hash.append(Character.forDigit((value >>> 4) & 15, 16));
                    hash.append(Character.forDigit(value & 15, 16));
                }
                if (!entry[0].contentEquals(hash)) return WRONG_DISC;
                files++;
            }
            if (files != CODE_FILES) return "Animal Crossing verification data is incomplete.";
            if (cancelled.getAsBoolean()) return "Import cancelled.";
            return new File(root, VERIFIED).createNewFile() ? null : "Could not record Animal Crossing verification.";
        } catch (IOException | NoSuchAlgorithmException error) {
            return "Could not verify the Animal Crossing disc.";
        }
    }
}
