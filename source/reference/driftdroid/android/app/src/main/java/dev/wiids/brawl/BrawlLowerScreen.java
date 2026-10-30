package dev.wiids.brawl;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.LightingColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Choreographer;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The AYN Thor's lower screen for Brawl, laid out like Super Smash Bros. for Nintendo 3DS's bottom screen: one tall
 * panel per fighter in the player's colour, with the series emblem and "1P" in the header, a name band (the name
 * tag, or CPU and its level), the fighter's portrait with the damage in the game's own digits, stock icons in a
 * stock battle, and the character's name in the footer. Read from the game every display frame
 * (runtime/src/android/brawl_hud_state.cpp).
 *
 * It follows the game the way the other WiiDS companions (Galaxy, Sunshine, NSMB, Mario Kart) do:
 *  - black whenever the upper screen is black (its fades, loading, the boot and wrist-strap screens), measured from
 *    the finished upper picture (aurora.cpp's Thor probe);
 *  - nothing shown twice: no logo while the title screen shows it, and on the results screen, which already shows
 *    the placings, KOs, falls and self-destructs, the records it leaves out instead;
 *  - "Paused" under the game's pause, and a note while the WiiDS menu has the game frozen;
 *  - each new screen fades in, in the game's own fonts (font_hira headings, font_melee everything else).
 *
 * All the art is the player's own disc's (scripts/stage_lower_assets.py into files/ThorHud/brawl, fonts from
 * scripts/extract_fonts.py into ThorHud/brawl/fonts) and, in Project+, its card's (stage_pplus_assets.py into
 * ThorHud/brawl-pplus).
 */
final class BrawlLowerScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "Brawl-ThorHUD";
    private static final float W = 1240f, H = 1080f;  // the lower panel, landscape

    // Snapshot v4: v3's live header/players, then the official result (BrawlResultSnapshot).
    static final int HEADER = 12, PER_PLAYER = 14;
    private static final int VERSION = 4;

    // What the screen shows, from the game's scene and match state.
    private static final int BLACK = 0, TITLE = 1, MENUS = 2, BATTLE = 3, RECORDS = 4;
    /** Scenes the top screen spends black or in a movie: the boot, the wrist-strap screen, the endings. */
    private static final String[] BLACK_SCENES = {"", "scBoot", "scStrap", "scIntro", "scMemoryChange", "scStaffRoll", "scEnding",
            "scAdvEnding"};
    /** Scenes that show a battle's placings: the lower screen shows its records instead. */
    private static final String[] RESULT_SCENES = {"scVsResult"};

    private final Activity activity;
    private final DisplayManager displays;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final File artFolder;
    /** Project+'s own art, looked up first while it plays; null in vanilla. */
    private final File modArtFolder;
    private final boolean projectPlus;
    private final SharedPreferences results;
    private HudPresentation presentation;
    private boolean running, closed, menuOpen;
    private int[] state;
    private int[] lastBattle;  // the last snapshot of the battle in progress, kept for its result
    private int[] pendingBattle;  // wait for Brawl's real result; quitting a battle must not save a made-up win
    // Per slot, for the battle in progress: the records the results screen leaves out (the lower screen's page then).
    private final int[] peak = new int[4], taken = new int[4], balls = new int[4], longest = new int[4];
    private final int[] seenDamage = new int[4], seenFalls = new int[4], lifeFrom = new int[4];
    private final boolean[] seenSmashBall = new boolean[4];
    private boolean freshResult;  // a battle ended in this run: its records can follow it
    private int sceneObject = -1;
    private String scene = "";
    private String lastLogged = "";
    private int loggedPhase = -1;

    BrawlLowerScreen(Activity activity) {
        this.activity = activity;
        displays = activity.getSystemService(DisplayManager.class);
        final File hud = new File(AppFiles.dataDir(activity), "ThorHud");
        artFolder = new File(hud, "brawl");
        projectPlus = new AppSettings(activity).projectPlusActive(activity);
        modArtFolder = projectPlus ? new File(hud, "brawl-pplus") : null;
        results = activity.getSharedPreferences("lower_screen_results", Context.MODE_PRIVATE);
    }

    void start() {
        if (running || closed || displays == null) return;
        running = true;
        displays.registerDisplayListener(this, handler);
        refresh();
        Choreographer.getInstance().postFrameCallback(poll);
    }

    void stop() {
        if (!running) return;
        running = false;
        displays.unregisterDisplayListener(this);
        Choreographer.getInstance().removeFrameCallback(poll);
        dismiss();
    }

    void close() { stop(); closed = true; }

    /** The WiiDS menu is open (the game is frozen under it). */
    void setMenuOpen(boolean open) {
        menuOpen = open;
        if (presentation != null) presentation.view.invalidate();
    }

    private final Choreographer.FrameCallback poll = new Choreographer.FrameCallback() {
        @Override public void doFrame(long frameTimeNanos) {
            if (!running || closed) return;
            int[] next;
            try { next = GameActivity.nativeReadBrawlState(); }
            catch (UnsatisfiedLinkError unavailable) { Log.e(TAG, "Snapshot bridge unavailable", unavailable); stop(); return; }
            if (next != null && next.length >= BrawlResultSnapshot.LENGTH && next[0] == VERSION) {
                next[8] = 0;  // the frame counter: not drawn, so it must not trigger a redraw every frame
                trackScene(next[11]);
                trackBattle(next);
                boolean changed = state == null || !Arrays.equals(next, state);
                state = next;
                if (changed && presentation != null) presentation.view.update(next);
                logChanges(next);
                int phase = phase(next);
                if (phase != loggedPhase) {
                    loggedPhase = phase;
                    Log.i(TAG, "phase=" + phase + " scene=" + scene + " top=" + next[10]);
                }
            }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };

    /** The scene's name, read again only when the game changes scene. */
    private void trackScene(int object) {
        if (object == sceneObject) return;
        sceneObject = object;
        String name;
        try { name = GameActivity.nativeSceneName(); } catch (UnsatisfiedLinkError e) { name = ""; }
        if (!contains(RESULT_SCENES, name) && contains(RESULT_SCENES, scene)) freshResult = false;  // results left
        scene = name == null ? "" : name;
        Log.i(TAG, "scene=" + scene);
    }

    private static boolean contains(String[] list, String value) {
        for (String item : list) if (item.equals(value)) return true;
        return false;
    }

    private int phase(int[] s) {
        if (s == null || contains(BLACK_SCENES, scene)) return BLACK;
        if ("scTitle".equals(scene)) return TITLE;
        if (s[1] == 1) return s[6] > 0 ? BATTLE : BLACK;  // a battle's scene before its fighters are in: loading
        if (contains(RESULT_SCENES, scene) && freshResult) return RECORDS;
        return MENUS;
    }

    /** A battle that ends (the melee scene closes) becomes the last battle shown, unless only CPUs fought (demos). */
    private void trackBattle(int[] s) {
        boolean inBattle = s[1] == 1 && s[6] > 0;
        int elapsed = Math.max(0, s[4]);
        if (inBattle) {
            List<Fighter> now = fighters(s);
            if (lastBattle == null) {
                pendingBattle = null;
                for (int[] a : new int[][] {peak, taken, balls, longest, lifeFrom}) Arrays.fill(a, 0);
                Arrays.fill(seenDamage, 0);
                Arrays.fill(seenFalls, 0);  // later CPU slots can appear several frames after player 1
                for (Fighter f : now) { seenDamage[f.slot] = f.damage10; seenFalls[f.slot] = f.falls; }
                Arrays.fill(seenSmashBall, false);
            }
            for (Fighter f : now) {
                int p = f.slot;
                peak[p] = Math.max(peak[p], f.damage10);
                if (f.damage10 > seenDamage[p]) taken[p] += f.damage10 - seenDamage[p];
                seenDamage[p] = f.damage10;
                if (f.falls > seenFalls[p]) {  // a stock ends: its life is the time since the last one began
                    longest[p] = Math.max(longest[p], elapsed - lifeFrom[p]);
                    lifeFrom[p] = elapsed;
                    seenFalls[p] = f.falls;
                }
                if (f.finalSmash && !seenSmashBall[p]) balls[p]++;
                seenSmashBall[p] = f.finalSmash;
            }
            lastBattle = BrawlResultSnapshot.retainPlayers(lastBattle, s);
            return;
        }
        if (s[1] == 1) return;  // live scene temporarily has no entries (loading or a fighter changing)
        if (lastBattle != null) { pendingBattle = lastBattle; lastBattle = null; }
        if (pendingBattle == null || !"scVsResult".equals(scene)) return;
        if (s[BrawlResultSnapshot.BASE] == 9) { pendingBattle = null; return; }  // No Contest
        int[] ended = BrawlResultSnapshot.complete(pendingBattle, s);
        if (ended == null) return;
        pendingBattle = null;
        boolean human = false;
        for (Fighter f : fighters(ended)) {
            if (!f.cpu) human = true;
            if (!f.out) longest[f.slot] = Math.max(longest[f.slot], Math.max(0, ended[4]) - lifeFrom[f.slot]);
        }
        if (!human) return;
        String line = encodeResult(ended, peak, taken, longest, balls);
        results.edit().putString("last", line).apply();
        freshResult = true;
        Log.i(TAG, "battle result saved: " + line);
        if (presentation != null) presentation.view.invalidate();
    }

    /**
     * "teams,frames played,time limit;" then per fighter "slot,kind,costume,stocks(-1 timed),falls,kos,damage x10,
     * team,cpu,cpu rank,peak damage x10,self-destructs,coins,damage taken x10,longest life (frames),Smash Balls".
     */
    private static String encodeResult(int[] s, int[] peaks, int[] taken, int[] longest, int[] balls) {
        StringBuilder out = new StringBuilder().append(s[7]).append(',').append(Math.max(0, s[4])).append(',')
                .append(Math.max(0, s[5]));
        for (Fighter f : fighters(s)) {
            int p = f.slot;
            out.append(';').append(p).append(',').append(f.kind).append(',').append(f.costume).append(',')
                    .append(f.stockBattle ? Math.max(0, f.stocks) : -1).append(',').append(f.falls).append(',')
                    .append(f.kos).append(',').append(f.damage10).append(',').append(f.team).append(',')
                    .append(f.cpu ? 1 : 0).append(',').append(f.cpuRank).append(',').append(peaks[p]).append(',')
                    .append(f.selfDestructs).append(',').append(f.coins).append(',').append(taken[p]).append(',')
                    .append(longest[p]).append(',').append(balls[p]).append(',').append(BrawlResultSnapshot.place(s, p));
        }
        return out.toString();
    }

    private static List<Fighter> decodeResult(String line) {
        List<Fighter> list = new ArrayList<>();
        String[] parts = line.split(";");
        for (int i = 1; i < parts.length; i++) {
            String[] v = parts[i].split(",");
            if (v.length < 10) continue;
            try {
                Fighter f = new Fighter();
                f.slot = Integer.parseInt(v[0]); f.kind = Integer.parseInt(v[1]); f.costume = Integer.parseInt(v[2]);
                int stocks = Integer.parseInt(v[3]);
                f.stockBattle = stocks >= 0; f.stocks = Math.max(0, stocks); f.falls = Integer.parseInt(v[4]);
                f.kos = Integer.parseInt(v[5]); f.damage10 = Integer.parseInt(v[6]); f.team = Integer.parseInt(v[7]);
                f.cpu = "1".equals(v[8]); f.cpuRank = Integer.parseInt(v[9]);
                if (v.length >= 13) {
                    f.peak10 = Integer.parseInt(v[10]); f.selfDestructs = Integer.parseInt(v[11]);
                    f.coins = Integer.parseInt(v[12]);
                }
                if (v.length >= 16) {
                    f.taken10 = Integer.parseInt(v[13]); f.longestLife = Integer.parseInt(v[14]);
                    f.smashBalls = Integer.parseInt(v[15]);
                }
                if (v.length >= 17) f.place = Integer.parseInt(v[16]);
                f.out = f.stockBattle && f.stocks <= 0;
                list.add(f);
            } catch (NumberFormatException ignored) { }
        }
        return list;
    }

    /** The saved battle's header field `index` (1 frames played, 2 time limit); 0 for a line saved without it. */
    private static int resultHeader(String line, int index) {
        String[] head = line.split(";", 2)[0].split(",");
        try { return head.length > index ? Integer.parseInt(head[index]) : 0; } catch (NumberFormatException e) { return 0; }
    }

    /** One line when who is playing (or the match state) changes, for checking the snapshot on a device. */
    private void logChanges(int[] s) {
        StringBuilder key = new StringBuilder().append("melee=").append(s[1]).append(" paused=").append(s[2])
                .append(" players=").append(s[6]).append(" limit=").append(s[5]).append(" teams=").append(s[7]);
        for (Fighter f : fighters(s)) {
            key.append(" | P").append(f.slot + 1).append(" kind=").append(f.kind).append(" costume=").append(f.costume)
                    .append(" stocks=").append(f.stocks).append(f.stockBattle ? "" : "(timed)").append(" falls=")
                    .append(f.falls).append(" kos=").append(f.kos).append(" team=").append(f.team)
                    .append(f.cpu ? " cpu" + f.cpuRank : "");
        }
        if (s.length >= BrawlResultSnapshot.LENGTH && s[BrawlResultSnapshot.BASE] != 0)
            key.append(" result=").append(Arrays.toString(Arrays.copyOfRange(s, BrawlResultSnapshot.BASE, BrawlResultSnapshot.LENGTH)));
        String text = key.toString();
        if (!text.equals(lastLogged)) {
            lastLogged = text;
            Log.i(TAG, text + " remaining=" + s[3] + " elapsed=" + s[4] + " top=" + s[10]);
        }
    }

    private void dismiss() {
        GameActivity.nativeSetLowerPanels(false);
        HudPresentation old = presentation;
        presentation = null;
        if (old != null) old.dismiss();
    }

    private void refresh() {
        if (!running || closed) return;
        int upper = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        Display target = null;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            // Thor's internal lower panel only: never a cast, virtual or HDMI display.
            if (display.isValid() && display.getDisplayId() != upper && "Screen-2".equals(display.getName())) {
                target = display;
                break;
            }
        }
        if (target != null && presentation != null && presentation.isShowing()
                && presentation.getDisplay().getDisplayId() == target.getDisplayId()) return;
        dismiss();
        if (target == null) return;
        HudPresentation next = new HudPresentation(target);
        presentation = next;
        next.setOnDismissListener(dialog -> {
            if (presentation == next) {
                presentation = null;
                GameActivity.nativeSetLowerPanels(false);
            }
        });
        try {
            next.show();
            if (state != null) next.view.update(state);
            Log.i(TAG, "Lower screen attached display=" + target.getDisplayId());
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException | SecurityException unavailable) {
            Log.w(TAG, "Lower screen unavailable", unavailable);
            dismiss();
        }
    }

    @Override public void onDisplayAdded(int id) { refresh(); }
    @Override public void onDisplayRemoved(int id) { refresh(); }
    @Override public void onDisplayChanged(int id) { refresh(); }

    private final class HudPresentation extends Presentation {
        final HudView view;
        HudPresentation(Display display) { super(activity, display); view = new HudView(getContext()); }
        @Override protected void onCreate(Bundle saved) {
            super.onCreate(saved);
            Window window = getWindow();
            if (window != null) {
                // Never focusable or touchable: the controller keeps driving the game on the upper display.
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
            setContentView(view);
            if (window != null) window.setLayout(-1, -1);
        }
    }

    // --- characters ------------------------------------------------------------------------------

    /** gmCharacterKind -> the two-digit number the game's portraits and stock icons use. */
    private static int artId(int kind) {
        final int[] ids = {
            0, 1, 2, 3, 23, 4, 5, 6, 7, 8,          // 0x00 Mario .. 0x09 Luigi (0x04 Zero Suit Samus)
            9, 10, 11, 12, 13, 14, 15, 15, 15, 16,  // 0x0A Captain Falcon .. 0x13 Marth (Ice Climbers x3)
            17, 18, 19, 37, 21, 22, 24, 25, 26, 28, // 0x14 Mr. Game & Watch .. 0x1D Charizard
            28, 29, 29, 30, 30, 31, 32, 33, 34, 36, // 0x1E Charizard .. 0x27 Jigglypuff
            40, 43, 45, 46, 11, 37,                 // 0x28 Toon Link .. 0x2B Sonic, Giga Bowser, Wario-Man
        };
        if (kind >= 0 && kind < ids.length) return ids[kind];
        // Project+'s added fighters (ProjectM/CloneEngine.asm's slots), in its own portrait numbering.
        if (kind == 0x32) return 39;  // Roy
        if (kind == 0x33) return 27;  // Mewtwo
        if (kind == 0x35) return 42;  // Knuckles
        return -1;
    }

    /** gmCharacterKind -> the series emblem (MenSelchrMark 01 Mario .. 19 Sonic; 20, Smash Bros., otherwise). */
    private static int seriesMark(int kind) {
        final int[] marks = {
            1, 2, 3, 4, 4, 5, 6, 7, 8, 1,           // Mario, DK, Link, Samus, ZSS, Yoshi, Kirby, Fox, Pikachu, Luigi
            9, 10, 1, 1, 3, 3, 11, 11, 11, 15,      // Falcon, Ness, Bowser, Peach, Zelda, Sheik, Ice Climbers, Marth
            18, 7, 3, 12, 6, 13, 14, 10, 2, 8,      // G&W, Falco, Ganondorf, Wario, Meta Knight, Pit, Olimar, Lucas,
                                                    // Diddy, Charizard
            8, 8, 8, 8, 8, 6, 8, 15, 16, 8,         // Pokemon, Dedede, Lucario, Ike, R.O.B., Jigglypuff
            3, 7, 17, 19, 1, 12,                    // Toon Link, Wolf, Snake, Sonic, Giga Bowser, Wario-Man
        };
        if (kind >= 0 && kind < marks.length) return marks[kind];
        if (kind == 0x32) return 15;  // Roy: Fire Emblem
        if (kind == 0x33) return 8;   // Mewtwo: Pokemon
        if (kind == 0x35) return 19;  // Knuckles: Sonic
        return 20;
    }

    private static String characterName(int kind) {
        if (kind == 0x32) return "Roy";
        if (kind == 0x33) return "Mewtwo";
        if (kind == 0x35) return "Knuckles";
        final String[] names = {
            "Mario", "Donkey Kong", "Link", "Samus", "Zero Suit Samus", "Yoshi", "Kirby", "Fox", "Pikachu", "Luigi",
            "Captain Falcon", "Ness", "Bowser", "Peach", "Zelda", "Sheik", "Ice Climbers", "Ice Climbers", "Ice Climbers",
            "Marth", "Mr. Game & Watch", "Falco", "Ganondorf", "Wario", "Meta Knight", "Pit", "Olimar", "Lucas",
            "Diddy Kong", "Charizard", "Charizard", "Squirtle", "Squirtle", "Ivysaur", "Ivysaur", "King Dedede",
            "Lucario", "Ike", "R.O.B.", "Jigglypuff", "Toon Link", "Wolf", "Snake", "Sonic", "Giga Bowser", "Wario-Man",
        };
        return kind >= 0 && kind < names.length ? names[kind] : "Fighter";
    }

    /** The game keeps a CPU's level as an AI rank (Lv. 3 is 21, Lv. 9 is 100); the nearest level wins. */
    private static int cpuLevel(int rank) {
        final int[] ranks = {0, 15, 21, 31, 42, 48, 54, 65, 100};
        int best = 0;
        for (int i = 1; i < ranks.length; i++) if (Math.abs(ranks[i] - rank) < Math.abs(ranks[best] - rank)) best = i;
        return best + 1;
    }

    /**
     * Brawl: Pokemon Trainer's portrait shows the whole team; the stock icon follows the Pokemon out now.
     * Project+ plays each Pokemon on its own, and its portrait 27 is Mewtwo.
     */
    private int portraitId(int kind) {
        return !projectPlus && kind >= 0x1D && kind <= 0x22 ? 27 : artId(kind);
    }

    // The 3DS game's player colours: header, the darker name band and emblem shade, and the portrait's backdrop.
    private static final int[][] PLAYER = {
        {0xFFE2242C, 0xFF88141A, 0xFFFADEDE},  // 1P red
        {0xFF2454D6, 0xFF142E82, 0xFFDCE6FA},  // 2P blue
        {0xFFECA014, 0xFF8C5A08, 0xFFFCEED2},  // 3P yellow
        {0xFF28A63C, 0xFF146022, 0xFFDEF4E0},  // 4P green
    };
    // Team battles: red, blue and green teams take those players' colours.
    private static final int[] TEAM_SLOT = {0, 1, 3};

    /** Brawl's damage colours: white at 0%, through yellow and orange, to deep red past 150%. */
    private static int damageColor(float percent) {
        final float[] at = {0, 35, 70, 110, 150, 200};
        final int[] colors = {0xFFFFFFFF, 0xFFFFF27A, 0xFFFFC23A, 0xFFFF7A2A, 0xFFE8302A, 0xFFA81420};
        if (percent <= at[0]) return colors[0];
        for (int i = 1; i < at.length; i++) {
            if (percent <= at[i]) {
                float t = (percent - at[i - 1]) / (at[i] - at[i - 1]);
                return lerpColor(colors[i - 1], colors[i], t);
            }
        }
        return colors[colors.length - 1];
    }

    private static int lerpColor(int a, int b, float t) {
        int r = Math.round(Color.red(a) + (Color.red(b) - Color.red(a)) * t);
        int g = Math.round(Color.green(a) + (Color.green(b) - Color.green(a)) * t);
        int bl = Math.round(Color.blue(a) + (Color.blue(b) - Color.blue(a)) * t);
        return Color.argb(255, r, g, bl);
    }

    /** A fighter as the panels draw it. */
    private static final class Fighter {
        int slot, kind, costume, damage10, stocks, falls, kos, team, cpuRank, selfDestructs, coins, peak10, place;
        int taken10, longestLife, smashBalls;
        boolean cpu, stockBattle, out, finalSmash;
        /** Timed battles: KOs minus falls. */
        int score() { return kos - falls; }
    }

    private static List<Fighter> fighters(int[] s) {
        List<Fighter> list = new ArrayList<>();
        for (int p = 0; p < 4; p++) {
            int o = HEADER + p * PER_PLAYER;
            if (s[o] != 1) continue;
            Fighter f = new Fighter();
            f.slot = p; f.kind = s[o + 1]; f.costume = s[o + 2]; f.damage10 = s[o + 3];
            f.stockBattle = s[o + 5] > 0; f.stocks = s[o + 4]; f.falls = s[o + 6]; f.team = s[o + 7];
            f.cpu = s[o + 8] == 1; f.cpuRank = s[o + 9]; f.kos = s[o + 10]; f.selfDestructs = s[o + 11];
            f.finalSmash = s[o + 12] == 1; f.coins = s[o + 13];
            f.out = f.stockBattle && f.stocks <= 0;
            list.add(f);
        }
        return list;
    }

    // --- drawing ---------------------------------------------------------------------------------

    private final class HudView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint glyph = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        private final Paint fallback = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Typeface fallbackFace = Typeface.create(Typeface.create("sans-serif-black", Typeface.NORMAL),
                Typeface.ITALIC);
        private final Map<String, Bitmap> art = new HashMap<>();
        private final RectF rect = new RectF();
        private final ColorMatrixColorFilter grey;
        private final GameFont titleFont, rowFont;
        private Shader backdrop;
        private int[] s;
        // Per slot: the last damage and falls seen, and when they changed (the damage shake and the KO flash).
        private final int[] lastDamage = new int[4], lastFalls = new int[4];
        private final long[] hitAt = new long[4], koAt = new long[4];
        private final boolean[] seen = new boolean[4];
        // The fade with the upper screen (0 black .. 1 clear), and each new screen's fade-in.
        private float light;
        private long lightAt, shownSince;
        private String shownKey = "";

        HudView(Context context) {
            super(context);
            setFocusable(false);
            setBackgroundColor(Color.BLACK);
            ColorMatrix matrix = new ColorMatrix();
            matrix.setSaturation(0);
            grey = new ColorMatrixColorFilter(matrix);
            File fonts = new File(artFolder, "fonts");
            titleFont = GameFont.load(fonts, "font_hira");
            rowFont = GameFont.load(fonts, "font_melee");
            fallback.setTypeface(fallbackFace);
            Log.i(TAG, "fonts title=" + (titleFont != null) + " rows=" + (rowFont != null));
        }

        void update(int[] next) {
            long now = SystemClock.uptimeMillis();
            boolean melee = next[1] == 1;
            for (int p = 0; p < 4; p++) {
                int o = HEADER + p * PER_PLAYER;
                if (!melee || next[o] == 0) { seen[p] = false; continue; }
                int damage = next[o + 3], falls = next[o + 6];
                if (seen[p]) {
                    if (damage > lastDamage[p]) hitAt[p] = now;
                    if (falls > lastFalls[p]) koAt[p] = now;
                }
                seen[p] = true;
                lastDamage[p] = damage;
                lastFalls[p] = falls;
            }
            s = next;
            invalidate();
        }

        private Bitmap image(String name) {
            if (art.containsKey(name)) return art.get(name);
            File file = modArtFolder != null ? new File(modArtFolder, name + ".png") : null;
            if (file == null || !file.isFile()) file = new File(artFolder, name + ".png");
            Bitmap bitmap = file.isFile() ? BitmapFactory.decodeFile(file.getAbsolutePath()) : null;
            art.put(name, bitmap);
            return bitmap;
        }

        private Bitmap costumeImage(String prefix, int id, int costume) {
            if (id < 0) return null;
            String base = prefix + "_" + (id < 10 ? "0" : "") + id + "_";
            Bitmap exact = image(base + Math.max(0, costume));
            return exact != null ? exact : image(base + 0);
        }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            Display display = getDisplay();
            GameActivity.nativeSetLowerPanels(running && !closed && isAttachedToWindow()
                    && display != null && display.getState() == Display.STATE_ON);
            float scale = Math.min(getWidth() / W, getHeight() / H);
            canvas.save();
            canvas.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
            canvas.scale(scale, scale);
            long now = SystemClock.uptimeMillis();
            int phase = phase(s);
            boolean animating = false;

            drawBackdrop(canvas);
            // Each new screen fades in over a third of a second.
            String key = phase + (phase == BATTLE ? ":" + s[6] + ":" + s[7] : "");
            if (!key.equals(shownKey)) { shownKey = key; shownSince = now; }
            float appear = Math.min(1f, (now - shownSince) / 320f);
            appear = appear * appear * (3 - 2 * appear);
            int layer = appear < 1 ? canvas.saveLayerAlpha(0, 0, W, H, Math.round(255 * appear)) : -1;
            if (appear < 1) animating = true;
            if (phase == BATTLE) {
                animating |= drawBattle(canvas);
                if (s[2] == 1 && !menuOpen) note(canvas, "Paused", null);
            } else if (phase == RECORDS) {
                drawRecords(canvas);
            } else if (phase == MENUS) {
                drawBetweenBattles(canvas);
            }
            // TITLE: the backdrop alone; the title screen above shows the logo.
            if (layer >= 0) canvas.restoreToCount(layer);

            animating |= veil(canvas, phase, now);
            if (menuOpen) note(canvas, "WiiDS Menu", "The game is paused");
            canvas.restore();
            if (animating) postInvalidateOnAnimation();
        }

        /** Brawl's charcoal: lighter in the middle, near black at the edges (the WiiDS menu's window colour). */
        private void drawBackdrop(Canvas canvas) {
            if (backdrop == null) {
                backdrop = new RadialGradient(W / 2, H * 0.42f, W * 0.75f, new int[] {0xFF2E3036, 0xFF16171B, 0xFF08090B},
                        new float[] {0f, 0.6f, 1f}, Shader.TileMode.CLAMP);
            }
            paint.setColor(Color.BLACK);  // opaque: a shader takes the paint's alpha
            paint.setShader(backdrop);
            canvas.drawRect(0, 0, W, H, paint);
            paint.setShader(null);
        }

        /**
         * Black whenever the upper screen is black: the upper picture's bright level (its brightest 0.5% of samples)
         * above 36/255 counts as clear, below 6 as black, as the Galaxy companion measures it. Darkens quickly and
         * brightens a little slower, so a fade reads as the same fade on both screens. True while it moves.
         */
        private boolean veil(Canvas canvas, int phase, long now) {
            float dt = lightAt == 0 ? 1 : Math.min(0.1f, (now - lightAt) / 1000f);
            lightAt = now;
            int level = s != null ? s[10] : -1;
            float top = level < 0 ? 1 : Math.max(0, Math.min(1, (level - 6) / 30f));
            float target = phase == BLACK ? 0 : top;
            float tau = target < light ? 0.08f : 0.15f;
            light += (target - light) * (1 - (float) Math.exp(-dt / tau));
            if (Math.abs(target - light) < 0.004f) light = target;
            if (light < 0.998f) {
                paint.setColor(Color.argb(Math.round(255 * (1 - light)), 0, 0, 0));
                canvas.drawRect(0, 0, W, H, paint);
                paint.setColor(Color.BLACK);
            }
            return light != target;
        }

        /** A dimmed screen with a heading and, optionally, a line under it (the pause and the WiiDS menu). */
        private void note(Canvas canvas, String heading, String line) {
            paint.setColor(0xB0000000);
            canvas.drawRect(0, 0, W, H, paint);
            paint.setColor(Color.BLACK);
            float y = line == null ? H / 2 + 34 : H / 2 + 4;
            text(canvas, titleFont, heading, W / 2, y, 96, Color.WHITE, Paint.Align.CENTER, W - 160, true);
            if (line != null) text(canvas, rowFont, line, W / 2, y + 86, 52, 0xFFC8CCD4, Paint.Align.CENTER, W - 160, false);
        }

        private boolean drawBattle(Canvas canvas) {
            List<Fighter> list = fighters(s);
            boolean teams = s[7] == 1;
            long now = SystemClock.uptimeMillis();
            boolean animating = false;
            float margin = 10, gap = 10, width = (W - 2 * margin - 3 * gap) / 4;
            float x = (W - (width * list.size() + gap * (list.size() - 1))) / 2;
            for (Fighter f : list) {
                rect.set(x, margin, x + width, H - margin);
                float shake = 0;
                long sinceHit = now - hitAt[f.slot];
                if (sinceHit < 200) { shake = (float) Math.sin(sinceHit / 18.0) * 8 * (1 - sinceHit / 200f); animating = true; }
                long sinceKo = now - koAt[f.slot];
                float flash = sinceKo < 600 ? 1 - sinceKo / 600f : 0;
                if (flash > 0) animating = true;
                drawPanel(canvas, f, new RectF(rect), teams, PANEL_BATTLE, 0, shake, flash);
                x += width + gap;
            }
            return animating;
        }

        /** The Smash Bros. logo, and the last battle's fighters in placing order (the winner first). */
        private void drawBetweenBattles(Canvas canvas) {
            String line = results.getString("last", "");
            List<Fighter> list = line.isEmpty() ? new ArrayList<>() : decodeResult(line);
            boolean teams = line.startsWith("1");
            Bitmap logo = image("logo");
            float logoW = list.isEmpty() ? 820 : 560;
            float logoTop = list.isEmpty() ? 300 : 40;
            paint.setColor(Color.BLACK);
            if (logo != null) {
                float logoH = logoW * logo.getHeight() / logo.getWidth();
                rect.set((W - logoW) / 2, logoTop, (W + logoW) / 2, logoTop + logoH);
                canvas.drawBitmap(logo, null, rect, paint);
                logoTop += logoH;
            }
            if (projectPlus) {
                text(canvas, titleFont, "PROJECT+", W / 2, logoTop + (list.isEmpty() ? 80 : 44), list.isEmpty() ? 96 : 64,
                        0xFF3FC060, Paint.Align.CENTER, W - 200, true);
            }
            if (list.isEmpty()) return;
            list.sort((a, b) -> a.place == b.place ? Integer.compare(a.slot, b.slot) : Integer.compare(a.place, b.place));
            float top = 410, margin = 10, gap = 10, width = (W - 2 * margin - 3 * gap) / 4;
            float x = (W - (width * list.size() + gap * (list.size() - 1))) / 2;
            for (int i = 0; i < list.size(); i++) {
                drawPanel(canvas, list.get(i), new RectF(x, top, x + width, H - margin), teams, PANEL_RESULT, list.get(i).place, 0, 0);
                x += width + gap;
            }
        }

        /**
         * On the results screen, which already shows the placings, KOs, falls and self-destructs: the records it
         * leaves out, in the same panels, in player order as the battle had them.
         */
        private void drawRecords(Canvas canvas) {
            String line = results.getString("last", "");
            List<Fighter> list = line.isEmpty() ? new ArrayList<>() : decodeResult(line);
            if (list.isEmpty()) return;
            boolean teams = line.startsWith("1");
            text(canvas, titleFont, "Battle Records", W / 2, 84, 72, Color.WHITE, Paint.Align.CENTER, W - 200, true);
            // The battle's length, unless the rules already said it (a timed battle that ran its course).
            int frames = resultHeader(line, 1), limit = resultHeader(line, 2);
            boolean timeLine = frames > 0 && (limit <= 0 || frames < limit - 30);
            if (timeLine) {
                text(canvas, rowFont, "Battle time " + clock(frames), W / 2, 140, 40, 0xFFC8CCD4, Paint.Align.CENTER,
                        W - 200, false);
            }
            float top = timeLine ? 170 : 124, margin = 10, gap = 10, width = (W - 2 * margin - 3 * gap) / 4;
            float x = (W - (width * list.size() + gap * (list.size() - 1))) / 2;
            for (Fighter f : list) {
                drawPanel(canvas, f, new RectF(x, top, x + width, H - margin), teams, PANEL_RECORDS, 0, 0, 0);
                x += width + gap;
            }
        }

        /** Frames as minutes and seconds ("1:05"). */
        private String clock(int frames) {
            int seconds = Math.max(0, frames) / 60;
            return String.format(java.util.Locale.ROOT, "%d:%02d", seconds / 60, seconds % 60);
        }

        private static final int PANEL_BATTLE = 0, PANEL_RESULT = 1, PANEL_RECORDS = 2;

        /**
         * One 3DS-style panel. In a battle the fighter's damage sits at its top right; for the last battle, its
         * placing ("1st") does; on the records page the placing is small and the battle's numbers sit under the
         * portrait.
         */
        private void drawPanel(Canvas canvas, Fighter f, RectF r, boolean teams, int kind, int place, float shake,
                               float flash) {
            boolean battle = kind == PANEL_BATTLE, records = kind == PANEL_RECORDS;
            int colourSlot = teams && f.team >= 0 && f.team < TEAM_SLOT.length ? TEAM_SLOT[f.team] : f.slot;
            int head = PLAYER[colourSlot][0], dark = PLAYER[colourSlot][1], wash = PLAYER[colourSlot][2];
            float w = r.width(), border = 6;
            float headerH = battle ? 96 : 72, bandH = battle ? 70 : 54, footerH = battle ? 96 : 72;
            float statsH = records ? 4 * 72 + 16 : 0;
            float top = r.top + headerH + bandH, bottom = r.bottom - footerH - statsH;

            canvas.save();
            canvas.clipRect(r);
            // Portrait backdrop: the player's tint fading to a soft grey.
            paint.setShader(new LinearGradient(0, top, 0, bottom, wash, 0xFFECECEC, Shader.TileMode.CLAMP));
            canvas.drawRect(r.left, top, r.right, bottom, paint);
            paint.setShader(null);
            // The portrait, wider than the panel so the fighter fills it, sitting on the footer.
            Bitmap portrait = costumeImage("bust", portraitId(f.kind), f.costume);
            if (portrait != null) {
                float pw = w * (battle ? 1.55f : 1.45f), ph = pw * portrait.getHeight() / portrait.getWidth();
                rect.set(r.centerX() - pw / 2, bottom - ph, r.centerX() + pw / 2, bottom);
                canvas.save();
                canvas.clipRect(r.left, top, r.right, bottom);
                paint.setColorFilter(f.out && !records ? grey : null);
                paint.setAlpha(f.out && !records ? 140 : 255);
                canvas.drawBitmap(portrait, null, rect, paint);
                paint.setColorFilter(null);
                paint.setAlpha(255);
                canvas.restore();
            }
            // Header: the series emblem and "1P" in the darker shade.
            paint.setColor(head);
            canvas.drawRect(r.left, r.top, r.right, r.top + headerH, paint);
            Bitmap mark = image(String.format(java.util.Locale.ROOT, "mark_%02d", seriesMark(f.kind)));
            if (mark != null) {
                float m = headerH - 12;
                rect.set(r.left + 10, r.top + 6, r.left + 10 + m, r.top + 6 + m);
                // The emblem is white on black: its brightness becomes its shape, filled with the darker shade.
                paint.setColorFilter(new ColorMatrixColorFilter(new float[] {
                    0, 0, 0, 0, Color.red(dark), 0, 0, 0, 0, Color.green(dark), 0, 0, 0, 0, Color.blue(dark),
                    0.33f, 0.33f, 0.33f, 0, 0}));
                canvas.drawBitmap(mark, null, rect, paint);
                paint.setColorFilter(null);
            }
            text(canvas, rowFont, (f.slot + 1) + "P", r.right - 14, r.top + headerH * 0.78f, headerH * 0.86f, dark,
                    Paint.Align.RIGHT, w * 0.5f, false);
            // Name band: CPU and its level (a human's name tag would go here).
            paint.setColor(dark);
            canvas.drawRect(r.left, r.top + headerH, r.right, top, paint);
            if (f.cpu) {
                text(canvas, rowFont, "CPU " + cpuLevel(f.cpuRank), r.centerX(), r.top + headerH + bandH * 0.74f,
                        bandH * 0.72f, 0xE6FFFFFF, Paint.Align.CENTER, w - 24, false);
            }
            // Top right: the damage (or the placing), in the battle HUD's own digits; the records page has neither,
            // since the results screen above shows the placings.
            if (kind == PANEL_RESULT && place > 0) {
                drawPlacing(canvas, place, r.right - border - 8, top + 10, Math.min(150, w * 0.5f));
            } else if (battle && !f.out) {
                drawDamage(canvas, f.damage10 / 10, r.right - border - 6 + shake, top + 10, w - 2 * border - 24,
                        damageColor(f.damage10 / 10f));
            }
            // Stocks along the bottom of the portrait (stock battles).
            if (f.stockBattle && f.stocks > 0) {
                Bitmap icon = costumeImage("stock", artId(f.kind), f.costume);
                float sx = r.left + border + 8, room = w - 2 * border - 16;
                if (f.stocks <= 6 && icon != null) {
                    // Every stock as an icon, shrunk to fit the row.
                    float is = Math.min(battle ? 52 : 40, room / f.stocks - 4), sy = bottom - is - 10;
                    for (int i = 0; i < f.stocks; i++) {
                        rect.set(sx + i * (is + 4), sy, sx + i * (is + 4) + is, sy + is);
                        canvas.drawBitmap(icon, null, rect, paint);
                    }
                } else {
                    // Many stocks: one icon and the count, as the game's HUD does.
                    float is = battle ? 52 : 40, sy = bottom - is - 10;
                    if (icon != null) {
                        rect.set(sx, sy, sx + is, sy + is);
                        canvas.drawBitmap(icon, null, rect, paint);
                    }
                    text(canvas, rowFont, "x" + f.stocks, sx + is + 8, sy + is * 0.86f, is, Color.WHITE,
                            Paint.Align.LEFT, room - is, true);
                }
            } else if (!battle && !records && !f.stockBattle) {
                int score = f.score();
                text(canvas, rowFont, (score > 0 ? "+" : "") + score, r.left + border + 12, bottom - 16, 52, Color.WHITE,
                        Paint.Align.LEFT, w - 24, true);
            }
            if (records) drawStats(canvas, f, r.left, bottom, r.right, bottom + statsH, dark);
            // Footer: the character's name, white on black.
            paint.setColor(Color.BLACK);
            canvas.drawRect(r.left, r.bottom - footerH, r.right, r.bottom, paint);
            String name = characterName(f.kind).toUpperCase(java.util.Locale.ROOT);
            text(canvas, rowFont, name, r.centerX(), r.bottom - footerH * 0.3f, footerH * 0.62f,
                    f.out && battle ? 0xFF8E949C : Color.WHITE, Paint.Align.CENTER, w - 24, false);
            // A fall lights the panel for a moment.
            if (flash > 0) {
                paint.setColor(Color.argb(Math.round(170 * flash), 255, 255, 255));
                canvas.drawRect(r, paint);
                paint.setColor(Color.BLACK);
            }
            canvas.restore();
            // Outline in the player's colour.
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(border);
            paint.setColor(head);
            canvas.drawRect(r.left + border / 2, r.top + border / 2, r.right - border / 2, r.bottom - border / 2, paint);
            paint.setStyle(Paint.Style.FILL);
        }

        /**
         * The records under the portrait, none of which the results screen shows: all the damage taken, the most at
         * once, the longest life (a stock, or the whole battle) and the Smash Balls caught. Each row: its label
         * small, the value under it on the right.
         */
        private void drawStats(Canvas canvas, Fighter f, float left, float top, float right, float bottom, int dark) {
            paint.setColor(lerpColor(dark, 0xFF000000, 0.55f));
            canvas.drawRect(left, top, right, bottom, paint);
            String[] labels = {"Most damage", "Longest life", "Final Smash ready"};
            String[] values = {(f.peak10 / 10) + "%", clock(f.longestLife),
                    String.valueOf(f.smashBalls)};
            float rowH = 96, y = top + 8, w = right - left;
            for (int i = 0; i < labels.length; i++) {
                if (i > 0) {
                    paint.setColor(0x26FFFFFF);
                    canvas.drawRect(left + 14, y, right - 14, y + 2, paint);
                }
                text(canvas, rowFont, labels[i], left + 16, y + 28, 26, 0xFFC8CCD4, Paint.Align.LEFT, w - 32, false);
                text(canvas, rowFont, values[i], right - 16, y + 64, 40, Color.WHITE, Paint.Align.RIGHT, w - 32, false);
                y += rowH;
            }
            paint.setColor(Color.BLACK);
        }

        /** The damage number right-aligned at (right, top), from the game's digit textures, fitted to `maxWidth`. */
        private void drawDamage(Canvas canvas, int damage, float right, float top, float maxWidth, int color) {
            String value = String.valueOf(damage);
            Bitmap pct = image("dmg_pct");
            Bitmap[] glyphs = new Bitmap[value.length()];
            for (int i = 0; i < glyphs.length; i++) {
                glyphs[i] = image("dmg_" + value.charAt(i));
                if (glyphs[i] == null) return;
            }
            float height = 190, scale = height / glyphs[0].getHeight(), overlap = 6 * scale;
            float pctH = height * 0.39f, pctW = pct != null ? pctH * pct.getWidth() / pct.getHeight() : 0;
            // Drawn width: the % plus every digit, each digit overlapping its right-hand neighbour once.
            float total = pctW;
            for (Bitmap g : glyphs) total += g.getWidth() * scale - overlap;
            if (total > maxWidth) {
                float fit = maxWidth / total;
                height *= fit; scale *= fit; overlap *= fit; pctH *= fit; pctW *= fit;
            }
            // Multiply the digits' white by the damage colour; their black outline stays black.
            paint.setColorFilter(new LightingColorFilter(color & 0xFFFFFF, 0));
            float x = right - pctW;
            if (pct != null) {
                rect.set(x, top + height - pctH, x + pctW, top + height);
                canvas.drawBitmap(pct, null, rect, paint);
            }
            x += overlap;
            for (int i = glyphs.length - 1; i >= 0; i--) {
                float gw = glyphs[i].getWidth() * scale;
                rect.set(x - gw, top, x, top + height);
                canvas.drawBitmap(glyphs[i], null, rect, paint);
                x -= gw - overlap;
            }
            paint.setColorFilter(null);
        }

        /** "1st", "2nd"…: the number in the HUD's digits, gold for the winner, and the suffix beside it. */
        private void drawPlacing(Canvas canvas, int place, float right, float top, float height) {
            String suffix = place == 1 ? "st" : place == 2 ? "nd" : place == 3 ? "rd" : "th";
            int color = place == 1 ? 0xFFFFC93C : Color.WHITE;
            float sw = textWidth(rowFont, suffix, height * 0.42f);
            text(canvas, rowFont, suffix, right, top + height * 0.95f, height * 0.42f, color, Paint.Align.RIGHT,
                    height, true);
            Bitmap digit = image("dmg_" + place);
            if (digit == null) return;
            float dw = height * digit.getWidth() / digit.getHeight();
            rect.set(right - sw - 4 - dw, top, right - sw - 4, top + height);
            paint.setColorFilter(new LightingColorFilter(color & 0xFFFFFF, 0));
            canvas.drawBitmap(digit, null, rect, paint);
            paint.setColorFilter(null);
        }

        private float textWidth(GameFont font, String value, float size) {
            if (font != null && font.covers(value)) return font.width(value, size / font.cellHeight, 0);
            fallback.setTextSize(size * 0.8f);
            return fallback.measureText(value);
        }

        /**
         * Text in one of the game's fonts at `size` (its cell height), tinted, with its baseline at y and shrunk to
         * fit `maxWidth`; `outlined` adds the dark rim Smash draws round its numbers. Android's font stands in only
         * where the staged font lacks a character.
         */
        private void text(Canvas canvas, GameFont font, String value, float x, float y, float size, int color,
                          Paint.Align align, float maxWidth, boolean outlined) {
            if (value == null || value.isEmpty()) return;
            if (font == null || !font.covers(value)) {
                fallback.setTextSize(size * 0.8f);
                float w = fallback.measureText(value);
                if (w > maxWidth) { fallback.setTextSize(size * 0.8f * maxWidth / w); w = maxWidth; }
                float left = align == Paint.Align.CENTER ? x - w / 2 : align == Paint.Align.RIGHT ? x - w : x;
                fallback.setTextAlign(Paint.Align.LEFT);
                if (outlined) {
                    fallback.setStyle(Paint.Style.STROKE);
                    fallback.setStrokeWidth(fallback.getTextSize() * 0.12f);
                    fallback.setStrokeJoin(Paint.Join.ROUND);
                    fallback.setColor(0xFF0A0C14);
                    canvas.drawText(value, left, y, fallback);
                    fallback.setStyle(Paint.Style.FILL);
                }
                fallback.setColor(color);
                canvas.drawText(value, left, y, fallback);
                return;
            }
            float scale = size / font.cellHeight;
            float w = font.width(value, scale, 0);
            if (w > maxWidth && w > 0) { scale *= maxWidth / w; w = maxWidth; }
            float left = align == Paint.Align.CENTER ? x - w / 2 : align == Paint.Align.RIGHT ? x - w : x;
            if (outlined) {
                float o = Math.max(2, size * 0.06f);
                glyph.setColorFilter(new PorterDuffColorFilter(0xFF0A0C14, PorterDuff.Mode.SRC_IN));
                glyph.setAlpha(255);
                for (int i = 0; i < 8; i++) {
                    double a = i * Math.PI / 4;
                    font.draw(canvas, value, left + (float) Math.cos(a) * o, y + (float) Math.sin(a) * o, scale, 0, glyph);
                }
            }
            glyph.setColorFilter(new PorterDuffColorFilter(color | 0xFF000000, PorterDuff.Mode.SRC_IN));
            glyph.setAlpha(Color.alpha(color));
            font.draw(canvas, value, left, y, scale, 0, glyph);
            glyph.setColorFilter(null);
            glyph.setAlpha(255);
        }
    }
}
