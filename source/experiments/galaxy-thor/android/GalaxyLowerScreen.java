package dev.thor.galaxy;

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
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Random;

/**
 * The AYN Thor's lower screen while Super Mario Galaxy runs: an observatory field guide.
 *
 * In a galaxy it is the galaxy's star chart: one node per Power Star the galaxy holds, in mission
 * order (a ledger, not a picture of the stage), gold where this save has earned it, the selected
 * mission ringed, comet / purple-coin / hidden / green stars marked as the game marks them, beside
 * the names of the missions earned or being played and this run's Star Bits against the bank.
 * In the Comet Observatory it is the save's galaxy ledger: every open galaxy with its stars
 * owned out of total, unfinished ones first, as the game's own Power Star list counts them.
 *
 * Every value comes from the game's per-frame snapshot (GalaxyActivity.nativeLowerState; layout
 * platform/include/port/thor_lower.h); unknown or invalid values are not drawn. The art is the
 * owner's own game textures (files/ThorHud, staged privately, never packaged). Display only.
 */
final class GalaxyLowerScreen extends View implements Runnable {
    private static final String TAG = "GalaxyLower";
    static final float W = 1240, H = 1080;

    // ---------------------------------------------------------------- snapshot layout (thor_lower.h)
    static final int VERSION = 2, STARS = 24, GALAXIES = 48, NAME = 48, SHORT = 32;
    static final int OFF_STAR_FLAGS = 68, OFF_GALAXY_OWNED = OFF_STAR_FLAGS + 4 * STARS,
        OFF_GALAXY_TOTAL = OFF_GALAXY_OWNED + 4 * GALAXIES, OFF_GALAXY_FLAGS = OFF_GALAXY_TOTAL + 4 * GALAXIES,
        OFF_STAGE = OFF_GALAXY_FLAGS + 4 * GALAXIES, OFF_GALAXY_NAME = OFF_STAGE + 32,
        OFF_SCENARIO_NAME = OFF_GALAXY_NAME + 2 * NAME, OFF_STAR_NAMES = OFF_SCENARIO_NAME + 2 * NAME,
        OFF_GALAXY_NAMES = OFF_STAR_NAMES + 2 * STARS * NAME, OFF_MARIO = OFF_GALAXY_NAMES + 2 * GALAXIES * SHORT,
        SIZE = OFF_MARIO + 24;
    static final int PHASE_NONE = 0, PHASE_LOGO = 1, PHASE_FILE_SELECT = 2, PHASE_LOADING = 3, PHASE_MISSION_SELECT = 4,
        PHASE_PLAY = 5, PHASE_PAUSE = 6, PHASE_STAR_GET = 7, PHASE_CUTSCENE = 8, PHASE_MISS = 9, PHASE_MAP = 10,
        PHASE_CREDITS = 11, PHASE_INTERMISSION = 12;
    static final int V_STAGE = 1, V_SCENARIO = 2, V_STARS = 4, V_COUNTERS = 8, V_LEDGER = 16, V_PURPLE = 32, V_MARIO = 64;
    static final int F_ASTRO = 1, F_LUIGI = 2, F_INFINITE = 4, F_STORYBOOK = 16;
    static final int S_OWNED = 1, S_COMET = 2, S_HIDDEN = 4, S_GREEN = 8, S_PURPLE = 16, S_GRAND = 32, S_SELECTED = 64;
    static final int G_COMPLETED = 1, G_CURRENT = 2;

    /** One copied snapshot. */
    static final class State {
        int phase, valid, flags, generation, scenarioNo, selectedNo, galaxyStars, galaxyOwned;
        int totalStars, bitsRun, bitsBanked, coins, purpleCoins, lives, galaxyCount;
        float marioX, marioY, marioZ, upX, upY, upZ;
        final int[] starFlags = new int[STARS];
        final int[] gOwned = new int[GALAXIES], gTotal = new int[GALAXIES], gFlags = new int[GALAXIES];
        String stage = "", galaxyName = "", scenarioName = "";
        final String[] starNames = new String[STARS];
        final String[] galaxyNames = new String[GALAXIES];

        static State read(ByteBuffer b) {
            if (b.getInt(0) != VERSION) return null;
            State s = new State();
            s.phase = b.getInt(4); s.valid = b.getInt(8); s.flags = b.getInt(12); s.generation = b.getInt(16);
            s.scenarioNo = b.getInt(24); s.selectedNo = b.getInt(28); s.galaxyStars = b.getInt(32); s.galaxyOwned = b.getInt(36);
            s.totalStars = b.getInt(40); s.bitsRun = b.getInt(44); s.bitsBanked = b.getInt(48); s.coins = b.getInt(52);
            s.purpleCoins = b.getInt(56); s.lives = b.getInt(60); s.galaxyCount = Math.max(0, Math.min(GALAXIES, b.getInt(64)));
            for (int i = 0; i < STARS; i++) s.starFlags[i] = b.getInt(OFF_STAR_FLAGS + 4 * i);
            for (int i = 0; i < GALAXIES; i++) {
                s.gOwned[i] = b.getInt(OFF_GALAXY_OWNED + 4 * i);
                s.gTotal[i] = b.getInt(OFF_GALAXY_TOTAL + 4 * i);
                s.gFlags[i] = b.getInt(OFF_GALAXY_FLAGS + 4 * i);
                s.galaxyNames[i] = utf16(b, OFF_GALAXY_NAMES + 2 * SHORT * i, SHORT);
            }
            StringBuilder stage = new StringBuilder();
            for (int i = 0; i < 32 && b.get(OFF_STAGE + i) != 0; i++) stage.append((char) b.get(OFF_STAGE + i));
            s.stage = stage.toString();
            s.galaxyName = utf16(b, OFF_GALAXY_NAME, NAME);
            s.scenarioName = utf16(b, OFF_SCENARIO_NAME, NAME);
            for (int i = 0; i < STARS; i++) s.starNames[i] = utf16(b, OFF_STAR_NAMES + 2 * NAME * i, NAME);
            s.marioX = b.getFloat(OFF_MARIO); s.marioY = b.getFloat(OFF_MARIO + 4); s.marioZ = b.getFloat(OFF_MARIO + 8);
            s.upX = b.getFloat(OFF_MARIO + 12); s.upY = b.getFloat(OFF_MARIO + 16); s.upZ = b.getFloat(OFF_MARIO + 20);
            return s;
        }

        private static String utf16(ByteBuffer b, int at, int units) {
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < units; i++) {
                char c = b.getChar(at + 2 * i);
                if (c == 0) break;
                out.append(c);
            }
            return out.toString().trim();
        }

        boolean has(int bit) { return (valid & bit) != 0; }
        boolean galaxyPhase() {
            return phase == PHASE_MISSION_SELECT || phase == PHASE_PLAY || phase == PHASE_PAUSE || phase == PHASE_STAR_GET
                || phase == PHASE_CUTSCENE || phase == PHASE_MISS || phase == PHASE_MAP;
        }
        /** The screen's fixed parts depend only on these. */
        String layoutKey() {
            StringBuilder k = new StringBuilder();
            k.append(phase).append('|').append(valid & ~V_COUNTERS & ~V_PURPLE & ~V_MARIO).append('|').append(flags & F_ASTRO).append('|')
                .append(generation).append('|').append(stage).append('|').append(galaxyName).append('|').append(scenarioName)
                .append('|').append(galaxyStars).append('|').append(galaxyOwned).append('|').append(galaxyCount);
            for (int i = 0; i < galaxyStars && i < STARS; i++) k.append(',').append(starFlags[i]);
            for (int i = 0; i < galaxyCount; i++) k.append(';').append(gOwned[i]).append('/').append(gTotal[i]);
            return k.toString();
        }
    }

    // ---------------------------------------------------------------- polling (UI thread)
    interface Source {
        int copy(ByteBuffer out);
        /** The game's live space (SPACE_W x SPACE_H RGBA): its sequence, 0 when nothing newer than [have]. */
        int space(ByteBuffer out, int have, int[] mode);
        /** The stage's live world pieces (port/thor_world.h): its sequence, 0 when nothing newer than [have]. */
        int world(ByteBuffer out, int have);
    }

    // The game's own space, captured from its 3D picture (platform/src/gx/vk/vk_thor_capture.cpp).
    static final int SPACE_W = 620, SPACE_H = 540;
    private final ByteBuffer spacePixels = ByteBuffer.allocateDirect(SPACE_W * SPACE_H * 4);
    private final Bitmap space = Bitmap.createBitmap(SPACE_W, SPACE_H, Bitmap.Config.ARGB_8888);
    private final int[] spaceMode = {0, -1};  // [0] the picture's kind, [1] the upper screen's bright level
    private int spaceSeq;
    private boolean spaceWarned;

    private final Source source;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ByteBuffer buffer = ByteBuffer.allocateDirect(SIZE).order(ByteOrder.LITTLE_ENDIAN);
    private State state;
    private int lastSeq;
    private long lastNewAt, lastPosLog;
    private boolean polling, bridgeWarned;
    private final Map<String, Bitmap> art = new HashMap<>();

    GalaxyLowerScreen(Context context, File artFolder, Source source) {
        super(context);
        this.source = source;
        setFocusable(false); setClickable(false);
        fallback.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        loadArt(artFolder);
    }

    // The poll is this view's own run(): the helper's d8 rejects javac 21's inner classes at -source 8.
    @Override public void run() {
        {
            if (!polling) return;
            int seq = 0;
            try { seq = source.copy(buffer); }
            catch (UnsatisfiedLinkError old) { if (!bridgeWarned) { bridgeWarned = true; Log.w(TAG, "state bridge unavailable", old); } }
            long now = SystemClock.uptimeMillis();
            if (seq != 0 && seq != lastSeq) {
                lastSeq = seq;
                lastNewAt = now;
                State next = State.read(buffer);
                if (next != null && (state == null || next.phase != state.phase || next.generation != state.generation))
                    Log.i(TAG, "phase=" + next.phase + " stage=" + next.stage + " scenario=" + next.scenarioNo + "/" + next.selectedNo
                        + " stars=" + next.galaxyOwned + "/" + next.galaxyStars + " total=" + next.totalStars + " ledger=" + next.galaxyCount
                        + " valid=" + next.valid);
                state = next;
                // Mario's position twice a second during play (for scripted device tests).
                if (next != null && next.phase == PHASE_PLAY && next.has(V_MARIO) && now - lastPosLog > 500) {
                    lastPosLog = now;
                    Log.i(TAG, String.format(Locale.ROOT, "mario %.0f %.0f %.0f", next.marioX, next.marioY, next.marioZ));
                }
            } else if (state != null && now - lastNewAt > 2000) {
                // The game stopped publishing (closing, stalled): no longer live.
                state = null;
            }
            handler.postDelayed(this, 66);
        }
    }

    void start() { if (polling) return; polling = true; lastSeq = 0; state = null; handler.post(this); invalidate(); }
    void stop() {
        polling = false; handler.removeCallbacks(this); state = null; baseKey = "";
        if (renderer != null) { renderer.release(); renderer = null; }
    }

    /**
     * This screen as the lower display shows it: this view (space, the chart, the maps), the
     * planet drawn on the GPU above it (GalaxyPlanetRenderer), and over both the marker, the
     * counters and the black veil (Over), so a cutscene still blacks out the planet too.
     */
    android.view.View layered() {
        Context context = getContext();
        android.widget.FrameLayout root = new android.widget.FrameLayout(context);
        root.addView(this);
        planetSurface = new android.view.TextureView(context);
        planetSurface.setOpaque(false);
        planetSurface.setVisibility(INVISIBLE);
        renderer = new GalaxyPlanetRenderer();
        planetSurface.setSurfaceTextureListener(renderer);
        root.addView(planetSurface);
        over = new Over(context, this);
        root.addView(over);
        return root;
    }

    /** The top layer; draws through its owner. Touches pass through to the owner below. */
    static final class Over extends View {
        private final GalaxyLowerScreen owner;
        Over(Context context, GalaxyLowerScreen owner) { super(context); this.owner = owner; setFocusable(false); setClickable(false); }
        @Override protected void onDraw(Canvas c) { owner.drawOver(c); }
    }

    // ---------------------------------------------------------------- flat maps
    /** A baked flat map (Bake-LowerMaps.py): the image and its world-to-pixel frame. */
    static final class LowerMap {
        Bitmap image;
        float k, ox, oz, w, h;
    }

    private File mapFolder;
    private final Map<String, LowerMap> maps = new HashMap<>();
    private final RectF mapRect = new RectF();
    private float markerX = Float.NaN, markerY;
    private long markerAt;

    /** The staged map for [stage], or null (loaded once; a miss is remembered). */
    private LowerMap map(String stage) {
        if (stage == null || stage.isEmpty() || mapFolder == null) return null;
        if (maps.containsKey(stage)) return maps.get(stage);
        LowerMap m = null;
        try {
            File json = new File(mapFolder, stage + ".json"), png = new File(mapFolder, stage + ".png");
            if (json.isFile() && png.isFile()) {
                byte[] bytes = java.nio.file.Files.readAllBytes(json.toPath());
                org.json.JSONObject frame = new org.json.JSONObject(new String(bytes, java.nio.charset.StandardCharsets.UTF_8));
                m = new LowerMap();
                m.image = BitmapFactory.decodeFile(png.getPath());
                m.k = (float) frame.getDouble("k"); m.ox = (float) frame.getDouble("ox"); m.oz = (float) frame.getDouble("oz");
                m.w = frame.getInt("width"); m.h = frame.getInt("height");
                if (m.image == null) m = null;
            }
        } catch (Exception broken) {
            Log.w(TAG, "map " + stage + " unreadable", broken);
            m = null;
        }
        if (m != null) Log.i(TAG, "map: " + stage);
        maps.put(stage, m);
        return m;
    }

    /** The map's place on this screen: between the galaxy's name and the counters. */
    private void mapRect(LowerMap m) {
        float h = 690, w = h * m.w / m.h;
        if (w > W - 100) { w = W - 100; h = w * m.h / m.w; }
        mapRect.set((W - w) / 2, 245, (W + w) / 2, 245 + h);
    }

    /** Mario on the map: his head in the game's own pictograph, easing after him, with a pulse. */
    private void marker(Canvas c, State s, LowerMap m, float t) {
        if (!s.has(V_MARIO)) { markerX = Float.NaN; return; }
        mapRect(m);
        float x = mapRect.left + (s.marioX * m.k + m.ox) / m.w * mapRect.width();
        float y = mapRect.top + (s.marioZ * m.k + m.oz) / m.h * mapRect.height();
        x = Math.max(mapRect.left + 20, Math.min(mapRect.right - 20, x));
        y = Math.max(mapRect.top + 20, Math.min(mapRect.bottom - 20, y));
        long now = SystemClock.uptimeMillis();
        float dt = markerAt == 0 ? 0 : Math.min(0.1f, (now - markerAt) / 1000f);
        markerAt = now;
        if (Float.isNaN(markerX) || Math.hypot(x - markerX, y - markerY) > 200) { markerX = x; markerY = y; }
        else {
            float a = 1 - (float) Math.exp(-dt / 0.06f);
            markerX += (x - markerX) * a; markerY += (y - markerY) * a;
        }
        float pulse = (t * 0.9f) % 1f;
        Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(5);
        ring.setColor(Color.argb((int) (200 * (1 - pulse)), 255, 255, 255));
        c.drawCircle(markerX, markerY, 26 + 34 * pulse, ring);
        ring.setStyle(Paint.Style.FILL); ring.setColor(0xcc0b1840);
        c.drawCircle(markerX, markerY, 34, ring);
        ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(4); ring.setColor(0xffffffff);
        c.drawCircle(markerX, markerY, 34, ring);
        icon(c, (s.flags & F_LUIGI) != 0 ? PIC_LUIGI : PIC_MARIO, markerX, markerY, 54, 255);
    }

    // ---------------------------------------------------------------- the 3D planet
    private File planetFolder;
    private volatile GalaxyPlanets planets;
    private String planetsWanted = "";
    private GalaxyPlanets.Planet planet, groupOf;
    private GalaxyPlanets.Planet[] group;
    private int groupMission;
    private final float[] groupRadius = new float[1];
    private final GalaxyPlanets.View planetView = new GalaxyPlanets.View();
    private final RectF planetArea = new RectF(30, 235, W - 30, 965);
    private GalaxyPlanetRenderer renderer;
    private android.view.TextureView planetSurface;
    private Over over;
    private boolean planetShown;
    private float planetAlpha = -1;
    private float[] marker;          // Mario on the planet this frame: x, y, front (drawn over it)
    private State overState;         // what this frame drew, for the layer above
    private boolean overGalaxy;
    private String previewGalaxy;    // debug: files/ThorHud/planet-preview names a galaxy to show off the title

    /** The baked planets of [stage]'s galaxy, loaded once in the background; null until ready. */
    private GalaxyPlanets planetsFor(String stage) {
        if (stage == null || stage.isEmpty() || planetFolder == null) return null;
        GalaxyPlanets ready = planets;
        if (ready != null && ready.galaxy.equals(stage)) return ready;
        if (!stage.equals(planetsWanted)) {
            planetsWanted = stage;
            final File file = new File(planetFolder, stage + ".planets");
            final String name = stage;
            if (file.isFile()) {
                Thread load = new Thread(() -> {
                    GalaxyPlanets loaded = GalaxyPlanets.load(file, name);
                    if (loaded != null && name.equals(planetsWanted)) planets = loaded;
                }, "GalaxyPlanets");
                load.setDaemon(true);
                load.start();
            }
        }
        return null;
    }

    // The game's live world pieces (port/thor_world.h), applied to the shown galaxy each frame.
    private final ByteBuffer world = ByteBuffer.allocateDirect(GalaxyPlanets.WORLD_HEADER
        + GalaxyPlanets.WORLD_PIECES * GalaxyPlanets.WORLD_PIECE).order(ByteOrder.LITTLE_ENDIAN);
    private int worldSeq;
    private boolean worldMissing;
    private GalaxyPlanets worldFor;

    private void applyWorld(GalaxyPlanets ps) {
        boolean fresh = ps != worldFor;
        worldFor = ps;
        if (!worldMissing) {
            try {
                int seq = source.world(world, worldSeq);
                if (seq != 0) worldSeq = seq;
            } catch (UnsatisfiedLinkError old) {
                worldMissing = true;
                Log.w(TAG, "live world state unavailable (older game library): pieces stand at their placements");
            }
        }
        ps.applyWorld(worldMissing || worldSeq == 0 ? null : world, fresh);
    }

    /** During play on a round world: the planet Mario is on, which he can turn by touch. */
    private boolean planetMode(State s) {
        return s.phase == PHASE_PLAY && s.has(V_MARIO) && s.galaxyPhase() && map(s.stage) == null && renderer != null
            && !renderer.failed() && planetsFor(s.stage) != null;
    }

    /** Shows or hides the GPU layer (and its fade-in) only when that changes. */
    private void showPlanet(boolean show, float alpha) {
        if (planetSurface == null) return;
        if (show != planetShown) { planetShown = show; planetSurface.setVisibility(show ? VISIBLE : INVISIBLE); }
        if (show && alpha != planetAlpha) { planetAlpha = alpha; planetSurface.setAlpha(alpha); }
    }

    /** The planet's soft glow, under the GPU layer. */
    private void glow(Canvas c, GalaxyPlanets.Planet main, float radius) {
        float g = planetView.screenRadius(planetArea, main.r, radius) * 1.3f;
        if (g < 1) return;
        Paint halo = new Paint(Paint.ANTI_ALIAS_FLAG);
        halo.setShader(new RadialGradient(planetArea.centerX(), planetArea.centerY(), g, 0x663a7bd5, 0x003a7bd5, Shader.TileMode.CLAMP));
        halo.setAlpha((int) (255 * planetView.fade()));
        c.drawCircle(planetArea.centerX(), planetArea.centerY(), g, halo);
    }

    /** Debug only: the largest world of [previewGalaxy], turning slowly, while not playing. */
    private boolean drawPreview(Canvas c) {
        GalaxyPlanets ps = planetsFor(previewGalaxy);
        if (ps == null || renderer == null || renderer.failed()) return false;
        applyWorld(ps);
        GalaxyPlanets.Planet main = ps.largest(1);
        if (main == null) return false;
        if (main != groupOf || groupMission != 1) { group = ps.neighbourhood(main, 1, groupRadius); groupOf = main; groupMission = 1; }
        planetView.spin(0.004f);
        GalaxyPlanets.Frame f = new GalaxyPlanets.Frame();
        planetView.frame(f, ps, planetArea, main, group, groupRadius[0], false, 0, 0, 0, 0, 1, 0);
        f.capture(ps, group, 1);
        glow(c, main, groupRadius[0]);
        renderer.submit(f);
        showPlanet(true, planetView.fade());
        return true;
    }

    private boolean drawPlanet(Canvas c, State s) {
        GalaxyPlanets ps = planetsFor(s.stage);
        if (ps == null) return false;
        int mission = s.selectedNo > 0 ? s.selectedNo : s.scenarioNo;
        applyWorld(ps);
        planet = ps.nearest(s.marioX, s.marioY, s.marioZ, planet, mission);
        if (planet == null) return false;
        if (planet != groupOf || mission != groupMission) {
            group = ps.neighbourhood(planet, mission, groupRadius);
            groupOf = planet; groupMission = mission;
        }
        GalaxyPlanets.Frame f = new GalaxyPlanets.Frame();
        marker = planetView.frame(f, ps, planetArea, planet, group, groupRadius[0], true, s.marioX, s.marioY, s.marioZ,
            s.upX, s.upY, s.upZ);
        f.capture(ps, group, mission);
        glow(c, planet, groupRadius[0]);
        renderer.submit(f);
        showPlanet(true, planetView.fade());
        return true;
    }

    /** Mario's marker on the planet (in the layer above the GPU picture). */
    private void planetMarker(Canvas c, State s) {
        float[] m = marker;
        if (m == null) return;
        boolean front = m[2] > -0.2f;
        Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
        ring.setStyle(Paint.Style.FILL); ring.setColor(front ? 0xcc0b1840 : 0x660b1840);
        c.drawCircle(m[0], m[1], 30, ring);
        ring.setStyle(Paint.Style.STROKE); ring.setStrokeWidth(4); ring.setColor(front ? 0xffffffff : 0x88ffffff);
        c.drawCircle(m[0], m[1], 30, ring);
        icon(c, (s.flags & F_LUIGI) != 0 ? PIC_LUIGI : PIC_MARIO, m[0], m[1], 46, front ? 255 : 110);
    }

    // Touch: one finger turns the planet, two pinch to zoom, a double tap returns to Mario.
    private float touchX, touchY, pinchDistance;
    private long lastTap;

    @Override public boolean onTouchEvent(android.view.MotionEvent e) {
        float scale = Math.min(getWidth() / W, getHeight() / H);
        float ox = (getWidth() - W * scale) / 2, oy = (getHeight() - H * scale) / 2;
        switch (e.getActionMasked()) {
            case android.view.MotionEvent.ACTION_DOWN:
                touchX = e.getX(); touchY = e.getY();
                break;
            case android.view.MotionEvent.ACTION_POINTER_DOWN:
                if (e.getPointerCount() == 2) pinchDistance = (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1));
                break;
            case android.view.MotionEvent.ACTION_MOVE:
                if (e.getPointerCount() >= 2) {
                    float d = (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1));
                    if (pinchDistance > 10 && d > 10) planetView.pinch(d / pinchDistance);
                    pinchDistance = d;
                } else {
                    planetView.drag((e.getX() - touchX) / scale, (e.getY() - touchY) / scale);
                    touchX = e.getX(); touchY = e.getY();
                }
                break;
            case android.view.MotionEvent.ACTION_UP: {
                long now = SystemClock.uptimeMillis();
                if (now - lastTap < 320 && e.getEventTime() - e.getDownTime() < 250) { planetView.reset(); lastTap = 0; }
                else if (e.getEventTime() - e.getDownTime() < 250) lastTap = now;
                break;
            }
            default:
                break;
        }
        return true;
    }

    private void loadArt(File folder) {
        mapFolder = new File(folder, "maps");
        planetFolder = new File(folder, "planets");
        File preview = new File(folder, "planet-preview");
        if (preview.isFile()) {
            try {
                previewGalaxy = new String(java.nio.file.Files.readAllBytes(preview.toPath()), java.nio.charset.StandardCharsets.UTF_8).trim();
                Log.i(TAG, "planet preview: " + previewGalaxy);
            } catch (java.io.IOException unreadable) { previewGalaxy = null; }
            if (previewGalaxy != null && previewGalaxy.isEmpty()) previewGalaxy = null;
        }
        String[] names = {"icon-imkira", "scenarioselect-mystar"};
        for (String name : names) {
            Bitmap b = BitmapFactory.decodeFile(new File(folder, name + ".png").getPath());
            if (b != null) art.put(name, b);
        }
        File fonts = new File(folder, "fonts");
        menuFont = GameFont.load(fonts, "menufont64");
        messageFont = GameFont.load(fonts, "messagefont26");
        numberFont = GameFont.load(fonts, "numberfont");
        pictureFont = GameFont.load(fonts, "picturefont");
        Log.i(TAG, "art " + art.size() + "/" + names.length + " fonts menu=" + (menuFont != null) + " message=" + (messageFont != null)
            + " number=" + (numberFont != null) + " picture=" + (pictureFont != null) + " from " + folder);
    }

    // ---------------------------------------------------------------- drawing
    // The game's own fonts (Font.arc): menu lettering, message text, the HUD's gold digits and the
    // star-list pictographs; fallbacks only if they are not staged.
    private GameFont menuFont, messageFont, numberFont, pictureFont;
    // picturefont code points (the game's Power Star list and HUD icons)
    private static final char PIC_STAR = '7', PIC_STAR_EMPTY = '8', PIC_STAR_GREEN = 'O', PIC_STAR_GRAND = 'K', PIC_STAR_COMET = 'e',
        PIC_QUESTION = 'Z', PIC_STARBIT = ';', PIC_STARBIT_TOTAL = 'W', PIC_COIN = 'A', PIC_PURPLE = 'N', PIC_MARIO = 'B',
        PIC_LUIGI = 'L', PIC_TIMES = '@', PIC_CROWN = 'P';

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyph = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fallback = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private final long born = SystemClock.uptimeMillis();
    private Bitmap base;
    private String baseKey = "";
    private float shown;   // cross-fade of the content as the state changes
    private PorterDuffColorFilter shadowTint = new PorterDuffColorFilter(0xff000000, PorterDuff.Mode.SRC_IN);

    private static final int GOLD = 0xffffd94a, INK = 0xffffffff, SOFT = 0xffcfd8ff, DIM = 0x99c8d0ff;

    @Override protected void onDraw(Canvas c) {
        float scale = Math.min(getWidth() / W, getHeight() / H);
        c.save();
        c.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
        c.scale(scale, scale);
        float t = (SystemClock.uptimeMillis() - born) / 1000f;
        backdrop(c);
        State s = state;
        boolean galaxy = s != null && s.galaxyPhase() && s.has(V_STAGE);
        boolean planet = s != null && planetMode(s);
        String key = s == null ? "idle" : s.layoutKey() + (planet ? "|planet" : "");
        if (base == null || !key.equals(baseKey)) {
            if (base == null) base = Bitmap.createBitmap((int) W, (int) H, Bitmap.Config.ARGB_8888);
            base.eraseColor(0);
            Canvas b = new Canvas(base);
            paint.setAlpha(255);  // the base is drawn fully opaque; the fade applies when it is shown
            if (galaxy) {
                if ((s.flags & F_ASTRO) != 0) observatoryBase(b, s); else galaxyBase(b, s);
            } else {
                idleBase(b, s);
            }
            baseKey = key;
            shown = 0;
        }
        sparkles(c, t, galaxy);
        shown = Math.min(1, shown + 0.12f);
        paint.setAlpha((int) (255 * shown));
        c.drawBitmap(base, 0, 0, paint);
        paint.setAlpha(255);
        marker = null;
        boolean onPlanet = false;
        if (galaxy) {
            LowerMap map = map(s.stage);
            if (map != null) marker(c, s, map, t);
            else if (planet) onPlanet = drawPlanet(c, s);
            else if ((s.flags & F_ASTRO) == 0) chartLive(c, s, t);
        }
        if (!onPlanet && previewGalaxy != null && (s == null || s.phase != PHASE_PLAY)) onPlanet = drawPreview(c);
        if (!onPlanet) showPlanet(false, 0);
        overState = s;
        overGalaxy = galaxy;
        if (over == null) drawOverContent(c);
        c.restore();
        if (over != null) over.invalidate();
        postInvalidateOnAnimation();
    }

    /** The layer above the GPU planet, fitted as onDraw fits this view. */
    void drawOver(Canvas c) {
        float scale = Math.min(getWidth() / W, getHeight() / H);
        c.save();
        c.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
        c.scale(scale, scale);
        drawOverContent(c);
        c.restore();
    }

    private void drawOverContent(Canvas c) {
        State s = overState;
        if (overGalaxy && s != null) {
            planetMarker(c, s);
            // Paused: the pause menu above shows the counters itself.
            if (s.phase == PHASE_PAUSE) paused(c);
            else counters(c, s);
        }
        veil(c, s);
        if (GalaxyActivity.settingsShowing) settingsNote(c);
    }

    /**
     * The game's own space behind everything: the title's 3D picture (before its logo is drawn),
     * live while the title is up. Everywhere else (the file screens, loading, galaxies) the game
     * supplies nothing new and the last title picture stays, as the Sunshine screen keeps its last
     * title sky. Black until the first picture arrives.
     */
    private void backdrop(Canvas c) {
        try {
            int seq = source.space(spacePixels, spaceSeq, spaceMode);
            if (seq != 0) {
                spacePixels.rewind();
                space.copyPixelsFromBuffer(spacePixels);
                spacePixels.rewind();
                if (spaceSeq == 0) Log.i(TAG, "live space: first picture, mode " + spaceMode[0]);
                spaceSeq = seq;
            }
        } catch (UnsatisfiedLinkError old) {
            if (!spaceWarned) { spaceWarned = true; Log.w(TAG, "space bridge unavailable", old); }
        }
        if (spaceSeq == 0) { c.drawColor(0xff000000); return; }
        r.set(0, 0, W, H);
        c.drawBitmap(space, null, r, paint);
    }

    /** Under the settings menu: the game is paused, and this screen says so. */
    private void settingsNote(Canvas c) {
        veilPaint.setColor(0xc0000818);
        c.drawRect(0, 0, W, H, veilPaint);
        heading(c, "Settings", W / 2, H / 2 - 10, 1.2f, W - 200);
        text(c, "The game is paused", W / 2, H / 2 + 70, 1.3f, SOFT, true, W - 200);
    }

    private final Paint veilPaint = new Paint();
    private float light;       // how much of this screen shows: 0 black .. 1 clear
    private long veilAt, slowUntil;

    /**
     * Black whenever the upper screen is black (its fades, loading, the logos), as the Sunshine and
     * NSMB companions do, and through every cutscene: the prologue's picture book, Peach's letter,
     * Mario's run to the castle, a galaxy's opening fly-in and mission card, event demos, the Power
     * Star get and the staff roll. The upper screen's brightness comes from the renderer's probe of
     * the finished picture: its brightest 0.5% of samples above 36/255 count as clear, below 6 as
     * black. After a cutscene this screen returns slowly.
     */
    private void veil(Canvas c, State s) {
        long now = SystemClock.uptimeMillis();
        float dt = veilAt == 0 ? 0 : Math.min(0.1f, (now - veilAt) / 1000f);
        veilAt = now;
        boolean cutscene = s != null && (s.phase == PHASE_CUTSCENE || s.phase == PHASE_STAR_GET || s.phase == PHASE_CREDITS
            || (s.flags & F_STORYBOOK) != 0);
        if (cutscene) slowUntil = now + 6000;
        int level = spaceMode[1];
        float top = level < 0 ? 1 : Math.max(0, Math.min(1, (level - 6) / 30f));
        float target = cutscene ? 0 : top;
        float tau = target < light ? 0.08f : now < slowUntil ? 0.9f : 0.15f;
        light += (target - light) * (1 - (float) Math.exp(-dt / tau));
        if (Math.abs(target - light) < 0.004f) light = target;
        if (light >= 0.998f) return;
        veilPaint.setColor(Color.argb((int) (255 * (1 - light)), 0, 0, 0));
        c.drawRect(0, 0, W, H, veilPaint);
    }

    /**
     * The game's own twinkle sprite (Icon.arc imkira) breathing at a few fixed points: garnish
     * over the real space, time-based, nothing the screen depends on.
     */
    private void sparkles(Canvas c, float t, boolean galaxy) {
        Bitmap kira = art.get("icon-imkira");
        if (kira == null) return;
        float[][] at = galaxy
            ? new float[][]{{70, 60}, {1170, 90}, {1190, 520}, {40, 640}, {610, 250}}
            : new float[][]{{150, 170}, {1080, 140}, {1150, 700}, {110, 820}, {700, 90}, {400, 960}};
        for (int i = 0; i < at.length; i++) {
            float phase = (t * 0.55f + i * 0.37f) % 1f;
            float a = (float) Math.sin(phase * Math.PI);
            float size = 30 + 46 * a;
            paint.setAlpha((int) (235 * a));
            c.save();
            c.rotate(t * 40 + i * 50, at[i][0], at[i][1]);
            r.set(at[i][0] - size / 2, at[i][1] - size / 2, at[i][0] + size / 2, at[i][1] + size / 2);
            c.drawBitmap(kira, null, r, paint);
            c.restore();
        }
        paint.setAlpha(255);
    }

    // ------------------------------------------------------------ the game's lettering
    /** A heading in the game's menu lettering (white, outlined), or message text where it lacks a glyph. */
    private void heading(Canvas c, String value, float cx, float y, float scale, float maxWidth) {
        if (value == null || value.isEmpty()) return;
        if (menuFont != null && menuFont.covers(value)) {
            glyph.setColorFilter(null); glyph.setAlpha(255);
            menuFont.drawCentered(c, value, cx, y, scale, 0, maxWidth, glyph);
        } else {
            text(c, value, cx, y, scale * 2.1f, INK, true, maxWidth);
        }
    }

    /** Message text (white coverage, coloured here) with the game's dark offset shadow. */
    private float text(Canvas c, String value, float x, float y, float scale, int color, boolean centered, float maxWidth) {
        if (value == null || value.isEmpty()) return x;
        if (messageFont == null) {
            fallback.setTextSize(26 * scale); fallback.setColor(color);
            fallback.setTextAlign(centered ? Paint.Align.CENTER : Paint.Align.LEFT);
            c.drawText(value, x, y, fallback);
            return x + fallback.measureText(value);
        }
        float w = messageFont.width(value, scale, 1);
        if (w > maxWidth && w > 0) { scale *= maxWidth / w; w = maxWidth; }
        float left = centered ? x - w / 2 : x;
        glyph.setColorFilter(shadowTint); glyph.setAlpha((Color.alpha(color) * 170) / 255);
        messageFont.draw(c, value, left + 2.5f * scale, y + 3f * scale, scale, 1, glyph);
        glyph.setColorFilter(new PorterDuffColorFilter(color | 0xff000000, PorterDuff.Mode.SRC_IN)); glyph.setAlpha(Color.alpha(color));
        float end = messageFont.draw(c, value, left, y, scale, 1, glyph);
        glyph.setColorFilter(null); glyph.setAlpha(255);
        return end;
    }

    /** One of the game's star-list / HUD pictographs centred on (cx, cy), [size] tall. */
    private void icon(Canvas c, char code, float cx, float cy, float size, int alpha) {
        if (pictureFont == null) return;
        float scale = size / pictureFont.cellHeight;
        String s = String.valueOf(code);
        float w = pictureFont.width(s, scale, 0);
        glyph.setColorFilter(null); glyph.setAlpha(alpha);
        pictureFont.draw(c, s, cx - w / 2, cy - pictureFont.cellHeight * scale / 2 + pictureFont.baseline * scale, scale, 0, glyph);
        glyph.setAlpha(255);
    }

    /** The HUD's gold digits; returns the end x. */
    private float digits(Canvas c, int value, float x, float y, float scale, boolean centered) {
        String s = String.valueOf(Math.max(0, value));
        if (numberFont == null) return text(c, s, x, y, scale, GOLD, centered, 400);
        float w = numberFont.width(s, scale, -1);
        float left = centered ? x - w / 2 : x;
        glyph.setColorFilter(null); glyph.setAlpha(255);
        return numberFont.draw(c, s, left, y, scale, -1, glyph);
    }

    /** A counter as the game's HUD sets it: pictograph, the times sign, gold digits. */
    private float counter(Canvas c, char code, int value, float x, float y, float size) {
        icon(c, code, x + size / 2, y - size * 0.36f, size, 255);
        icon(c, PIC_TIMES, x + size * 1.05f, y - size * 0.32f, size * 0.62f, 255);
        return digits(c, value, x + size * 1.32f, y, size / 34f * 1.15f, false);
    }

    // ------------------------------------------------------------ idle states
    /** Title, file select, loading: the game's own logo over its own space. */
    private void idleBase(Canvas c, State s) {
        // The logo belongs to the top screen only (owner's choice): here, the game's live space alone.
        // The title and file select are one stage (FileSelect): no line that could be wrong there.
        String line = s == null ? ""
            : s.phase == PHASE_LOADING || s.phase == PHASE_INTERMISSION ? "Travelling..."
            : s.phase == PHASE_CREDITS ? "Thank you for playing" : "";
        text(c, line, W / 2, 800, 1.6f, SOFT, true, W - 200);
    }

    private void scrims(Canvas c) {
        // Soft darkening at the top and bottom so the lettering reads over bright space.
        Paint p = new Paint();
        p.setShader(new LinearGradient(0, 0, 0, 300, 0xa0000010, 0x00000010, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, W, 300, p);
        p.setShader(new LinearGradient(0, 760, 0, H, 0x00000010, 0xb0000010, Shader.TileMode.CLAMP));
        c.drawRect(0, 760, W, H, p);
    }

    // ------------------------------------------------------------ a galaxy
    // The chart: nodes along a gentle arc, in mission order (a ledger, not the stage's layout).
    private float nodeX(int i, int n) { return n == 1 ? W / 2 : 150 + (W - 300) * i / (n - 1f); }
    private float nodeY(int i, int n) {
        if (n == 1) return 380;
        float u = i / (n - 1f) * 2 - 1;  // -1 .. 1
        return 430 - 100 * (1 - u * u);
    }

    /** The pictograph the game's star list uses for this star in this save. */
    private char starIcon(int f) {
        boolean owned = (f & S_OWNED) != 0;
        if (owned) return (f & S_GRAND) != 0 ? PIC_STAR_GRAND : (f & S_GREEN) != 0 ? PIC_STAR_GREEN : PIC_STAR;
        if ((f & S_HIDDEN) != 0 && (f & S_SELECTED) == 0) return PIC_QUESTION;
        if ((f & S_COMET) != 0) return PIC_STAR_COMET;
        return PIC_STAR_EMPTY;
    }

    private void galaxyBase(Canvas c, State s) {
        scrims(c);
        heading(c, s.galaxyName.isEmpty() ? "Super Mario Galaxy" : s.galaxyName, W / 2, 128, 1.15f, W - 120);
        int mission = s.selectedNo > 0 ? s.selectedNo : s.scenarioNo;
        boolean stars = s.has(V_STARS) && s.galaxyStars > 0;
        if (s.phase == PHASE_MISSION_SELECT) {
            text(c, "Choose a mission", W / 2, 205, 1.5f, SOFT, true, W - 200);
        } else if (s.has(V_SCENARIO) && mission > 0 && !s.scenarioName.isEmpty()) {
            if (stars && mission <= s.galaxyStars) {
                float w = messageFont != null ? Math.min(messageFont.width(s.scenarioName, 1.5f, 1), W - 300) : 600;
                float left = W / 2 - (w + 70) / 2;
                icon(c, starIcon(s.starFlags[mission - 1]), left + 26, 192, 56, 255);
                text(c, s.scenarioName, left + 70, 205, 1.5f, INK, false, W - 300);
            } else {
                // A scene without Power Stars (the prologue, a hub): its name only.
                text(c, s.scenarioName, W / 2, 205, 1.5f, INK, true, W - 200);
            }
        }
        LowerMap map = map(s.stage);
        if (map != null) {
            // A flat stage: its map (Bake-LowerMaps.py) instead of the star chart.
            mapRect(map);
            paint.setAlpha(255);
            c.drawBitmap(map.image, null, mapRect, paint);
            return;
        }
        if (planetMode(s)) return;  // the planet view takes this space during play
        if (!stars) return;
        int n = Math.min(s.galaxyStars, 8);
        // Constellation lines in mission order: decoration of the ledger, not of the stage.
        Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        line.setStyle(Paint.Style.STROKE); line.setStrokeWidth(3); line.setColor(0x70e8ecff);
        Path path = new Path();
        for (int i = 0; i < n; i++) { if (i == 0) path.moveTo(nodeX(i, n), nodeY(i, n)); else path.lineTo(nodeX(i, n), nodeY(i, n)); }
        c.drawPath(path, line);
        for (int i = 0; i < n; i++) {
            int f = s.starFlags[i];
            boolean owned = (f & S_OWNED) != 0, selected = (f & S_SELECTED) != 0;
            float x = nodeX(i, n), y = nodeY(i, n);
            if (owned) {
                Paint halo = new Paint(Paint.ANTI_ALIAS_FLAG);
                halo.setShader(new RadialGradient(x, y, 78, (f & S_GREEN) != 0 ? 0x6644ff88 : 0x66ffd84a, 0x00000000, Shader.TileMode.CLAMP));
                c.drawCircle(x, y, 78, halo);
            }
            if (!selected) icon(c, starIcon(f), x, y, owned ? 104 : 92, owned ? 255 : 210);
            digits(c, i + 1, x, y + 86, 1.05f, true);
        }
        ledger(c, s, n);
    }

    /** The selected mission's star breathes, with the game's twinkle turning on it (live). */
    private void chartLive(Canvas c, State s, float t) {
        if (!s.has(V_STARS) || s.galaxyStars <= 0) return;
        int n = Math.min(s.galaxyStars, 8);
        Bitmap kira = art.get("icon-imkira");
        for (int i = 0; i < n; i++) {
            int f = s.starFlags[i];
            if ((f & S_SELECTED) == 0) continue;
            float x = nodeX(i, n), y = nodeY(i, n);
            float pulse = 0.5f + 0.5f * (float) Math.sin(t * 3.2f);
            paint.setShader(new RadialGradient(x, y, 100 + 12 * pulse, new int[]{(int) (120 + 90 * pulse) << 24 | 0xfff2a8, 0x00fff2a8},
                null, Shader.TileMode.CLAMP));
            c.drawCircle(x, y, 100 + 12 * pulse, paint);
            paint.setShader(null);
            icon(c, starIcon(f), x, y, 124 + 10 * pulse, 255);
            if (kira != null) {
                for (int k = 0; k < 3; k++) {
                    double a = t * 1.3 + k * 2.094;
                    float kx = x + 70 * (float) Math.cos(a), ky = y + 70 * (float) Math.sin(a), size = 34 + 10 * (float) Math.sin(t * 4 + k);
                    paint.setAlpha(220);
                    r.set(kx - size / 2, ky - size / 2, kx + size / 2, ky + size / 2);
                    c.drawBitmap(kira, null, r, paint);
                }
                paint.setAlpha(255);
            }
        }
    }

    /** The missions in order: the star the game shows for it, its number and name where revealed. */
    private void ledger(Canvas c, State s, int n) {
        float top = 585, row = Math.min(56, 320f / Math.max(1, n));
        r.set(70, top - 22, W - 70, top + row * n + 8);
        paint.setColor(0x7a050822); c.drawRoundRect(r, 30, 30, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(0x55e8ecff);
        c.drawRoundRect(r, 30, 30, paint); paint.setStyle(Paint.Style.FILL);
        for (int i = 0; i < n; i++) {
            int f = s.starFlags[i];
            boolean owned = (f & S_OWNED) != 0, selected = (f & S_SELECTED) != 0;
            float y = top + row * i + row * 0.66f;
            if (selected) { r.set(84, y - row * 0.7f, W - 84, y + row * 0.3f); paint.setColor(0x44ffffff); c.drawRoundRect(r, 18, 18, paint); }
            icon(c, starIcon(f), 126, y - row * 0.28f, row * 0.82f, owned ? 255 : 200);
            digits(c, i + 1, 186, y, row / 46f, true);
            String name = s.starNames[i];
            String shown = !name.isEmpty() ? name : (f & S_HIDDEN) != 0 ? "Hidden star" : (f & S_COMET) != 0 ? "Comet mission" : "Not yet found";
            text(c, shown, 226, y, row / 40f, owned || selected ? INK : DIM, false, W - 420);
            if ((f & S_PURPLE) != 0) icon(c, PIC_PURPLE, W - 140, y - row * 0.28f, row * 0.7f, 255);
        }
    }

    // ------------------------------------------------------------ the Observatory
    private void observatoryBase(Canvas c, State s) {
        scrims(c);
        heading(c, s.galaxyName.isEmpty() ? "Comet Observatory" : s.galaxyName, W / 2, 118, 1.1f, W - 120);
        if (s.totalStars >= 0) {
            float size = 78;
            float w = size * 1.32f + (numberFont != null ? numberFont.width(String.valueOf(s.totalStars), 2.2f, -1) : 80);
            counterBig(c, PIC_STAR, s.totalStars, W / 2 - w / 2, 236, size);
        }
        if (!s.has(V_LEDGER)) return;
        // Unfinished galaxies first (what is left to find), then the finished ones.
        int[] order = new int[s.galaxyCount];
        int k = 0, done = 0;
        for (int i = 0; i < s.galaxyCount; i++) if ((s.gFlags[i] & G_COMPLETED) == 0) order[k++] = i;
        for (int i = 0; i < s.galaxyCount; i++) if ((s.gFlags[i] & G_COMPLETED) != 0) { order[k++] = i; done++; }
        text(c, s.galaxyCount == 0 ? "No galaxies open yet"
            : (s.galaxyCount - done) + " galaxies with stars to find  -  " + done + " complete", W / 2, 300, 1.25f, SOFT, true, W - 160);
        int rows = 11, shownCount = Math.min(order.length, rows * 2);
        for (int j = 0; j < shownCount; j++) {
            int i = order[j];
            float x = j < rows ? 60 : W / 2 + 10, y = 330 + (j % rows) * 56;
            boolean complete = (s.gFlags[i] & G_COMPLETED) != 0;
            r.set(x, y, x + W / 2 - 70, y + 48);
            paint.setColor(complete ? 0x55402c00 : 0x7a050822); c.drawRoundRect(r, 24, 24, paint);
            text(c, s.galaxyNames[i], x + 22, y + 34, 1.05f, complete ? 0xffffe9a8 : INK, false, W / 2 - 300);
            if (complete) icon(c, PIC_CROWN, x + W / 2 - 230, y + 24, 38, 255);
            float end = digits(c, s.gOwned[i], x + W / 2 - 200, y + 36, 1.0f, false);
            end = text(c, "/", end + 2, y + 34, 1.0f, SOFT, false, 40);
            digits(c, s.gTotal[i], end + 2, y + 36, 1.0f, false);
        }
        if (order.length > shownCount)
            text(c, "+" + (order.length - shownCount) + " more", W / 2, 330 + rows * 56 + 26, 1.1f, SOFT, true, 400);
    }

    private void counterBig(Canvas c, char code, int value, float x, float y, float size) {
        icon(c, code, x + size / 2, y - size * 0.36f, size, 255);
        icon(c, PIC_TIMES, x + size * 1.05f, y - size * 0.32f, size * 0.6f, 255);
        digits(c, value, x + size * 1.32f, y, 2.2f, false);
    }

    // ------------------------------------------------------------ live counters
    /** Bottom band: Star Bits this run against the bank, coins (and purple coins), lives. */
    private void counters(Canvas c, State s) {
        boolean astro = (s.flags & F_ASTRO) != 0;
        float y = 1030, size = 64, x = 70;
        if (!astro && s.has(V_COUNTERS)) {
            x = counter(c, PIC_STARBIT, (s.flags & F_INFINITE) != 0 ? 999 : s.bitsRun, x, y, size) + 46;
        }
        if (s.bitsBanked >= 0 && (s.has(V_COUNTERS) || astro)) {
            float start = x;
            x = counter(c, PIC_STARBIT_TOTAL, s.bitsBanked, x, y, size) + 46;
            text(c, "banked", start + size * 1.32f, y - 58, 0.8f, SOFT, false, 200);
        }
        if (!astro && s.has(V_COUNTERS)) {
            x = counter(c, PIC_COIN, s.coins, x, y, size) + 46;
            if (s.has(V_PURPLE)) x = counter(c, PIC_PURPLE, s.purpleCoins, x, y, size) + 46;
        }
        // Lives stay on the upper screen only (the game's own HUD); coins and Star Bits live here,
        // and the upper HUD leaves them out while this screen is up.
    }

    private void paused(Canvas c) {
        paint.setColor(0x99000010); c.drawRect(0, 0, W, H, paint);
        heading(c, "Paused", W / 2, H / 2 + 24, 1.3f, W - 200);
    }
}
