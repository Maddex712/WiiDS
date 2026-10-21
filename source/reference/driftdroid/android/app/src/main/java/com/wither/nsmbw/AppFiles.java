package com.wither.nsmbw;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/**
 * Where the game's files live on the phone.
 *
 * <ul>
 * <li>Runtime files (DSP ROM, pipeline cache, Wii bootstrap NAND, CA bundle) travel in the APK's
 * assets/runtime-&lt;variant&gt; and are unpacked to internal storage once per install or update;
 * the native side finds them through WIICOMPILED_RUNTIME_DIR.</li>
 * <li>User data (Config.toml, NAND with the saves, caches, logs) lives in the app's external
 * files directory under the variant's own folder (nsmbw/ or msmbwii/); WIICOMPILED_DATA_DIR.</li>
 * <li>The imported disc (sys/, files/) and the MSMBWii mod are shared by both variants and come
 * from the WiiDS Games screen (rom/GameCatalog.kt).</li>
 * </ul>
 */
final class AppFiles {
    private AppFiles() {}

    static File dataDir(Context context) {
        final File external = context.getExternalFilesDir(null);
        final File base = external != null ? external : new File(context.getFilesDir(), "data");
        final File dir = new File(base, NsmbVariant.current().id);
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    /** Same folder as GameCatalog.Disc.NSMB_USA_REV1.installDir. */
    static File gameDir(Context context) { return new File(context.getFilesDir(), "games/nsmbw"); }

    static File runtimeDir(Context context) {
        return new File(context.getFilesDir(), "runtime-" + NsmbVariant.current().id);
    }

    /**
     * The mod's files beside the disc: mod/riivolution and mod/msmbwiijala (the runtime reads
     * the latter from <dvd_root>/../mod). Same folder as GameCatalog.msmbwiiModDir.
     */
    static File modDir(Context context) { return new File(context.getFilesDir(), "games/mod"); }

    /** An extracted disc: sys/main.dol and the files/ tree. */
    static boolean isGameRoot(File dir) {
        return new File(dir, "sys/main.dol").isFile() && new File(dir, "files").isDirectory();
    }

    static boolean hasGameData(Context context) { return isGameRoot(gameDir(context)); }

    /** Imported, and the one game and revision this port runs. */
    static boolean gameReady(Context context) {
        return hasGameData(context) && gameProblem(gameDir(context)) == null;
    }

    /** Why the extracted game at `root` can't be played, or null when it can. */
    static String gameProblem(File root) {
        if (!isGameRoot(root)) {
            return "That isn't an extracted game: it needs the files and sys folders Dolphin's "
                    + "Extract Entire Disc makes.";
        }
        final byte[] header = new byte[8];
        try (InputStream in = new FileInputStream(new File(root, "sys/boot.bin"))) {
            if (readFully(in, header) < header.length) {
                return "Its sys/boot.bin is too short, so the game can't be identified.";
            }
        } catch (IOException e) {
            return "It has no sys/boot.bin, so the game can't be identified.";
        }
        final String problem = headerProblem(header);
        if (problem != null) return problem;
        try {
            if (!NsmbBuild.DOL_SHA256.equals(sha256(new File(root, "sys/main.dol")))) {
                return "The game executable does not match the disc revision compiled into this app.";
            }
            if (NsmbBuild.THOR_BUILD) {
                final String[][] modules = NsmbBuild.DISC_REVISION == 1 ? new String[][] {
                        {"d_profileNP", "f04ca9636e20feb5bdacdb8a1115a2b4a71e99f5091df56b4099ae8ea36c7eaf"},
                        {"d_basesNP", "66b51efac566aead05e4b2351f64800ca54cd38aa93d8d1c9961a054f2854ebb"},
                        {"d_enemiesNP", "9896eab47a57ee0d3e2644393dd4c35e81171662da020fdb6bebc9307a4a9283"},
                        {"d_en_bossNP", "ca28f19ed9fa1a16d5bca7260e9e0127ecd0a964697531da16747d8edffac124"}
                } : new String[][] {
                        {"d_profileNP", "51bccc66ff299411f46adac5520e28d5b3e890bd93713acee697f89b673d4011"},
                        {"d_basesNP", "eb58ce05b3664be786752f1ced1dd1e11b2aa86216e56719e46402d18338db95"},
                        {"d_enemiesNP", "f7941b87baf0b6151f087bcc41323aa43877f1c9f1763b327bfd2869e8a28955"},
                        {"d_en_bossNP", "a0946cd4f7a1d461ba4f316e3db63f619917246b2050e13660759e5e5a8d3875"}
                };
                for (String[] module : modules) {
                    if (!module[1].equals(sha256(new File(root, "files/rels/" + module[0] + ".rel.LZ")))) {
                        return "The game modules do not match the disc revision compiled into this app.";
                    }
                }
            }
        } catch (IOException | NoSuchAlgorithmException failure) {
            return "Could not verify the game executable: " + failure.getMessage();
        }
        return null;
    }

    private static String sha256(File file) throws IOException, NoSuchAlgorithmException {
        final MessageDigest hash = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(file)) {
            final byte[] chunk = new byte[65536];
            int count;
            while ((count = in.read(chunk)) != -1) hash.update(chunk, 0, count);
        }
        final StringBuilder hex = new StringBuilder(64);
        for (byte value : hash.digest()) hex.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return hex.toString();
    }

    /**
     * The first 8 bytes of sys/boot.bin name the game (6 characters) and its revision (byte 7).
     * The revision is tied to this build's translated code, not just an accepted header.
     */
    static String headerProblem(byte[] header) {
        if (header == null || header.length < 8) return "The disc header is incomplete.";
        final String id = new String(header, 0, 6, StandardCharsets.US_ASCII);
        final int revision = header[7] & 0xFF;
        if (!id.equals("SMNE01")) {
            return "That is " + id.trim() + ", not New Super Mario Bros. Wii for the USA (SMNE01). "
                    + "This build requires USA revision " + NsmbBuild.DISC_REVISION + ".";
        }
        if (revision != NsmbBuild.DISC_REVISION) {
            return "That is New Super Mario Bros. Wii revision " + revision + ". This port runs only "
                    + "revision " + NsmbBuild.DISC_REVISION + " (Dolphin shows it under Properties > Info).";
        }
        return null;
    }

    static int readFully(InputStream in, byte[] buffer) throws IOException {
        int total = 0;
        while (total < buffer.length) {
            final int read = in.read(buffer, total, buffer.length - total);
            if (read <= 0) {
                break;
            }
            total += read;
        }
        return total;
    }

    /** Unpacks assets/runtime when this APK has not done it yet. */
    static void prepareRuntime(Context context) throws IOException {
        final File dir = runtimeDir(context);
        final File stampFile = new File(dir, ".apk-stamp");
        final String stamp = apkStamp(context);
        if (stamp.equals(readText(stampFile))) {
            return;
        }
        deleteRecursively(dir);
        if (!dir.mkdirs() && !dir.isDirectory()) {
            throw new IOException("cannot create " + dir);
        }
        copyAssetTree(context.getAssets(), "runtime-" + NsmbVariant.current().id, dir);
        writeText(stampFile, stamp);
    }

    /**
     * Writes Config.toml from the settings before every start. The runtime reads it once at boot;
     * changes made while playing go through JNI as well and are saved here the next time.
     */
    static void writeConfig(Context context, AppSettings settings) throws IOException {
        final File config = new File(dataDir(context), "Config.toml");
        final String game = gameDir(context).getAbsolutePath().replace('\\', '/');
        final String text = "# Written by the app before every start; change settings in the game menu.\n"
                + "\n[video]\n"
                + "aspect_mode = " + settings.aspectMode() + "\n"
                + "gpu_compat = " + settings.gpuCompat() + "\n"
                + "resolution_multiplier = " + String.format(Locale.ROOT, "%.2f", settings.resolutionScale()) + "\n"
                + "frame_interpolation_fps = " + (settings.frameRate() == 120 ? 120 : 0) + "\n"
                + "graphics_api = \"auto\"\n"
                + "skip_unready_pipelines = true\n"
                + "disable_copy_filter = true\n"
                + "show_fps = " + settings.showFps() + "\n"
                + "spinning_coins = " + settings.spinningCoins() + "\n"
                + "frame_graph = " + settings.frameGraph() + "\n"
                + "texture_replacements = false\n"
                + "texture_dumps = false\n"
                + "\n[audio]\n"
                + "volume = 1.0\n"
                + "muted = false\n"
                + "attenuate_music_when_media_plays = false\n"
                + "mix_worker = true\n"
                + "\n[network]\nenabled = false\n"
                + "\n[discord]\nenabled = false\n"
                + "\n[paths]\n"
                + "dvd_root = '" + game + "'\n"
                // MSMBWii: the mod's Riivolution folder (riivolution/*.xml and its files); its
                // option is enabled by riivolution/config/SMNE.xml there.
                + (NsmbVariant.current() != NsmbVariant.MSMBWII ? "" : "overlay_roots = ['" + modDir(context).getAbsolutePath().replace('\\', '/') + "']\n");
        writeText(config, text + keptSections(config));
    }

    /** Tables of the old Config.toml the app doesn't write (the game's own: keyboard keybinds...). */
    private static final java.util.Set<String> APP_TABLES =
            new java.util.HashSet<>(java.util.Arrays.asList("video", "audio", "network", "discord", "paths"));

    private static String keptSections(File config) {
        if (!config.isFile()) return "";
        final StringBuilder kept = new StringBuilder();
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(config), java.nio.charset.StandardCharsets.UTF_8))) {
            boolean keep = false;
            String line;
            while ((line = in.readLine()) != null) {
                final String trimmed = line.trim();
                if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                    final String name = trimmed.substring(1, trimmed.length() - 1).trim();
                    keep = !APP_TABLES.contains(name);
                    if (keep) kept.append('\n');
                }
                if (keep) kept.append(line).append('\n');
            }
        } catch (IOException e) {
            android.util.Log.w("NSMBW", "could not read the old Config.toml", e);
        }
        return kept.toString();
    }

    private static String apkStamp(Context context) {
        try {
            final PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return info.getLongVersionCode() + ":" + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return "unknown";
        }
    }

    /** Copies an asset directory recursively. AssetManager.list() is empty for a file. */
    private static void copyAssetTree(AssetManager assets, String path, File target) throws IOException {
        final String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                copy(in, out);
            }
            return;
        }
        if (!target.mkdirs() && !target.isDirectory()) {
            throw new IOException("cannot create " + target);
        }
        for (String child : children) {
            copyAssetTree(assets, path + "/" + child, new File(target, child));
        }
    }

    static void copy(InputStream in, OutputStream out) throws IOException {
        final byte[] buffer = new byte[1 << 16];
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
        }
    }

    static void deleteRecursively(File file) {
        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    private static String readText(File file) {
        if (!file.isFile()) {
            return "";
        }
        try (InputStream in = new FileInputStream(file)) {
            final byte[] bytes = new byte[(int) Math.min(file.length(), 4096)];
            final int read = in.read(bytes);
            return read > 0 ? new String(bytes, 0, read, StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
