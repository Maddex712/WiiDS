package com.driftdroid.android.achievements;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.file.*;
import java.util.*;

/**
 * Bounded cache of public RA badge pictures. Filled only after a manual history import, on that
 * import's worker; library browsing and drawing only read files already here. Requests go to RA's
 * media host with no credentials, no redirects and strict size/format checks.
 */
public final class RaBadges {
    static final int MAX_BYTES = 64 * 1024, MAX_SIDE = 256, MIN_SIDE = 16, MAX_FILES = 3000, MAX_PER_IMPORT = 1500;
    private final File root;
    public RaBadges(File root) { this.root = root; }

    public File file(String name) throws IOException {
        if (!RaProgress.badgeName(name)) throw new IOException("Invalid RA badge");
        return new File(root, name + ".png");
    }

    /** A cached badge, or null (missing, damaged or never fetched): callers draw the fallback. */
    public Bitmap read(String name) {
        try {
            File file = file(name);
            if (!file.isFile() || file.length() <= 0 || file.length() > MAX_BYTES) return null;
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(file.getPath(), bounds);
            if (!validSize(bounds.outWidth, bounds.outHeight) || !"image/png".equals(bounds.outMimeType)) return null;
            return BitmapFactory.decodeFile(file.getPath());
        } catch (IOException invalid) { return null; }
    }

    static boolean validSize(int width, int height) {
        return width >= MIN_SIDE && height >= MIN_SIDE && width <= MAX_SIDE && height <= MAX_SIDE;
    }

    /** Downloads at once; RA serves badges from a CDN, so a few parallel requests stay light. */
    static final int PARALLEL = 6, PROGRESS_EVERY = 60;

    /** Fetches missing pictures for [names]; returns how many were added. Failures are skipped. */
    public int fetchMissing(Collection<String> names) { return fetchMissing(names, null); }

    /**
     * As above, [PARALLEL] at a time over reused HTTPS connections; [progress] runs (on a download
     * thread) after every [PROGRESS_EVERY] new pictures so screens can fill in while the rest arrive.
     */
    public int fetchMissing(Collection<String> names, Runnable progress) {
        if (!root.isDirectory() && !root.mkdirs()) return 0;
        List<String> missing = new ArrayList<>();
        for (String name : new LinkedHashSet<>(names)) {
            if (missing.size() >= MAX_PER_IMPORT) break;
            try { if (!file(name).isFile()) missing.add(name); } catch (IOException skipped) { /* unsafe name: never fetched */ }
        }
        java.util.concurrent.atomic.AtomicInteger added = new java.util.concurrent.atomic.AtomicInteger();
        java.util.concurrent.ExecutorService pool = java.util.concurrent.Executors.newFixedThreadPool(PARALLEL, r -> {
            Thread thread = new Thread(r, "RaBadgeDownload"); thread.setDaemon(true); return thread;
        });
        try {
            for (String name : missing) pool.execute(() -> {
                if (Thread.currentThread().isInterrupted()) return;
                try {
                    if (fetch(name) && added.incrementAndGet() % PROGRESS_EVERY == 0 && progress != null) progress.run();
                } catch (IOException | RuntimeException skipped) { /* fallback art is drawn for this badge */ }
            });
            pool.shutdown();
            if (!pool.awaitTermination(10, java.util.concurrent.TimeUnit.MINUTES)) pool.shutdownNow();
        } catch (InterruptedException interrupted) {
            pool.shutdownNow();
            Thread.currentThread().interrupt();
        }
        prune();
        return added.get();
    }

    private boolean fetch(String name) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(RaProgress.badgeUrl(name)).openConnection();
        connection.setInstanceFollowRedirects(false);
        connection.setConnectTimeout(10000); connection.setReadTimeout(10000);
        connection.setRequestProperty("User-Agent", "WiiDS-ProgressMirror/0.1");
        connection.setRequestProperty("Accept", "image/png");
        boolean keep = false;
        try {
            if (connection.getResponseCode() != 200) return false;
            String type = connection.getContentType();
            if (type == null || !type.toLowerCase(Locale.ROOT).startsWith("image/png")) return false;
            if (connection.getContentLengthLong() > MAX_BYTES) return false;
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                while ((n = input.read(buffer)) != -1) {
                    if (bytes.size() + n > MAX_BYTES) return false;
                    bytes.write(buffer, 0, n);
                }
            }
            // Fully read and closed: the HTTPS connection goes back to the pool for the next badge.
            keep = true;
            byte[] data = bytes.toByteArray();
            if (!png(data)) return false;
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(data, 0, data.length, bounds);
            if (!validSize(bounds.outWidth, bounds.outHeight)) return false;
            // A cache file: written whole then moved into place (no per-file sync; a lost picture is fetched again).
            Path temporary = Files.createTempFile(root.toPath(), ".badge-", ".tmp");
            try {
                try (FileOutputStream output = new FileOutputStream(temporary.toFile())) { output.write(data); }
                Files.move(temporary, file(name).toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } finally { Files.deleteIfExists(temporary); }
            return true;
        } finally { if (!keep) connection.disconnect(); }
    }

    static boolean png(byte[] data) {
        byte[] signature = {(byte)0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        if (data == null || data.length < signature.length) return false;
        for (int i = 0; i < signature.length; i++) if (data[i] != signature[i]) return false;
        return true;
    }

    /** Oldest pictures go first once the cache holds more than MAX_FILES. */
    private void prune() {
        File[] files = root.listFiles((dir, name) -> name.matches("[0-9]{1,12}\\.png"));
        if (files == null || files.length <= MAX_FILES) return;
        Arrays.sort(files, Comparator.comparingLong(File::lastModified));
        for (int i = 0; i < files.length - MAX_FILES; i++) //noinspection ResultOfMethodCallIgnored
            files[i].delete();
    }

    /** Disconnect removes imported presentation along with the history. */
    public void clear() {
        File[] files = root.listFiles();
        if (files != null) for (File file : files) //noinspection ResultOfMethodCallIgnored
            file.delete();
    }
}
