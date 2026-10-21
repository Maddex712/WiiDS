package com.driftdroid.android.dualscreen;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import com.driftdroid.android.ThorLowerStatus;
import com.driftdroid.android.ThorLowerStatusView;
import org.json.JSONObject;
import org.json.JSONArray;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

/** One read-only race map. HUD pixels and glyphs come from the original Wii UI. */
public final class ThorRaceMapView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF rect = new RectF();
    private final PorterDuffColorFilter shadow = new PorterDuffColorFilter(Color.BLACK, PorterDuff.Mode.SRC_IN);
    private final PorterDuffColorFilter yellow = new PorterDuffColorFilter(Color.YELLOW, PorterDuff.Mode.SRC_IN);
    private final float[] incoming = new float[RaceMapBridge.SIZE];
    private final float[] latest = new float[RaceMapBridge.SIZE];
    private final float[] previous = new float[RaceMapBridge.SIZE];
    private final float[] state = new float[RaceMapBridge.SIZE];
    private final float[] ranks = new float[12], rankFrom = new float[12], rankTarget = new float[12];
    private final long[] rankStarted = new long[12];
    private final Bitmap[] items = new Bitmap[20];
    private final Bitmap[] mapFaces = new Bitmap[24];
    private final File folder;
    private final boolean retroRewind;
    private final String expectedCodePulSha;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService loader = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "ThorMapAssets"); t.setDaemon(true); return t;
    });
    private final ThorLowerStatusView lowerStatus;
    private boolean nativeRaceAvailable, hudFailed, courseLoading, courseFailed;
    private ThorLowerStatus.Phase lowerPhase = ThorLowerStatus.Phase.WAITING;
    private ThorWiiHudAssets hud;
    private Bitmap course;
    private int requestedCourse = -1, lastTimer = -1;
    private String requestedIdentity = "";
    private int courseEpoch;
    private boolean active, closed, haveSnapshot;
    private float x0, z0, x1, z1;
    private long sampleTime, lastFrameTime;
    private double samplePeriod = 16_666_667;
    // Read by the separate test instrumentation; no timing overlay in gameplay.
    private long callbackCount, drawCount, slowFrameCount, maxFrameNanos;
    private final int[] timerDigits = new int[7];
    private static final float[] TIMER_POSITIONS = {0, 24, 60, 84, 119, 142, 166};
    private static final Pattern COURSE_IDENTITY = Pattern.compile("[A-Za-z0-9_-]+:[0-9a-fA-F]{64}");

    public ThorRaceMapView(Context context, File folder) {
        this(context, folder, false, "");
    }

    public ThorRaceMapView(Context context, File folder, boolean retroRewind, String expectedCodePulSha) {
        super(context);
        this.folder = folder;
        this.retroRewind = retroRewind;
        this.expectedCodePulSha = expectedCodePulSha == null ? "" : expectedCodePulSha;
        lowerStatus = new ThorLowerStatusView(context, folder, retroRewind ? "Retro Rewind" : "Mario Kart Wii");
        lowerStatus.setOnContentChanged(this::invalidate);
        setContentDescription("Mario Kart Wii course map, race timer and live standings");
        loader.execute(() -> {
            try {
                ThorWiiHudAssets nativeHud = new ThorWiiHudAssets(folder);
                Bitmap[] held = new Bitmap[items.length];
                Bitmap[] faces = new Bitmap[mapFaces.length];
                for (int i = 0; i < faces.length; i++) {
                    faces[i] = BitmapFactory.decodeFile(new File(folder, String.format(Locale.ROOT, "face_%02d.png", i)).getPath());
                    if (faces[i] == null) throw new IllegalArgumentException("Missing Wii map face " + i);
                }
                for (int i = 0; i < held.length; i++) {
                    held[i] = BitmapFactory.decodeFile(new File(folder, String.format(Locale.ROOT, "item_%02d.png", i)).getPath());
                    if (held[i] == null) throw new IllegalArgumentException("Missing held-item art " + i);
                }
                handler.post(() -> {
                    if (closed) return;
                    hud = nativeHud;
                    System.arraycopy(held, 0, items, 0, items.length);
                    System.arraycopy(faces, 0, mapFaces, 0, mapFaces.length);
                    refreshLowerStatus();
                    invalidate();
                });
            } catch (Exception e) {
                Log.e("ThorRaceMap", "Could not load original Wii HUD", e);
                handler.post(() -> { if (!closed) { hudFailed = true; refreshLowerStatus(); invalidate(); } });
            }
        });
    }

    public void update(long frameTime) {
        if (closed) return;
        callbackCount++;
        if (lastFrameTime != 0) {
            long delta = frameTime - lastFrameTime;
            maxFrameNanos = Math.max(maxFrameNanos, delta);
            if (delta > 25_000_000) slowFrameCount++;
        }
        lastFrameTime = frameTime;
        boolean valid = RaceMapBridge.nativeRead(incoming) && incoming[0] == 1;
        nativeRaceAvailable = valid;
        boolean identityChanged = false;
        if (retroRewind) {
            String identity = valid ? RaceMapBridge.nativeCourseIdentity() : "";
            if (identity == null || !COURSE_IDENTITY.matcher(identity).matches()) identity = "";
            identityChanged = !identity.equals(requestedIdentity);
            if (identityChanged) {
                requestedIdentity = identity;
                courseEpoch++;
                course = null;
                courseLoading = false;
                courseFailed = false;
                haveSnapshot = false;
                lastTimer = -1;
                // Never suppress the original HUD using a bitmap from the previous track.
                RaceMapBridge.nativeMapVisible(false);
                if (!identity.isEmpty()) loadCustomCourse(identity, courseEpoch);
            }
            valid = valid && !identity.isEmpty();
        }
        boolean reset = identityChanged || !haveSnapshot || !active || !valid || incoming[2] != latest[2]
                || incoming[7] < latest[7] || incoming[7] - latest[7] > 6;
        active = valid;
        if (valid) {
            if (reset) {
                System.arraycopy(incoming, 0, previous, 0, incoming.length);
                System.arraycopy(incoming, 0, latest, 0, incoming.length);
                sampleTime = frameTime;
                for (int i = 0; i < 12; i++) {
                    float rank = incoming[RaceMapBridge.HEADER + i * RaceMapBridge.STRIDE + 2];
                    ranks[i] = rankFrom[i] = rankTarget[i] = rank;
                    rankStarted[i] = frameTime;
                }
            } else if (incoming[7] != latest[7]) {
                samplePeriod = Math.max(16_666_667, Math.min(100_000_000,
                        (incoming[7] - latest[7]) * 16_666_667.0));
                System.arraycopy(latest, 0, previous, 0, latest.length);
                sampleTime = frameTime;
            }
            System.arraycopy(incoming, 0, latest, 0, incoming.length);
            System.arraycopy(latest, 0, state, 0, latest.length);
            haveSnapshot = true;
            float alpha = (float) Math.min(1, Math.max(0, (frameTime - sampleTime) / samplePeriod));
            for (int i = 0; i < 12; i++) {
                int at = RaceMapBridge.HEADER + i * RaceMapBridge.STRIDE;
                float target = latest[at + 2];
                if (target != rankTarget[i]) {
                    rankFrom[i] = ranks[i]; rankTarget[i] = target; rankStarted[i] = frameTime;
                }
                float t = Math.min(1, (frameTime - rankStarted[i]) / 160_000_000f);
                float ease = t * t * (3 - 2 * t);
                ranks[i] = rankFrom[i] + (rankTarget[i] - rankFrom[i]) * ease;
                if (previous[at] == 0 || latest[at] == 0) continue;
                float dx = latest[at + 4] - previous[at + 4], dz = latest[at + 5] - previous[at + 5];
                if (dx * dx + dz * dz > 16_000_000) continue;
                state[at + 4] = previous[at + 4] + dx * alpha;
                state[at + 5] = previous[at + 5] + dz * alpha;
                float turn = latest[at + 6] - previous[at + 6];
                while (turn > Math.PI) turn -= (float) (2 * Math.PI);
                while (turn < -Math.PI) turn += (float) (2 * Math.PI);
                state[at + 6] = previous[at + 6] + turn * alpha;
            }
            int id = (int) latest[2];
            if (!retroRewind && id != requestedCourse) loadCourse(id);
        } else haveSnapshot = false;
        boolean visible = active && course != null && hud != null;
        RaceMapBridge.nativeMapVisible(visible);
        refreshLowerStatus();
        // Still sample at the controller's existing cadence, but don't redraw a
        // static idle/error page each vsync. Lifecycle stop removes callbacks.
        if (visible) invalidate();
    }

    private void refreshLowerStatus() {
        ThorLowerStatus.Phase phase = ThorLowerStatus.resolve(nativeRaceAvailable, (int) incoming[3],
                !retroRewind || !requestedIdentity.isEmpty(), hud != null, hudFailed,
                course != null, courseLoading, courseFailed);
        if (phase != lowerPhase) {
            lowerPhase = phase;
            lowerStatus.setStatus(phase.getTitle(), phase.getMessage());
            invalidate();
        }
    }

    public void close() {
        if (closed) return;
        closed = true; courseEpoch++; lowerStatus.close();
        RaceMapBridge.nativeMapVisible(false); loader.shutdownNow();
    }

    private void loadCourse(int id) {
        requestedCourse = id; course = null; courseLoading = true; courseFailed = false;
        final int token = ++courseEpoch;
        Log.i("ThorRaceMap", "course=" + id + " snapshot=" + Arrays.toString(latest));
        loader.execute(() -> {
            try {
                String key = String.format(Locale.ROOT, "course_%02d", id);
                JSONObject info = new JSONObject(new String(Files.readAllBytes(new File(folder, key + ".json").toPath()), StandardCharsets.UTF_8));
                JSONArray frame = info.getJSONArray("frame");
                float[] bounds = new float[4];
                for (int i = 0; i < 4; i++) bounds[i] = (float) frame.getDouble(i);
                Bitmap image = BitmapFactory.decodeFile(new File(folder, key + ".png").getPath());
                if (image == null || bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) throw new IllegalArgumentException("Invalid map assets");
                handler.post(() -> {
                    if (closed || requestedCourse != id || courseEpoch != token) return;
                    course = image; courseLoading = false; courseFailed = false;
                    x0 = bounds[0]; z0 = bounds[1]; x1 = bounds[2]; z1 = bounds[3];
                    refreshLowerStatus(); invalidate();
                });
            } catch (Exception e) {
                Log.e("ThorRaceMap", "Could not load course " + id, e);
                handler.post(() -> {
                    if (!closed && courseEpoch == token && requestedCourse == id) {
                        courseLoading = false; courseFailed = true; refreshLowerStatus(); invalidate();
                    }
                });
            }
        });
    }

    private static JSONObject readJson(File file) throws Exception {
        return new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
    }

    private static float[] customBounds(JSONObject info) throws Exception {
        JSONArray frame = info.getJSONArray("frame");
        if (frame.length() != 4) throw new IllegalArgumentException("Invalid custom map bounds");
        float[] bounds = new float[4];
        for (int i = 0; i < bounds.length; i++) {
            bounds[i] = (float) frame.getDouble(i);
            if (!Float.isFinite(bounds[i])) throw new IllegalArgumentException("Nonfinite custom map bounds");
        }
        if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1])
            throw new IllegalArgumentException("Inverted custom map bounds");
        return bounds;
    }

    private static void checkCustomIdentity(JSONObject info, String key, String hash) throws Exception {
        if (info.getInt("schema") != 1 || !"retro_rewind".equals(info.getString("profile"))
                || !key.equals(info.getString("key"))
                || !hash.equalsIgnoreCase(info.getString("source_sha256"))
                || !(key + ".szs").equals(info.getString("archive_name"))
                || !("/Race/Course/" + key + ".szs").equals(info.getString("dvd_path"))
                || !("course_" + key + ".png").equals(info.getString("png"))
                || !("course_" + key + ".json").equals(info.getString("receipt")))
            throw new IllegalArgumentException("Custom map archive identity mismatch");
    }

    private void loadCustomCourse(String identity, int token) {
        courseLoading = true; courseFailed = false;
        Log.i("ThorRaceMap", "RR course=" + identity);
        loader.execute(() -> {
            try {
                String[] parts = identity.split(":", 2);
                String key = parts[0], sourceHash = parts[1];
                File customFolder = new File(folder, "retro_rewind");
                JSONObject manifest = readJson(new File(customFolder, "maps.json"));
                if (manifest.getInt("schema") != 1 || !"retro_rewind".equals(manifest.getString("profile"))
                        || !expectedCodePulSha.matches("[0-9a-fA-F]{64}")
                        || !expectedCodePulSha.equalsIgnoreCase(manifest.getString("code_pul_sha256")))
                    throw new IllegalArgumentException("Retro Rewind map profile/Code.pul mismatch");
                JSONObject record = manifest.getJSONObject("courses").getJSONObject(key);
                checkCustomIdentity(record, key, sourceHash);
                JSONObject info = readJson(new File(customFolder, record.getString("receipt")));
                checkCustomIdentity(info, key, sourceHash);
                float[] bounds = customBounds(info);
                if (!Arrays.equals(bounds, customBounds(record)))
                    throw new IllegalArgumentException("Custom map receipt/manifest bounds mismatch");
                String pngHash = record.getString("png_sha256");
                if (!pngHash.matches("[0-9a-fA-F]{64}") || !pngHash.equalsIgnoreCase(info.getString("png_sha256")))
                    throw new IllegalArgumentException("Custom map receipt/manifest PNG mismatch");
                byte[] pixels = Files.readAllBytes(new File(customFolder, record.getString("png")).toPath());
                byte[] digest = MessageDigest.getInstance("SHA-256").digest(pixels);
                StringBuilder actualHash = new StringBuilder(64);
                for (byte value : digest) actualHash.append(String.format(Locale.ROOT, "%02x", value & 255));
                if (!pngHash.equalsIgnoreCase(actualHash.toString()))
                    throw new IllegalArgumentException("Custom map PNG hash mismatch");
                Bitmap image = BitmapFactory.decodeByteArray(pixels, 0, pixels.length);
                JSONArray size = record.getJSONArray("size"), receiptSize = info.getJSONArray("size");
                if (image == null || size.length() != 2 || receiptSize.length() != 2
                        || image.getWidth() != size.getInt(0) || image.getHeight() != size.getInt(1)
                        || image.getWidth() != receiptSize.getInt(0) || image.getHeight() != receiptSize.getInt(1))
                    throw new IllegalArgumentException("Custom map PNG dimensions mismatch");
                handler.post(() -> {
                    if (closed || courseEpoch != token || !requestedIdentity.equals(identity)) {
                        image.recycle();
                        return;
                    }
                    course = image; courseLoading = false; courseFailed = false;
                    x0 = bounds[0]; z0 = bounds[1]; x1 = bounds[2]; z1 = bounds[3];
                    refreshLowerStatus(); invalidate();
                });
            } catch (Exception e) {
                Log.e("ThorRaceMap", "Could not load verified RR course " + identity, e);
                handler.post(() -> {
                    if (!closed && courseEpoch == token && requestedIdentity.equals(identity)) {
                        courseLoading = false; courseFailed = true; refreshLowerStatus(); invalidate();
                    }
                });
            }
        });
    }

    @Override protected void onDraw(Canvas canvas) {
        drawCount++;
        if (!active || course == null || hud == null) {
            lowerStatus.render(canvas, getWidth(), getHeight());
            return;
        }
        canvas.drawColor(Color.BLACK);
        float width = getWidth(), height = getHeight(), unit = height / 240f;
        float board = Math.min(width * .38f, unit * 80f);
        int player = Math.max(0, Math.min(11, (int) latest[5]));
        int me = RaceMapBridge.HEADER + player * RaceMapBridge.STRIDE;
        float cx = state[me] != 0 ? state[me + 4] : (x0 + x1) * .5f;
        float cz = state[me] != 0 ? state[me + 5] : (z0 + z1) * .5f;
        float scale = unit / 64f;
        float centerX = board + (width - board) * .5f, centerY = height * .57f;
        canvas.save(); canvas.clipRect(board, 0, width, height);
        rect.set(centerX + (x0 - cx) * scale, centerY + (z0 - cz) * scale,
                 centerX + (x1 - cx) * scale, centerY + (z1 - cz) * scale);
        paint.setColor(Color.WHITE); canvas.drawBitmap(course, null, rect, paint);
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < Math.min(12, (int) state[6]); i++) {
                int at = RaceMapBridge.HEADER + i * RaceMapBridge.STRIDE;
                if (state[at] == 0 || ((i == player) != (pass == 1))) continue;
                marker(canvas, centerX + (state[at + 4] - cx) * scale,
                       centerY + (state[at + 5] - cz) * scale,
                       (int) state[at + 1], i == player, unit, state[at + 6]);
            }
        }
        canvas.restore();
        standings(canvas, board, height, player);
        timer(canvas, board, width, unit);
    }

    private void marker(Canvas canvas, float x, float y, int character, boolean local, float unit, float heading) {
        if (character < 0 || character >= mapFaces.length) return;
        float size = (local ? 16 : 13) * unit;
        if (local) {
            canvas.save();
            if (Float.isFinite(heading)) canvas.rotate((float) Math.toDegrees(heading), x, y);
            paint.setColorFilter(yellow);
            rect.set(x - size * .70f, y - size * 1.5f, x + size * .70f, y);
            canvas.drawBitmap(hud.searchlight, null, rect, paint);
            paint.setColorFilter(null); canvas.restore();
        }
        paint.setColorFilter(shadow);
        icon(canvas, mapFaces[character], x - size * .047f, y, size * 1.25f);
        icon(canvas, mapFaces[character], x + size * .047f, y, size * 1.25f);
        paint.setColorFilter(null);
        icon(canvas, mapFaces[character], x, y, size);
    }

    private void standings(Canvas canvas, float width, float height, int player) {
        float row = height / 12f;
        paint.setColor(Color.rgb(22, 29, 42));
        canvas.drawRect(0, 0, width, height, paint);
        paint.setColor(Color.WHITE);
        for (int rank = 0; rank < 12; rank++) {
            rect.set(0, rank * row, width, (rank + 1) * row);
            canvas.drawBitmap(hud.normalRow, null, rect, paint);
        }
        if (ranks[player] >= 1 && ranks[player] <= 12) {
            float top = (ranks[player] - 1) * row;
            rect.set(0, top, width, top + row);
            canvas.drawBitmap(hud.selectedRow, null, rect, paint);
        }
        for (int rank = 0; rank < 12; rank++) {
            Bitmap label = hud.ranks[rank];
            float h = row * .8925f, w = label.getWidth() * h / label.getHeight();
            float x = width * .055f, y = rank * row + (row - h) * .5f;
            rect.set(x, y, x + w, y + h);
            canvas.drawBitmap(label, null, rect, paint);
        }
        canvas.save(); canvas.clipRect(0, 0, width, height);
        for (int i = 0; i < Math.min(12, (int) latest[6]); i++) {
            int at = RaceMapBridge.HEADER + i * RaceMapBridge.STRIDE;
            if (latest[at] == 0 || ranks[i] < 1 || ranks[i] > 12) continue;
            float center = (ranks[i] - .5f) * row;
            int character = (int) latest[at + 1], item = (int) latest[at + 7];
            if (character >= 0 && character < hud.portraits.length)
                icon(canvas, hud.portraits[character], width * .53f, center, row * .77f);
            if (item >= 0 && item < items.length) icon(canvas, items[item], width * .83f, center, row * .68f);
        }
        canvas.restore();
    }

    private void timer(Canvas canvas, float board, float width, float unit) {
        int ms = Math.max(0, (int) latest[4]);
        if (ms != lastTimer) {
            lastTimer = ms;
            timerDigits[0] = ms / 600000 % 10; timerDigits[1] = ms / 60000 % 10;
            timerDigits[2] = ms / 10000 % 6; timerDigits[3] = ms / 1000 % 10;
            timerDigits[4] = ms / 100 % 10; timerDigits[5] = ms / 10 % 10; timerDigits[6] = ms % 10;
        }
        float scale = Math.min(unit * .45f, (width - board - unit * 8) / 363f);
        float origin = board + (width - board) * .5f, top = unit * 7;
        timerSprite(canvas, hud.timeLabel, origin, top, -97, 168, 28, scale);
        for (int i = 0; i < timerDigits.length; i++)
            timerSprite(canvas, hud.digits[timerDigits[i]], origin, top, TIMER_POSITIONS[i], 32, 32, scale);
        timerSprite(canvas, hud.colon, origin, top, 41, 32, 32, scale);
        timerSprite(canvas, hud.dot, origin, top, 100, 32, 32, scale);
    }

    private void timerSprite(Canvas canvas, Bitmap image, float origin, float top, float nativeX, float w, float h, float scale) {
        rect.set(origin + (nativeX - w * .5f) * scale, top, origin + (nativeX + w * .5f) * scale, top + h * scale);
        canvas.drawBitmap(image, null, rect, paint);
    }

    private void icon(Canvas canvas, Bitmap image, float x, float y, float size) {
        float scale = size / Math.max(image.getWidth(), image.getHeight());
        float hw = image.getWidth() * scale * .5f, hh = image.getHeight() * scale * .5f;
        rect.set(x - hw, y - hh, x + hw, y + hh);
        paint.setColor(Color.WHITE); canvas.drawBitmap(image, null, rect, paint);
    }
}
