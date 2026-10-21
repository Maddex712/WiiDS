package dev.chasem.smslauncher;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The AYN Thor's lower screen while Super Mario Sunshine (or Super Mario Eclipse) runs: the area's
 * name and episode, its postcard beside a live map of the area with Mario on it, the Shine,
 * blue-coin and coin counters laid out as the top HUD draws them (the top screen leaves them out
 * while this screen shows them, SMS_THOR_SPLIT_HUD), and the area's eight episode Shines. Drawn
 * with the owner's own HUD art (staged privately to files/ThorHud, never packaged); the map is the
 * game's own collision floor and the postcard, where the disc has none, a photo of the area the
 * game took on arrival. Values come only from the game's per-frame snapshot (smsbootstrap
 * nativeSnapshot); nothing unknown is drawn. Display only: the window is not focusable or
 * touchable, so the controller keeps driving the game.
 */
final class ThorSunshineScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "SMS-ThorHUD";
    private static final float W = 1240, H = 1080;
    // TApplication::mAppState values (System/Application.hpp).
    private static final int APP_GAMEPLAY = 5, APP_TITLE = 8, APP_MENU = 9;
    // BSE application.hxx / Eclipse menu/character_select.hxx, at the pinned mod revisions.
    private static final int APP_SETTINGS = 10, APP_CHARACTER_SELECT = 11;

    static native int nativeSnapshot(int[] out);
    /** The title's 3D sky as the game drew it (640x360 RGBA, rows top first); 0 when not newer than [have]. */
    static native int nativeSky(java.nio.ByteBuffer out, int have);
    /** The area's photo (same format as the sky), taken before the HUD; 0 when not newer than [have]. */
    static native int nativePhoto(java.nio.ByteBuffer out, int have);
    /** The area's floor triangles (11 floats each); the count, with the export's sequence in seq[0]. */
    static native int nativeFloor(java.nio.ByteBuffer out, int have, int[] seq);
    private static final int SKY_W = 640, SKY_H = 360;
    private final java.nio.ByteBuffer skyPixels = java.nio.ByteBuffer.allocateDirect(SKY_W * SKY_H * 4);
    private final Bitmap sky = Bitmap.createBitmap(SKY_W, SKY_H, Bitmap.Config.ARGB_8888);
    private int skySeq;
    private final java.nio.ByteBuffer photoPixels = java.nio.ByteBuffer.allocateDirect(SKY_W * SKY_H * 4);
    private int photoSeq;
    private Bitmap photo;        // the current area's photo (the picture part, 16:9)
    private String photoScene = "";
    private static final int FLOOR_MAX = 40000, FLOOR_FLOATS = 11;
    private java.nio.ByteBuffer floorBytes;
    private final int[] floorSeqOut = new int[1];
    private int floorSeq;
    private boolean mapBuilding;
    private AreaMap areaMap;

    private final Activity activity;
    /** Super Mario Eclipse's own app: its night theme (its title sky is a starry one). */
    private final boolean eclipse;
    private final DisplayManager displays;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Map<String, Bitmap> art = new HashMap<>();
    private static final int WORDS = 72, WORDS_V2 = 64;
    private final int[] words = new int[WORDS];
    private Snapshot shown;
    private Screen presentation;
    private boolean running, linkWarned;

    ThorSunshineScreen(Activity activity) {
        this.activity = activity;
        eclipse = activity.getPackageName().contains("eclipse");
        displays = activity.getSystemService(DisplayManager.class);
        File folder = new File(ThorStorage.root(activity), "ThorHud");
        new Thread(() -> {
            Map<String, Bitmap> loaded = new HashMap<>();
            String[] names = {"game_6-shine_icon", "game_6-coin_icon", "game_6-bluecoin_icon", "game_6-mario_icon",
                "game_6-coin_number_x", "game_6-coin_back", "select-sc_map_bianco", "select-sc_map_corona",
                "select-sc_map_mamma", "select-sc_map_mare", "select-sc_map_monte", "select-sc_map_pinna",
                "select-sc_map_ricco", "select-sc_map_sirena", "select-sc_question",
                "option-title_logo", "option-windows_kumo05", "option-stage_airport", "option-stage_dolpictown",
                "guide-guide_map_pinna",
                "guide-guide_picture_00", "guide-guide_picture_02", "guide-guide_picture_03", "guide-guide_picture_04",
                "guide-guide_picture_06", "guide-guide_picture_07", "guide-guide_picture_08", "guide-guide_picture_09",
                "guide-guide_picture_11", "select-sc_number_1", "select-sc_number_2", "select-sc_number_3",
                "select-sc_number_4", "select-sc_number_5", "select-sc_number_6", "select-sc_number_7", "select-sc_number_8"};
            for (String name : names) {
                Bitmap b = BitmapFactory.decodeFile(new File(folder, name + ".png").getPath());
                if (b != null) loaded.put(name, b);
            }
            for (int d = 0; d < 10; d++) {
                Bitmap b = BitmapFactory.decodeFile(new File(folder, "game_6-coin_number_" + d + ".png").getPath());
                if (b != null) loaded.put("digit" + d, b);
            }
            // The swoosh is an intensity texture: its brightness is the stroke's coverage.
            Bitmap back = loaded.get("game_6-coin_back");
            if (back != null) loaded.put("swoosh", luminanceAlpha(back));
            handler.post(() -> { art.putAll(loaded); invalidate(); Log.i(TAG, "Lower screen art: " + loaded.size() + " images"); });
        }, "SunshineHudArt").start();
    }

    /** True when this device has the Thor's own lower panel (where this screen goes). */
    static boolean hasLowerPanel(Activity activity) {
        DisplayManager displays = activity.getSystemService(DisplayManager.class);
        if (displays == null) return false;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION))
            if (display.isValid() && "Screen-2".equals(display.getName())) return true;
        return false;
    }

    private static Bitmap luminanceAlpha(Bitmap src) {
        int w = src.getWidth(), h = src.getHeight();
        int[] px = new int[w * h];
        src.getPixels(px, 0, w, 0, 0, w, h);
        for (int i = 0; i < px.length; i++) {
            int c = px[i];
            int l = (Color.red(c) * 77 + Color.green(c) * 151 + Color.blue(c) * 28) >> 8;
            px[i] = (l * Color.alpha(c) / 255) << 24 | 0xffffff;
        }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
    }

    void start() {
        if (running || displays == null) return;
        running = true;
        displays.registerDisplayListener(this, handler);
        refresh();
        handler.post(poll);
    }

    void stop() {
        if (!running) return;
        running = false;
        displays.unregisterDisplayListener(this);
        handler.removeCallbacks(poll);
        if (presentation != null) presentation.dismiss();
        presentation = null;
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!running) return;
            int n = 0;
            try { n = nativeSnapshot(words); }
            catch (UnsatisfiedLinkError notLoaded) { if (!linkWarned) { linkWarned = true; Log.w(TAG, "Snapshot bridge unavailable", notLoaded); } }
            Snapshot next = n >= WORDS_V2 && words[0] >= 2 ? new Snapshot(words, n) : null;
            try {
                int seq = nativeSky(skyPixels, skySeq);
                if (seq != 0) { skyPixels.rewind(); sky.copyPixelsFromBuffer(skyPixels); skyPixels.rewind(); skySeq = seq; }
            } catch (UnsatisfiedLinkError notLoaded) { /* reported once by the snapshot bridge */ }
            if (next != null) {
                pollPhoto(next);
                pollFloor(next);
            }
            boolean changed = next == null ? shown != null : !next.sameAs(shown);
            shown = next;
            if (changed && next != null) Log.i(TAG, "state stage=" + next.inStage + " scene=" + next.scene + " scenario=" + next.scenario
                + " shines=" + next.shines + " blue=" + next.blueCoins + " coins=" + next.coins
                + " area=" + next.shineStage + " '" + next.areaName + "' episode=" + next.episode + " '" + next.episodeName
                + "' held=" + Integer.toBinaryString(Math.max(0, next.held)) + " app=" + next.appState + " card=" + next.cardScreen
                + " split=" + next.split + " floor=" + next.floorSeq + " photo=" + next.photoSeq);
            handler.postDelayed(this, 33); // the game's fades and Mario's marker are followed closely
        }
    };

    private void pollPhoto(Snapshot s) {
        if (s.photoSeq <= 0 || s.photoSeq == photoSeq) return;
        try {
            int seq = nativePhoto(photoPixels, photoSeq);
            if (seq == 0) return;
            Bitmap raw = Bitmap.createBitmap(SKY_W, SKY_H, Bitmap.Config.ARGB_8888);
            photoPixels.rewind(); raw.copyPixelsFromBuffer(photoPixels); photoPixels.rewind();
            // The game draws 448 of the EFB's 528 lines (rows top first): that part is the picture.
            int rows = Math.round(SKY_H * 448f / 528f);
            photo = Bitmap.createBitmap(raw, 0, 0, SKY_W, rows);
            photoScene = s.scene;
            photoSeq = seq;
            Log.i(TAG, "Area photo for " + photoScene);
            invalidateBase();
        } catch (UnsatisfiedLinkError old) { photoSeq = s.photoSeq; }
    }

    private void pollFloor(Snapshot s) {
        if (mapBuilding || s.floorSeq <= 0 || s.floorSeq == floorSeq) return;
        try {
            if (floorBytes == null) floorBytes = java.nio.ByteBuffer.allocateDirect(FLOOR_MAX * FLOOR_FLOATS * 4).order(ByteOrder.nativeOrder());
            int count = nativeFloor(floorBytes, floorSeq, floorSeqOut);
            if (count <= 0) return;
            floorSeq = floorSeqOut[0];
            float[] tris = new float[count * FLOOR_FLOATS];
            FloatBuffer view = floorBytes.asFloatBuffer();
            view.get(tris, 0, tris.length);
            final String scene = s.scene;
            final boolean night = eclipse;
            mapBuilding = true;
            new Thread(() -> {
                long began = SystemClock.elapsedRealtime();
                AreaMap built = AreaMap.build(tris, count, MAP_W, MAP_H, night, scene);
                long took = SystemClock.elapsedRealtime() - began;
                handler.post(() -> {
                    mapBuilding = false;
                    if (built != null) areaMap = built;
                    Log.i(TAG, "Area map " + scene + ": " + count + " triangles, " + took + " ms" + (built == null ? " (empty)" : ""));
                    invalidateBase();
                });
            }, "SunshineAreaMap").start();
        } catch (UnsatisfiedLinkError old) { floorSeq = s.floorSeq; }
    }

    private void invalidate() { if (presentation != null) presentation.view.invalidate(); }
    private void invalidateBase() { if (presentation != null) presentation.view.baseKey = ""; invalidate(); }

    private void refresh() {
        if (!running) return;
        int upper = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        Display target = null;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            // The Thor's own lower panel only, never a cast or external display.
            if (display.isValid() && display.getDisplayId() != upper && "Screen-2".equals(display.getName())) { target = display; break; }
        }
        if (target != null && presentation != null && presentation.isShowing()
                && presentation.getDisplay().getDisplayId() == target.getDisplayId()) return;
        if (presentation != null) presentation.dismiss();
        presentation = null;
        if (target == null) return;
        Screen next = new Screen(target);
        try { next.show(); presentation = next; Log.i(TAG, "Lower screen attached display=" + target.getDisplayId()); }
        catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException | SecurityException unavailable) {
            Log.w(TAG, "Lower screen unavailable", unavailable);
        }
    }

    @Override public void onDisplayAdded(int id) { refresh(); }
    @Override public void onDisplayRemoved(int id) { refresh(); }
    @Override public void onDisplayChanged(int id) { refresh(); }

    /** One published frame of game state (layout: android/thor_snapshot.cpp in the native port). */
    static final class Snapshot {
        final boolean inStage, split;
        final int shines, blueCoins, coins, lives, water, nozzle, stage, scenario, shineStage, episode, held, appState, cardScreen, light;
        final int swooshWhite, swooshBlack, floorSeq, photoSeq;
        final float marioX, marioY, marioZ;
        final String scene, areaName, episodeName;
        Snapshot(int[] w, int n) {
            inStage = w[2] == 1; shines = w[3]; blueCoins = w[4]; coins = w[5]; lives = w[6];
            water = w[7]; nozzle = w[8]; stage = w[9]; scenario = w[10]; shineStage = w[11];
            episode = w[20]; held = w[21]; appState = w[22] & 0xff; cardScreen = (w[22] >> 8) - 1; light = w[23];
            scene = text(w, 12, 20); areaName = text(w, 24, 40); episodeName = text(w, 40, 64);
            boolean v3 = n >= 72;
            marioX = v3 ? Float.intBitsToFloat(w[64]) : 0; marioY = v3 ? Float.intBitsToFloat(w[65]) : 0;
            marioZ = v3 ? Float.intBitsToFloat(w[66]) : 0;
            swooshWhite = v3 ? w[67] : 0; swooshBlack = v3 ? w[68] : 0;
            floorSeq = v3 ? w[69] : 0; photoSeq = v3 ? w[70] : 0; split = v3 && w[71] == 1;
        }
        private static String text(int[] w, int from, int to) {
            StringBuilder b = new StringBuilder();
            for (int i = from; i < to; i++) for (int k = 0; k < 4; k++) {
                int c = (w[i] >> (8 * k)) & 0xFF;
                if (c == 0) return b.toString().trim();
                b.append((char) c);
            }
            return b.toString().trim();
        }
        /** True in the seven areas with eight episodes, where the screen shows Shine slots. */
        boolean tracked() { return held >= 0 && shineStage >= 2 && shineStage <= 8; }
        boolean sameAs(Snapshot o) {
            return o != null && inStage == o.inStage && shines == o.shines && blueCoins == o.blueCoins && coins == o.coins
                && lives == o.lives && scene.equals(o.scene) && scenario == o.scenario
                && appState == o.appState && cardScreen == o.cardScreen && shineStage == o.shineStage && episode == o.episode && held == o.held && areaName.equals(o.areaName)
                && episodeName.equals(o.episodeName) && split == o.split && floorSeq == o.floorSeq && photoSeq == o.photoSeq;
        }
    }

    /** The scene archive's area: plate image (if the disc has one) and a plain name. */
    static final class Area {
        final String plate, name; final boolean secret, episodes, retail;
        Area(String plate, String name, boolean secret, boolean episodes, boolean retail) {
            this.plate = plate; this.name = name; this.secret = secret; this.episodes = episodes; this.retail = retail;
        }
        static Area from(Snapshot state) {
            if (state == null) return null;
            // Eclipse stage.hxx adds stages 61-92. They must never borrow a retail postcard/plate.
            if (state.stage < 61) {
                Area retail = of(state.scene);
                if (retail != null) return retail;
            }
            if ((state.scene.isEmpty() && state.areaName.isEmpty()) || "option".equals(state.scene)) return null;
            String name = state.areaName.isEmpty() ? state.scene.replace('_', ' ') : state.areaName;
            return new Area(null, name, false, false, false);
        }
        static Area of(String scene) {
            if (scene == null || scene.isEmpty() || scene.equals("option") || scene.startsWith("test")) return null;
            String s = scene.toLowerCase(Locale.ROOT);
            boolean ex = s.contains("_ex");
            if (s.startsWith("airport")) return new Area(null, "Delfino Airstrip", false, false, true);
            if (s.startsWith("dolpic")) return new Area(null, "Delfino Plaza", ex, false, true);
            if (s.startsWith("bia")) return new Area("bianco", "Bianco Hills", ex, !ex, true);
            if (s.startsWith("ric")) return new Area("ricco", "Ricco Harbor", ex, !ex, true);
            if (s.startsWith("mam")) return new Area("mamma", "Gelato Beach", ex, !ex, true);
            if (s.startsWith("pinna")) return new Area("pinna", "Pinna Park", false, true, true);
            if (s.startsWith("sirena") || s.startsWith("delfino") || s.startsWith("casino")) return new Area("sirena", "Sirena Beach", ex, !ex, true);
            if (s.startsWith("mare")) return new Area("mare", "Noki Bay", ex, !ex, true);
            if (s.startsWith("monte")) return new Area("monte", "Pianta Village", ex, !ex, true);
            if (s.startsWith("coro")) return new Area("corona", "Corona Mountain", false, false, true);
            return null;
        }
    }

    // ---------------------------------------------------------------- the area map
    /** The map's picture size: the inside of the map panel. */
    static final int MAP_W = 720, MAP_H = 476;

    /**
     * A top-down map of the area from the game's own collision floor: water surfaces in water's
     * colour, ground shaded by height and slope, the sea around, a shallows glow and an outline.
     * Framed on the land (not the open sea); [k], [ox], [oz] map the game's x/z to its pixels.
     */
    static final class AreaMap {
        final Bitmap bitmap; final float k, ox, oz; final String scene;
        AreaMap(Bitmap bitmap, float k, float ox, float oz, String scene) { this.bitmap = bitmap; this.k = k; this.ox = ox; this.oz = oz; this.scene = scene; }

        static AreaMap build(float[] f, int n, int w, int h, boolean night, String scene) {
            final int F = FLOOR_FLOATS;
            // Giant flat triangles are out-of-bounds catch floors and ceilings, not ground.
            float[] area = new float[n];
            int land = 0;
            for (int i = 0; i < n; i++) {
                int o = i * F;
                area[i] = Math.abs((f[o + 2] - f[o]) * (f[o + 5] - f[o + 1]) - (f[o + 4] - f[o]) * (f[o + 3] - f[o + 1])) / 2;
                if (f[o + 10] == 0) land++;
            }
            if (land < 8) return null;
            float[] landAreas = new float[land];
            for (int i = 0, j = 0; i < n; i++) if (f[i * F + 10] == 0) landAreas[j++] = area[i];
            Arrays.sort(landAreas);
            float big = Math.max(4.0e7f, landAreas[Math.min(land - 1, (int) (land * 0.98f))] * 6);
            int kept = 0;
            Integer[] order = new Integer[n];
            for (int i = 0; i < n; i++) if (f[i * F + 10] != 0 || area[i] < big) order[kept++] = i;
            // Frame on the land, ignoring a sliver of outliers on each side.
            float[] xs = new float[kept * 3], zs = new float[kept * 3], ys = new float[kept];
            int nx = 0, ny = 0;
            for (int j = 0; j < kept; j++) {
                int o = order[j] * F;
                if (f[o + 10] != 0) continue;
                for (int v = 0; v < 3; v++) { xs[nx] = f[o + v * 2]; zs[nx] = f[o + v * 2 + 1]; nx++; }
                ys[ny++] = f[o + 6];
            }
            if (nx < 3) return null;
            float[] sx = Arrays.copyOf(xs, nx), sz = Arrays.copyOf(zs, nx), sy = Arrays.copyOf(ys, ny);
            Arrays.sort(sx); Arrays.sort(sz); Arrays.sort(sy);
            float minX = sx[(int) (nx * 0.004f)], maxX = sx[Math.min(nx - 1, (int) (nx * 0.996f))];
            float minZ = sz[(int) (nx * 0.004f)], maxZ = sz[Math.min(nx - 1, (int) (nx * 0.996f))];
            float lo = sy[(int) (ny * 0.03f)], hi = sy[Math.min(ny - 1, (int) (ny * 0.97f))];
            float k = Math.min(w * 0.9f / Math.max(1, maxX - minX), h * 0.88f / Math.max(1, maxZ - minZ));
            float ox = w / 2f - (minX + maxX) / 2 * k, oz = h / 2f - (minZ + maxZ) / 2 * k;
            Integer[] sorted = Arrays.copyOf(order, kept);
            Arrays.sort(sorted, (a, b) -> Float.compare(f[a * F + 6], f[b * F + 6]));

            int sea = night ? 0xff283480 : 0xff469cde, shallows = night ? 0xff5a64a0 : 0xff74b5e7;
            int outline = night ? 0xff101038 : 0xff24406e, water = night ? 0xff567cdc : 0xff60b2f0;
            int low = night ? 0xffc4b2ec : 0xfff6de96, mid = night ? 0xff84a0f0 : 0xff62ba48, high = night ? 0xffeee8ff : 0xffece6d2;
            final int S = 2;  // drawn at twice the size, then filtered down: smooth edges
            Bitmap ground = Bitmap.createBitmap(w * S, h * S, Bitmap.Config.ARGB_8888);
            Canvas g = new Canvas(ground);
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
            Path tri = new Path();
            float lx = -0.45f, ly = 0.8f, lz = -0.4f;
            for (Integer idx : sorted) {
                int o = idx * F;
                tri.reset();
                tri.moveTo((f[o] * k + ox) * S, (f[o + 1] * k + oz) * S);
                tri.lineTo((f[o + 2] * k + ox) * S, (f[o + 3] * k + oz) * S);
                tri.lineTo((f[o + 4] * k + ox) * S, (f[o + 5] * k + oz) * S);
                tri.close();
                if (f[o + 10] != 0) { p.setColor(water); g.drawPath(tri, p); continue; }
                float t = Math.max(0, Math.min(1, (f[o + 6] - lo) / Math.max(1, hi - lo)));
                int base = t < 0.45f ? mix(low, mid, Math.min(1, t * 2.2f)) : mix(mid, high, (t - 0.45f) / 0.55f);
                float sign = f[o + 8] > 0 ? 1 : -1;
                float shade = 0.6f + 0.45f * Math.max(0, sign * f[o + 7] * lx + Math.abs(f[o + 8]) * ly + sign * f[o + 9] * lz);
                p.setColor(scale(base, shade));
                g.drawPath(tri, p);
            }
            Bitmap small = Bitmap.createScaledBitmap(ground, w, h, true);
            ground.recycle();
            Bitmap out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(out);
            c.drawColor(sea);
            Paint glow = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
            int[] at = new int[2];
            glow.setMaskFilter(new BlurMaskFilter(16, BlurMaskFilter.Blur.NORMAL));
            Bitmap halo = small.extractAlpha(glow, at);
            Paint tint = new Paint(Paint.FILTER_BITMAP_FLAG); tint.setColor(shallows);
            c.drawBitmap(halo, at[0], at[1], tint); c.drawBitmap(halo, at[0], at[1], tint);
            glow.setMaskFilter(new BlurMaskFilter(3, BlurMaskFilter.Blur.OUTER));
            Bitmap rim = small.extractAlpha(glow, at);
            tint.setColor(outline);
            for (int i = 0; i < 3; i++) c.drawBitmap(rim, at[0], at[1], tint);
            c.drawBitmap(small, 0, 0, new Paint(Paint.FILTER_BITMAP_FLAG));
            small.recycle(); halo.recycle(); rim.recycle();
            return new AreaMap(out, k, ox, oz, scene);
        }

        private static int mix(int a, int b, float t) {
            return Color.rgb(Math.round(Color.red(a) + (Color.red(b) - Color.red(a)) * t),
                Math.round(Color.green(a) + (Color.green(b) - Color.green(a)) * t),
                Math.round(Color.blue(a) + (Color.blue(b) - Color.blue(a)) * t));
        }
        private static int scale(int c, float s) {
            return Color.rgb(Math.min(255, Math.round(Color.red(c) * s)), Math.min(255, Math.round(Color.green(c) * s)),
                Math.min(255, Math.round(Color.blue(c) * s)));
        }
    }

    private final class Screen extends Presentation {
        final Board view;
        Screen(Display display) {
            super(activity, display);
            view = new Board(getContext());
            if (eclipse) {
                // Eclipse: rasterize the lower artwork in software (its initial hardware drawing
                // path produced startup ANRs). The top SDL window and its renderer are unchanged.
                view.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
                Window lower = getWindow();
                if (lower != null) lower.clearFlags(WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED);
            }
        }
        @Override protected void onCreate(Bundle saved) {
            super.onCreate(saved);
            Window window = getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
            setContentView(view);
            if (window != null) window.setLayout(-1, -1);
        }
    }

    // Course layout (design canvas 1240x1080).
    private static final float POST_CX = 236, POST_CY = 436;           // the postcard, left of the map
    private static final float MAP_L = 452, MAP_T = 186, MAP_R = 1196, MAP_B = 686, MAP_BORDER = 12;
    private static final float COUNTERS_X = 40, COUNTERS_Y = 676, COUNTER_STEP = 100;
    private static final float GRID_X = 676, GRID_Y = 744;

    /**
     * Drawn only from the game's own 2D art and data: the title logo and clouds over Isle Delfino's
     * sky and sea; in a course the area's name and episode, its postcard and live map, the
     * counters in the top HUD's style and the episode Shines.
     */
    private final class Board extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint tinted = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final RectF r = new RectF();
        private final long born = SystemClock.uptimeMillis();
        private final Map<String, Bitmap> cache = new HashMap<>();
        private int cachedArt = -1;
        private boolean rendererReported;
        private float light;              // follows the game's fader: 0 black .. 1 clear
        private long fileSelectSince = -1; // when the file select appeared (its logo fades in)
        // The course screen's fixed parts (title, postcard, map panel, Shine grid) drawn once.
        private Bitmap base;
        String baseKey = "";
        private float markerX = Float.NaN, markerY;

        Board(Context context) {
            super(context); setFocusable(false); setClickable(false);
            text.setTypeface(Typeface.create("sans-serif-black", Typeface.NORMAL));
        }

        private int ink() { return eclipse ? 0xffeee8ff : 0xff1c2c60; }

        @Override protected void onDraw(Canvas c) {
            if (BuildConfig.DEBUG && !rendererReported) {
                rendererReported = true;
                Log.i(TAG, "lower first draw canvasHardware=" + c.isHardwareAccelerated()
                    + " windowHardware=" + isHardwareAccelerated() + " layerType=" + getLayerType());
            }
            if (cachedArt != art.size()) { cache.clear(); cachedArt = art.size(); baseKey = ""; }
            float scale = Math.min(getWidth() / W, getHeight() / H);
            c.save();
            c.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
            c.scale(scale, scale);
            float t = (SystemClock.uptimeMillis() - born) / 1000f;
            Snapshot s = shown;
            Area area = Area.from(s);
            // Phases follow the game: black through boot, the Nintendo logo and movies; the title's
            // sky fading in with the top screen; the logo only once the file select (A/B/C) is up.
            int state = s == null ? -1 : s.appState;
            boolean playing = state == APP_GAMEPLAY;
            boolean optionMap = playing && s.inStage && (s.stage == 15 || "option".equals(s.scene));
            // TCardLoad: 10, 9 and 3 are the "Press Start" title; from 8 (its logo fading out above)
            // the file select's logo fades in here.
            boolean pressStart = optionMap && (s.cardScreen < 0 || s.cardScreen == 3 || s.cardScreen == 9 || s.cardScreen == 10);
            boolean fileSelect = optionMap && !pressStart;
            boolean inCourse = playing && s.inStage && area != null && !optionMap;
            boolean eclipseMenu = state == APP_SETTINGS || state == APP_CHARACTER_SELECT;
            boolean sky = playing || state == APP_TITLE || state == APP_MENU || eclipseMenu;
            float target = sky ? Math.max(0, Math.min(255, s.light)) / 255f : 0;
            light += (target - light) * 0.4f;
            if (Math.abs(target - light) < 0.004f) light = target;
            long now = SystemClock.uptimeMillis();
            if (!fileSelect) fileSelectSince = -1;
            else if (fileSelectSince < 0) fileSelectSince = now;
            paint.setColor(0xff000000);
            if (light > 0.002f) {
                backdrop(c);
                if (inCourse) {
                    course(c, s, area, t);
                } else if (eclipseMenu) {
                    eclipseMenu(c, state);
                } else if (fileSelect) {
                    float appear = Math.min(1f, (now - fileSelectSince) / 1000f);
                    int layer = c.saveLayerAlpha(0, 0, W, H, (int) (255 * appear * appear * (3 - 2 * appear)));
                    title(c, t);
                    c.restoreToCount(layer);
                }
            }
            if (!inCourse) markerX = Float.NaN;
            if (light < 0.998f) { paint.setColor(Color.argb((int) (255 * (1 - light)), 0, 0, 0)); c.drawRect(0, 0, W, H, paint); }
            paint.setColor(0xff000000);
            c.restore();
            postInvalidateDelayed(33); // Mario's marker and the current episode move gently
        }

        /**
         * The sky behind everything: the game's own title sky (live on "Press Start", then its last
         * frame), cropped to this screen's shape around the sun. Until the game has drawn one, a
         * plain stand-in.
         */
        private void backdrop(Canvas c) {
            if (skySeq != 0) {
                // The game draws 448 of the EFB's 528 lines (rows top first); that part is the 16:9
                // picture. Crop it to this screen's shape on the sun's side.
                float rows = SKY_H * 448f / 528f, pixel = (16f / 9f * rows) / SKY_W;
                float cropW = (W / H) * rows / pixel;
                Rect from = new Rect((int) (SKY_W - cropW), 0, SKY_W, (int) rows);
                r.set(0, 0, W, H);
                paint.setColor(0xff000000);
                c.drawBitmap(sky, from, r, paint);
                return;
            }
            Bitmap stand = cached("sky", this::buildSky);
            if (stand != null) c.drawBitmap(stand, 0, 0, paint);
        }

        // ------------------------------------------------------------ cached scenery
        private Bitmap cached(String key, java.util.function.Supplier<Bitmap> build) {
            if (cache.containsKey(key)) return cache.get(key);
            Bitmap b = build.get();
            cache.put(key, b);
            return b;
        }

        /** Title-screen sky: the blue gradient, sun rays from the upper right, cumulus banks. */
        private Bitmap buildSky() {
            Bitmap out = Bitmap.createBitmap((int) W, (int) H, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(out);
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
            p.setShader(eclipse ? new LinearGradient(0, 0, 0, H, 0xff0a0a28, 0xff341e60, Shader.TileMode.CLAMP)
                : new LinearGradient(0, 0, 0, H * 0.75f, 0xff1c6cd6, 0xff78c4f5, Shader.TileMode.CLAMP));
            c.drawRect(0, 0, W, H, p); p.setShader(null);
            if (eclipse) {
                java.util.Random stars = new java.util.Random(7);
                for (int k = 0; k < 240; k++) {
                    p.setColor(Color.argb(90 + stars.nextInt(140), 255, 255, 255));
                    c.drawCircle(stars.nextInt((int) W), stars.nextInt((int) H), 1 + stars.nextInt(3), p);
                }
                return out;
            }
            p.setColor(0x22ffffff); p.setMaskFilter(new BlurMaskFilter(6, BlurMaskFilter.Blur.NORMAL));
            for (int k = 0; k < 9; k++) {
                double a = Math.toRadians(100 + k * 9);
                Path ray = new Path(); ray.moveTo(1080, -60);
                ray.lineTo(1080 + 1800 * (float) Math.cos(a - 0.03), -60 + 1800 * (float) Math.sin(a - 0.03));
                ray.lineTo(1080 + 1800 * (float) Math.cos(a + 0.03), -60 + 1800 * (float) Math.sin(a + 0.03));
                ray.close(); c.drawPath(ray, p);
            }
            p.setMaskFilter(null);
            p.setColor(0xffffffff); // the rays' faint white must not carry over to the clouds
            Bitmap puff = art.get("option-windows_kumo05");
            if (puff != null) {
                float[][] banks = {{150, 150, 1f}, {1040, 230, 1.15f}, {620, 70, 0.6f}, {300, 930, 1.2f}, {1050, 960, 1f}};
                float[][] puffs = {{-110, 20, 150}, {-30, -30, 190}, {70, -10, 170}, {150, 30, 130}, {20, 40, 160}};
                Paint shade = new Paint(p);
                shade.setColorFilter(new PorterDuffColorFilter(0xffaacdf0, PorterDuff.Mode.SRC_IN));
                for (int pass = 0; pass < 2; pass++)
                    for (float[] b : banks) for (float[] q : puffs) {
                        float size = q[2] * b[2], x = b[0] + q[0] * b[2], y = b[1] + q[1] * b[2] + (pass == 0 ? 10 : 0);
                        RectF d = new RectF(x - size / 2, y - size / 2, x + size / 2, y + size / 2);
                        c.drawBitmap(puff, null, d, pass == 0 ? shade : p);
                    }
            }
            return out;
        }

        /** A navy halo around an area plate so pale lettering reads on the sky. */
        private Bitmap buildHalo(String name) {
            Bitmap plate = art.get(name);
            if (plate == null) return null;
            Paint blur = new Paint(); blur.setMaskFilter(new BlurMaskFilter(6, BlurMaskFilter.Blur.NORMAL));
            int[] offset = new int[2];
            Bitmap alpha = plate.extractAlpha(blur, offset);
            Bitmap out = Bitmap.createBitmap(alpha.getWidth(), alpha.getHeight(), Bitmap.Config.ARGB_8888);
            Paint p = new Paint(); p.setColor(0xff0e3278);
            Canvas c = new Canvas(out);
            c.drawBitmap(alpha, 0, 0, p); c.drawBitmap(alpha, 0, 0, p);
            c.drawBitmap(plate, -offset[0], -offset[1], null);
            return out;
        }

        /** A Shine not yet collected: the same icon, grey and faint. */
        private Bitmap buildGhost() {
            Bitmap shine = art.get("game_6-shine_icon");
            if (shine == null) return null;
            Bitmap out = Bitmap.createBitmap(shine.getWidth(), shine.getHeight(), Bitmap.Config.ARGB_8888);
            ColorMatrix grey = new ColorMatrix(); grey.setSaturation(0);
            ColorMatrix dim = new ColorMatrix(); dim.setScale(0.75f, 0.75f, 0.8f, 0.6f);
            grey.postConcat(dim);
            Paint p = new Paint(Paint.FILTER_BITMAP_FLAG); p.setColorFilter(new ColorMatrixColorFilter(grey));
            new Canvas(out).drawBitmap(shine, 0, 0, p);
            return out;
        }

        // ------------------------------------------------------------ drawing helpers
        /** The game's dark-blue message window with its white rim. */
        private void window(Canvas c, float l, float tp, float rr, float b) {
            r.set(l, tp + 10, rr, b + 10);
            paint.setColor(0x3305143c); c.drawRoundRect(r, 30, 30, paint);
            r.set(l, tp, rr, b);
            paint.setColor(0xcd0c225c); c.drawRoundRect(r, 30, 30, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(5); paint.setColor(0xebffffff);
            r.set(l + 4, tp + 4, rr - 4, b - 4); c.drawRoundRect(r, 26, 26, paint);
            paint.setStyle(Paint.Style.FILL);
        }

        private void image(Canvas c, String name, float cx, float cy, float h) { image(c, art.get(name), cx, cy, h, 255); }

        private void image(Canvas c, Bitmap b, float cx, float cy, float h, int alpha) {
            if (b == null) return;
            float w = h * b.getWidth() / b.getHeight();
            r.set(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
            paint.setAlpha(alpha); c.drawBitmap(b, null, r, paint); paint.setAlpha(255);
        }

        /** Image fitted inside a box (height first, then width). */
        private void fitted(Canvas c, Bitmap b, float cx, float cy, float maxW, float maxH) {
            if (b == null) return;
            float h = maxH, w = h * b.getWidth() / b.getHeight();
            if (w > maxW) { w = maxW; h = w * b.getHeight() / b.getWidth(); }
            r.set(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
            c.drawBitmap(b, null, r, paint);
        }

        /** Outlined text, shrunk to fit [maxWidth]. */
        private void label(Canvas c, String value, float x, float y, float size, int color, Paint.Align align, float maxWidth) {
            text.setTextAlign(align); text.setTextSize(size);
            float width = text.measureText(value);
            if (width > maxWidth) { size *= maxWidth / width; text.setTextSize(size); }
            text.setStyle(Paint.Style.STROKE); text.setStrokeWidth(size / 7); text.setColor(0xff0b2350);
            c.drawText(value, x, y, text);
            text.setStyle(Paint.Style.FILL); text.setColor(color);
            c.drawText(value, x, y, text);
        }

        // ------------------------------------------------------------ screens
        /** Same backdrop and message-window styling for Eclipse's additional contexts. */
        private void eclipseMenu(Canvas c, int state) {
            image(c, "game_6-shine_icon", W / 2, 270, 140);
            window(c, 160, 390, W - 160, 650);
            label(c, "Super Mario Eclipse", W / 2, 490, 64, 0xffffffff, Paint.Align.CENTER, W - 400);
            label(c, state == APP_SETTINGS ? "Game settings" : "Character select",
                W / 2, 590, 46, 0xffffe27a, Paint.Align.CENTER, W - 400);
        }

        private void title(Canvas c, float t) {
            Bitmap logo = art.get("option-title_logo");
            if (logo != null) {
                paint.setColor(0x1e05143c); paint.setMaskFilter(new BlurMaskFilter(40, BlurMaskFilter.Blur.NORMAL));
                r.set(W / 2 - 280, 130, W / 2 + 280, 650); c.drawRoundRect(r, 200, 200, paint); paint.setMaskFilter(null);
                image(c, logo, W / 2, 380, 560, 255);
            } else {
                label(c, eclipse ? "Super Mario Eclipse" : "Super Mario Sunshine", W / 2, 400, 84, 0xffffffff, Paint.Align.CENTER, W - 200);
            }
            float[][] shines = {{150, 520}, {1090, 560}, {230, 640}, {1010, 660}};
            for (int k = 0; k < shines.length; k++)
                image(c, art.get("game_6-shine_icon"), shines[k][0], shines[k][1] + 8 * (float) Math.sin(t * 1.8f + k * 1.3f), 64, 255);
            window(c, W / 2 - 300, 700, W / 2 + 300, 800);
            label(c, "Welcome to Isle Delfino", W / 2, 768, 44, 0xffffe27a, Paint.Align.CENTER, 540);
        }

        /** A course: the fixed parts once into [base], then the live marker, glow and counters. */
        private void course(Canvas c, Snapshot s, Area area, float t) {
            boolean slots = s.stage < 61 && s.tracked();
            String key = s.scene + "|" + s.stage + "|" + s.episode + "|" + s.held + "|" + slots + "|" + s.areaName + "|"
                + s.episodeName + "|" + (areaMap == null ? "-" : areaMap.scene) + "|" + photoSeq + "|" + art.size();
            if (base == null || !key.equals(baseKey)) {
                if (base == null) base = Bitmap.createBitmap((int) W, (int) H, Bitmap.Config.ARGB_8888);
                base.eraseColor(0);
                courseBase(new Canvas(base), s, area, slots);
                baseKey = key;
            }
            c.drawBitmap(base, 0, 0, paint);
            if (slots && !area.secret && s.episode >= 0 && s.episode < 8) currentEpisode(c, s, t);
            marker(c, s, t);
            counters(c, s, slots);
        }

        private void courseBase(Canvas c, Snapshot s, Area area, boolean slots) {
            int stage = s.stage < 61 && s.shineStage >= 0 ? s.shineStage : -1;
            // Title: the game's own area plate when the disc has one, else the area's name.
            String plate = plate(stage, area);
            Bitmap halo = plate == null ? null : cached("halo-" + plate, () -> buildHalo(plate));
            if (halo != null) fitted(c, halo, W / 2, 78, 1000, 96);
            else {
                String name = !s.areaName.isEmpty() ? s.areaName : area.name;
                label(c, name.toUpperCase(Locale.ROOT), W / 2, 104, 66, 0xffffffff, Paint.Align.CENTER, 1060);
            }
            String subtitle = area.secret ? "Secret course"
                : slots && s.episode >= 0 && s.episode < 8 && !s.episodeName.isEmpty() ? s.episodeName
                : stage == 1 ? "The heart of Isle Delfino" : stage == 0 ? "Welcome to Isle Delfino"
                : eclipse ? "Super Mario Eclipse" : "";
            if (!subtitle.isEmpty()) {
                text.setTextAlign(Paint.Align.CENTER); text.setStyle(Paint.Style.FILL); text.setTextSize(42);
                float width = text.measureText(subtitle);
                if (width > 1100) text.setTextSize(42 * 1100 / width);
                text.setColor(ink());
                c.drawText(subtitle, W / 2, 160, text);
            }
            // Postcard: the guide's for the original areas, else the photo the game took here.
            String picture = area.retail ? postcard(stage) : null;
            Bitmap post = picture != null ? art.get(picture) : photo != null && photoScene.equals(s.scene) ? photo : null;
            if (post != null) postcard(c, post);
            // The map panel, framed like a printed tourist map.
            mapPanel(c);
            if (slots) shineGrid(c, s, area);
        }

        /** The postcard in a white border, pinned at a slight angle with its own soft shadow. */
        private void postcard(Canvas c, Bitmap pic) {
            float w = 352, h = w * pic.getHeight() / pic.getWidth();
            if (h > 420) { h = 420; w = h * pic.getWidth() / pic.getHeight(); }
            float border = 14;
            c.save();
            c.rotate(-4, POST_CX, POST_CY);
            RectF frame = new RectF(POST_CX - w / 2 - border, POST_CY - h / 2 - border, POST_CX + w / 2 + border, POST_CY + h / 2 + border);
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
            p.setColor(0x6e000000); p.setMaskFilter(new BlurMaskFilter(12, BlurMaskFilter.Blur.NORMAL));
            c.drawRect(frame.left + 8, frame.top + 12, frame.right + 8, frame.bottom + 12, p);
            p.setMaskFilter(null); p.setColor(0xffffffff); c.drawRect(frame, p);
            c.drawBitmap(pic, null, new RectF(POST_CX - w / 2, POST_CY - h / 2, POST_CX + w / 2, POST_CY + h / 2), p);
            c.restore();
        }

        private void mapPanel(Canvas c) {
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
            RectF panel = new RectF(MAP_L, MAP_T, MAP_R, MAP_B);
            p.setColor(0x5a05143c); p.setMaskFilter(new BlurMaskFilter(14, BlurMaskFilter.Blur.NORMAL));
            c.drawRoundRect(new RectF(panel.left, panel.top + 10, panel.right, panel.bottom + 10), 42, 42, p);
            p.setMaskFilter(null);
            p.setColor(eclipse ? 0xffbaa0ff : 0xffffffff);
            c.drawRoundRect(panel, 42, 42, p);
            RectF inner = new RectF(MAP_L + MAP_BORDER, MAP_T + MAP_BORDER, MAP_R - MAP_BORDER, MAP_B - MAP_BORDER);
            Path clip = new Path(); clip.addRoundRect(inner, 30, 30, Path.Direction.CW);
            c.save(); c.clipPath(clip);
            if (areaMap != null && shown != null && areaMap.scene.equals(shown.scene)) {
                c.drawBitmap(areaMap.bitmap, null, inner, p);
            } else {
                p.setColor(eclipse ? 0xff283480 : 0xff469cde); c.drawRect(inner, p);
                text.setTextAlign(Paint.Align.CENTER); text.setStyle(Paint.Style.FILL); text.setTextSize(34);
                text.setColor(0x99ffffff);
                c.drawText("Charting the area…", inner.centerX(), inner.centerY() + 12, text);
            }
            c.restore();
        }

        /** The area's eight episode Shines, held ones gold, numbered with the game's own digits. */
        private void shineGrid(Canvas c, Snapshot s, Area area) {
            Bitmap shine = art.get("game_6-shine_icon"), ghost = cached("ghost", this::buildGhost);
            for (int i = 0; i < 8; i++) {
                float cx = GRID_X + 32 + (i % 4) * 132, cy = GRID_Y + 30 + (i / 4) * 128;
                boolean got = (s.held & (1 << i)) != 0;
                image(c, got ? shine : ghost, cx, cy, 70, 255);
                image(c, art.get("select-sc_number_" + (i + 1)), cx + 34, cy + 40, 40, got ? 255 : 170);
            }
        }

        /** The current episode's Shine glows and bobs (live). */
        private void currentEpisode(Canvas c, Snapshot s, float t) {
            int i = s.episode;
            float cx = GRID_X + 32 + (i % 4) * 132, cy = GRID_Y + 30 + (i / 4) * 128;
            float pulse = 0.5f + 0.5f * (float) Math.sin(t * 4f);
            paint.setShader(new RadialGradient(cx, cy, 58 + 6 * pulse, (int) (150 + 60 * pulse) << 24 | 0xffe264,
                0x00ffe264, Shader.TileMode.CLAMP));
            c.drawCircle(cx, cy, 58 + 6 * pulse, paint); paint.setShader(null);
            boolean got = (s.held & (1 << i)) != 0;
            Bitmap icon = got ? art.get("game_6-shine_icon") : cached("ghost", this::buildGhost);
            image(c, icon, cx, cy - 4 + 4 * (float) Math.sin(t * 3f), 80, 255);
            image(c, art.get("select-sc_number_" + (i + 1)), cx + 34, cy + 40, 40, 255);
        }

        /** Mario's marker on the map: his HUD face on a white badge with a soft pulse ring. */
        private void marker(Canvas c, Snapshot s, float t) {
            AreaMap map = areaMap;
            if (map == null || !map.scene.equals(s.scene) || (s.marioX == 0 && s.marioZ == 0)) { markerX = Float.NaN; return; }
            float sx = (MAP_R - MAP_L - 2 * MAP_BORDER) / MAP_W, sy = (MAP_B - MAP_T - 2 * MAP_BORDER) / MAP_H;
            float x = MAP_L + MAP_BORDER + (s.marioX * map.k + map.ox) * sx;
            float y = MAP_T + MAP_BORDER + (s.marioZ * map.k + map.oz) * sy;
            x = Math.max(MAP_L + 40, Math.min(MAP_R - 40, x));
            y = Math.max(MAP_T + 40, Math.min(MAP_B - 40, y));
            if (Float.isNaN(markerX) || Math.abs(x - markerX) + Math.abs(y - markerY) > 200) { markerX = x; markerY = y; }
            else { markerX += (x - markerX) * 0.45f; markerY += (y - markerY) * 0.45f; }
            float ring = 50 + 8 * (float) Math.sin(t * 3f);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(6); paint.setColor(0x96ffffff);
            c.drawCircle(markerX, markerY, ring, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x50000000); c.drawCircle(markerX + 3, markerY + 5, 40, paint);
            paint.setColor(0xffffffff); c.drawCircle(markerX, markerY, 38, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(5); paint.setColor(0xffdc2828);
            c.drawCircle(markerX, markerY, 36, paint);
            paint.setStyle(Paint.Style.FILL);
            image(c, art.get("game_6-mario_icon"), markerX, markerY, 58, 255);
        }

        /**
         * Shines, blue coins and coins as the top HUD lays them out: stacked, each tilted up to the
         * right over its tinted swoosh, in the game's own icons and gold digits. Lives stay on top.
         */
        private void counters(Canvas c, Snapshot s, boolean slots) {
            String[] icons = {"game_6-shine_icon", "game_6-bluecoin_icon", "game_6-coin_icon"};
            int[] values = {s.shines, s.blueCoins, s.coins};
            int swoosh = swooshColour(s);
            float shift = slots ? 0 : 300;   // without the Shine grid, the counters move to the middle
            for (int k = 0; k < 3; k++) {
                if (values[k] < 0) continue;
                float x = COUNTERS_X - k * 8 + shift, y = COUNTERS_Y + k * COUNTER_STEP;
                c.save();
                c.rotate(-9, x, y + 60);
                Bitmap sw = art.get("swoosh");
                if (sw != null) {
                    tinted.setColorFilter(new PorterDuffColorFilter(swoosh, PorterDuff.Mode.SRC_IN));
                    r.set(x + 34, y + 44, x + 34 + 535, y + 44 + 65);
                    c.drawBitmap(sw, null, r, tinted);
                }
                Bitmap icon = art.get(icons[k]);
                float ix = x + 18;
                if (icon != null) {
                    float ih = 80, iw = ih * icon.getWidth() / icon.getHeight();
                    r.set(ix, y + 60 - ih / 2, ix + iw, y + 60 + ih / 2); c.drawBitmap(icon, null, r, paint);
                    ix += iw - 6;
                }
                Bitmap times = art.get("game_6-coin_number_x");
                if (times != null) { r.set(ix, y + 65 - 28, ix + 57, y + 65 + 29); c.drawBitmap(times, null, r, paint); ix += 57 - 8; }
                String digits = String.format(Locale.ROOT, values[k] >= 100 ? "%d" : "%02d", Math.max(0, values[k]));
                for (char ch : digits.toCharArray()) {
                    Bitmap d = art.get("digit" + (ch - '0'));
                    if (d == null) continue;
                    r.set(ix, y + 57 - 35, ix + 69, y + 57 + 34); c.drawBitmap(d, null, r, paint);
                    ix += 69 - 14;
                }
                c.restore();
            }
        }

        /** The game's own swoosh colour (the coin counter's backing), else the theme's. */
        private int swooshColour(Snapshot s) {
            int white = s.swooshWhite, black = s.swooshBlack;
            int fallback = eclipse ? 0xff9646c8 : 0xff5ac846;
            int w = argb(white), b = argb(black);
            if ((white & 0xff) != 0 && !nearWhite(w)) return w | 0xff000000;
            if ((black & 0xff) != 0 && !nearWhite(b)) return b | 0xff000000;
            return fallback;
        }
        private int argb(int rgba) { return (rgba & 0xff) << 24 | (rgba >>> 8); }
        private boolean nearWhite(int c) { return Color.red(c) > 230 && Color.green(c) > 230 && Color.blue(c) > 230; }

        /** The guide's postcard for each Shine area (0 airstrip, 1 plaza, 2-8 the areas, 9 Corona). */
        private String postcard(int stage) {
            switch (stage) {
                case 0: return "guide-guide_picture_00";
                case 1: return "guide-guide_picture_11";
                case 5: return "guide-guide_map_pinna";
                case 2: case 3: case 4: case 6: case 7: case 8: case 9: return "guide-guide_picture_0" + stage;
                default: return null;
            }
        }

        private String plate(int stage, Area area) {
            switch (stage) {
                case 0: return art.containsKey("option-stage_airport") ? "option-stage_airport" : null;
                case 1: return art.containsKey("option-stage_dolpictown") ? "option-stage_dolpictown" : null;
                default:
                    String name = area.plate == null ? null : "select-sc_map_" + area.plate;
                    return name != null && art.containsKey(name) ? name : null;
            }
        }
    }
}
