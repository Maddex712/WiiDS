package dev.thor.galaxy;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;

/**
 * Super Mario Galaxy's own settings menu on the AYN Thor: a Wii-style system window in Galaxy's
 * colours, the twin of the Mario Kart Wii port's Thor Settings. Opened by the Thor's Back key or the
 * gear; the game is frozen underneath while it is open (platform/mobile/thor/ThorSettingsBridge.cpp
 * applies every change on the game's thread and saves it at once).
 *
 * Drawn at 1920x1080 with the game's own lettering (menufont64 titles, messagefont26 rows) and icons
 * (the A button, the Power Star) from the owner's Galaxy data staged privately in files/ThorHud.
 * Controls: D-pad / left stick move, Left / Right change a value, A selects, B or Back goes back.
 */
final class GalaxySettingsMenu extends View {
    private static final String TAG = "GalaxyMenu";
    static final float W = 1920, H = 1080;

    // ThorSettingsBridge.h's ThorSetting numbers.
    static final int RESOLUTION = 0, FRAME_RATE = 1, SMOOTHING = 2, SHOW_FPS = 3, RIGHT_STICK = 4, POINTER_SPEED = 5,
        STICK_RESPONSE = 6, INVERT_CAMERA = 7, LANGUAGE = 8, SKIP_MOVIES = 9, INFINITE_LIVES = 10, INFINITE_HEALTH = 11,
        INFINITE_STAR_BITS = 12, QUICK_SAVE = 13,
        // The Thor's buttons, one setting each (its Galaxy action), then "reset to defaults".
        BUTTON_BASE = 14, BUTTON_COUNT = 12, BUTTONS_RESET = BUTTON_BASE + BUTTON_COUNT, SETTING_COUNT = BUTTONS_RESET + 1;

    static final int PAGE_ROOT = 0, PAGE_DISPLAY = 1, PAGE_CONTROLS = 2, PAGE_GAME = 3, PAGE_BUTTONS = 4;
    // Row kinds.
    static final int OPEN = 0, CHOICE = 1, TOGGLE = 2, STEP = 3, RESUME = 4, WIIDS = 5, SAVE = 6, RESET = 7;

    // ThorSettingsBridge.h's ThorButtonAction numbers, and the Thor's buttons in BUTTON_BASE order.
    static final String[] ACTIONS = {"Nothing", "Jump", "Shoot Star Bits", "Spin", "Crouch / ground pound",
        "Camera behind Mario", "Pause", "Centre the pointer"};
    static final int[] ACTION_VALUES = {0, 1, 2, 3, 4, 5, 6, 7};
    // The standard layout (mobile_input::kDefaultButtonMap).
    static final int[] STANDARD_BUTTONS = {1, 2, 3, 3, 5, 3, 4, 2, 4, 7, 6, 6};
    static final String[] BUTTONS = {"A", "B", "X", "Y", "L1", "R1", "L2 (left trigger)", "R2 (right trigger)",
        "L3 (left stick press)", "R3 (right stick press)", "Start", "Select"};

    private static Row[] buttonRows() {
        Row[] rows = new Row[BUTTON_COUNT + 1];
        for (int i = 0; i < BUTTON_COUNT; i++) rows[i] = Row.choice(BUTTONS[i], BUTTON_BASE + i, ACTIONS, ACTION_VALUES);
        rows[BUTTON_COUNT] = Row.action(RESET, "Reset to the standard layout");
        return rows;
    }

    static final class Row {
        final int kind, key, target;
        final String label;
        final String[] names;
        final int[] values;
        final int min, max, step;
        Row(int kind, String label, int key, int target, String[] names, int[] values, int min, int max, int step) {
            this.kind = kind; this.label = label; this.key = key; this.target = target; this.names = names; this.values = values;
            this.min = min; this.max = max; this.step = step;
        }
        static Row open(String label, int page) { return new Row(OPEN, label, -1, page, null, null, 0, 0, 0); }
        static Row choice(String label, int key, String[] names, int[] values) { return new Row(CHOICE, label, key, 0, names, values, 0, 0, 0); }
        static Row toggle(String label, int key) { return new Row(TOGGLE, label, key, 0, null, null, 0, 1, 1); }
        static Row step(String label, int key, int min, int max, int step) { return new Row(STEP, label, key, 0, null, null, min, max, step); }
        static Row action(int kind, String label) { return new Row(kind, label, -1, 0, null, null, 0, 0, 0); }
    }

    static final String[] TITLES = {"Settings", "Display", "Controls", "Game", "Buttons"};
    static final Row[][] PAGES = {
        {Row.open("Display", PAGE_DISPLAY), Row.open("Controls", PAGE_CONTROLS), Row.open("Game", PAGE_GAME),
            Row.action(SAVE, "Quick Save"), Row.action(RESUME, "Resume"), Row.action(WIIDS, "Return to WiiDS")},
        {Row.choice("Resolution", RESOLUTION, new String[]{"480p (Wii)", "720p", "1080p (Thor screen)", "1440p"}, new int[]{0, 1, 2, 3}),
            Row.choice("Frame rate", FRAME_RATE, new String[]{"60 FPS", "30 FPS (battery saver)"}, new int[]{60, 30}),
            Row.toggle("Smoothing (anti-aliasing and bloom)", SMOOTHING),
            Row.choice("Show FPS", SHOW_FPS, new String[]{"Off", "FPS", "Full stats"}, new int[]{0, 1, 2})},
        {Row.choice("Right stick", RIGHT_STICK, new String[]{"Camera", "Star Pointer"}, new int[]{0, 1}),
            Row.step("Pointer and camera speed", POINTER_SPEED, 50, 250, 10),
            Row.step("Stick response", STICK_RESPONSE, 50, 200, 10),
            Row.toggle("Invert camera", INVERT_CAMERA),
            Row.open("Buttons", PAGE_BUTTONS)},
        {Row.choice("Language", LANGUAGE, new String[]{"Automatic", "English", "Español", "Français", "Deutsch", "Italiano"},
                new int[]{0, 1, 2, 3, 4, 5}),
            Row.toggle("Skip movies", SKIP_MOVIES),
            Row.toggle("Infinite lives", INFINITE_LIVES),
            Row.toggle("Infinite health", INFINITE_HEALTH),
            Row.toggle("Infinite Star Bits", INFINITE_STAR_BITS)},
        buttonRows(),
    };
    static final String[][] HELP = {
        {"Settings save as soon as they change. Quick Save saves your game right where you are."},
        {"Resolution and smoothing apply as soon as the game resumes.",
            "The Thor's screen is 16:9: Galaxy already fills it in widescreen."},
        {"In the game's menus the left stick also moves the Wii cursor."},
        {"A new language applies the next time Galaxy starts."},
        {"Choose what each Thor button does. The D-pad, sticks and Back keep their jobs."},
    };

    interface Host {
        void setValue(int key, int value);
        void readValues(int[] out);
        void close();
        void returnToWiiDS();
    }

    private final Host host;
    private final int[] values = new int[SETTING_COUNT];
    private int page = PAGE_ROOT, focus;
    private final int[] lastFocus = new int[5];
    private final float[] lift = new float[16];
    private final long born = SystemClock.uptimeMillis();
    private long lastFrame, nextRepeat;
    private int stickDir;  // -1 up, 1 down, -2 left, 2 right, 0 none
    private boolean leaving;

    private GameFont menuFont, messageFont;
    private Bitmap aButton, star, kira;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyph = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fallback = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private final Path path = new Path();

    GalaxySettingsMenu(Context context, File art, Host host) {
        super(context);
        this.host = host;
        setFocusable(true);
        setFocusableInTouchMode(true);
        File fonts = new File(art, "fonts");
        menuFont = GameFont.load(fonts, "menufont64");
        messageFont = GameFont.load(fonts, "messagefont26");
        aButton = BitmapFactory.decodeFile(new File(art, "iconabutton-myiconabutton00.png").getPath());
        star = BitmapFactory.decodeFile(new File(art, "pausemenu-imgetstaricon.png").getPath());
        kira = BitmapFactory.decodeFile(new File(art, "icon-imkira.png").getPath());
        fallback.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.BOLD));
        host.readValues(values);
        Log.i(TAG, "menu art: fonts menu=" + (menuFont != null) + " message=" + (messageFont != null) + " a=" + (aButton != null)
            + " star=" + (star != null));
    }

    // ------------------------------------------------------------------ input
    private Row[] rows() { return PAGES[page]; }

    private void move(int dir) {
        Row[] rows = rows();
        if (page == PAGE_ROOT && focus >= 4) {
            if (dir == -2 || dir == 2) { focus = focus == 4 ? 5 : 4; invalidate(); return; }
            if (dir == -1) { focus = 3; invalidate(); return; }
            return;
        }
        if (dir == -1 && focus > 0) focus--;
        else if (dir == 1 && focus < rows.length - 1) focus = page == PAGE_ROOT && focus == 3 ? 4 : focus + 1;
        else if (dir == -2 || dir == 2) change(rows[focus], dir > 0 ? 1 : -1);
        invalidate();
    }

    private void change(Row row, int delta) {
        if (row.kind == CHOICE) {
            int at = 0;
            for (int i = 0; i < row.values.length; i++) if (row.values[i] == values[row.key]) at = i;
            int next = Math.max(0, Math.min(row.values.length - 1, at + delta));
            if (next != at) set(row.key, row.values[next]);
        } else if (row.kind == TOGGLE) {
            set(row.key, delta > 0 ? 1 : 0);
        } else if (row.kind == STEP) {
            int v = Math.max(row.min, Math.min(row.max, values[row.key] + delta * row.step));
            if (v != values[row.key]) set(row.key, v);
        }
    }

    private void set(int key, int value) {
        values[key] = value;
        host.setValue(key, value);
    }

    private void select() {
        Row row = rows()[focus];
        switch (row.kind) {
            case OPEN: lastFocus[page] = focus; page = row.target; focus = 0; break;
            case CHOICE: {
                int at = 0;
                for (int i = 0; i < row.values.length; i++) if (row.values[i] == values[row.key]) at = i;
                set(row.key, row.values[(at + 1) % row.values.length]);
                break;
            }
            case TOGGLE: set(row.key, values[row.key] != 0 ? 0 : 1); break;
            case STEP: change(row, 1); break;
            case RESUME: host.close(); break;
            // Saves once the game runs again: the menu closes first (the game is frozen under it).
            case SAVE: host.setValue(QUICK_SAVE, 1); host.close(); break;
            case WIIDS: leaving = true; host.returnToWiiDS(); break;
            case RESET:
                // The game applies it on its own thread: show the standard layout at once.
                host.setValue(BUTTONS_RESET, 1);
                System.arraycopy(STANDARD_BUTTONS, 0, values, BUTTON_BASE, BUTTON_COUNT);
                break;
            default: break;
        }
        invalidate();
    }

    private void back() {
        if (page == PAGE_ROOT) { host.close(); return; }
        if (page == PAGE_BUTTONS) {
            page = PAGE_CONTROLS;
            focus = PAGES[PAGE_CONTROLS].length - 1;
            invalidate();
            return;
        }
        int from = page;
        page = PAGE_ROOT;
        focus = from - 1;
        invalidate();
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: move(-1); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(1); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: move(-2); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: move(2); return true;
            case KeyEvent.KEYCODE_BUTTON_L1: if (page != PAGE_ROOT) { change(rows()[focus], -1); invalidate(); } return true;
            case KeyEvent.KEYCODE_BUTTON_R1: if (page != PAGE_ROOT) { change(rows()[focus], 1); invalidate(); } return true;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE:
            case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_BUTTON_START:
                return true;  // acted on when released, so the press never reaches the game after closing
            default: return true;  // nothing reaches the game while the menu is up
        }
    }

    @Override public boolean onKeyUp(int code, KeyEvent e) {
        if (leaving) return true;
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_DPAD_CENTER: select(); return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE: back(); return true;
            case KeyEvent.KEYCODE_BUTTON_START: host.close(); return true;
            default: return true;
        }
    }

    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) == 0 && (e.getSource() & InputDevice.SOURCE_GAMEPAD) == 0)
            return super.onGenericMotionEvent(e);
        float x = e.getAxisValue(MotionEvent.AXIS_X), y = e.getAxisValue(MotionEvent.AXIS_Y);
        float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (Math.abs(hx) > 0.5f) x = hx;
        if (Math.abs(hy) > 0.5f) y = hy;
        int dir = 0;
        if (Math.abs(y) > 0.55f && Math.abs(y) >= Math.abs(x)) dir = y < 0 ? -1 : 1;
        else if (Math.abs(x) > 0.55f) dir = x < 0 ? -2 : 2;
        if (dir != stickDir) {
            stickDir = dir;
            if (dir != 0) { move(dir); nextRepeat = SystemClock.uptimeMillis() + 300; }
        }
        return true;
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (e.getActionMasked() != MotionEvent.ACTION_UP) return true;
        float scale = Math.min(getWidth() / W, getHeight() / H);
        float x = (e.getX() - (getWidth() - W * scale) / 2) / scale, y = (e.getY() - (getHeight() - H * scale) / 2) / scale;
        if (closeRect().contains(x, y)) { back(); return true; }
        Row[] rows = rows();
        for (int i = 0; i < rows.length; i++) {
            if (rowRect(i).contains(x, y)) {
                if (focus == i) select(); else { focus = i; invalidate(); }
                return true;
            }
        }
        return true;
    }

    // ------------------------------------------------------------------ layout
    private RectF rowRect(int i) {
        RectF out = new RectF();
        if (page == PAGE_ROOT) {
            float top = 270, h = 90, gap = 22, x0 = 310, x1 = 1610;
            if (i < 4) out.set(x0, top + i * (h + gap), x1, top + i * (h + gap) + h);
            else {
                float y = top + 4 * (h + gap), mid = (x0 + x1) / 2;
                if (i == 4) out.set(x0, y, mid - 12, y + h); else out.set(mid + 12, y, x1, y + h);
            }
        } else if (page == PAGE_BUTTONS) {
            // Two columns of six buttons, then the reset row across both.
            float top = 196, h = 82, gap = 14;
            if (i < BUTTON_COUNT) {
                int col = i / 6, line = i % 6;
                float x0 = col == 0 ? 140 : 970, x1 = col == 0 ? 950 : 1780;
                out.set(x0, top + line * (h + gap), x1, top + line * (h + gap) + h);
            } else {
                out.set(560, top + 6 * (h + gap), 1360, top + 6 * (h + gap) + h);
            }
        } else {
            float top = 200, h = 88, gap = 16;
            out.set(140, top + i * (h + gap), 1780, top + i * (h + gap) + h);
        }
        return out;
    }

    private RectF closeRect() { return new RectF(1520, 70, 1790, 134); }

    // ------------------------------------------------------------------ drawing
    @Override protected void onDraw(Canvas c) {
        long now = SystemClock.uptimeMillis();
        float dt = lastFrame == 0 ? 0 : Math.min(0.1f, (now - lastFrame) / 1000f);
        lastFrame = now;
        if (stickDir != 0 && now >= nextRepeat) { move(stickDir); nextRepeat = now + 90; }
        float t = (now - born) / 1000f;

        float scale = Math.min(getWidth() / W, getHeight() / H);
        c.drawColor(0xb0000818);  // the frozen game, dimmed
        c.save();
        c.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
        c.scale(scale, scale);
        float open = Math.min(1, t / 0.16f);
        c.translate(0, 22 * (float) Math.pow(1 - open, 3));

        panel(c, t);
        header(c);
        footer(c);
        Row[] rows = rows();
        for (int i = 0; i < rows.length; i++) {
            boolean focused = i == focus;
            lift[i] += ((focused ? 1 : 0) - lift[i]) * Math.min(1, dt / 0.1f);
            row(c, rows[i], rowRect(i), focused, lift[i], t);
        }
        help(c);
        c.restore();
        postInvalidateOnAnimation();
    }

    /** The window: Galaxy's deep-space blue with the system window's pinstripes and a cyan rim. */
    private void panel(Canvas c, float t) {
        r.set(70, 45, 1850, 1035);
        shade(new LinearGradient(0, 45, 0, 1035, 0xff16357a, 0xff0a1840, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, 24, 24, paint);
        paint.setShader(null);
        c.save();
        path.reset(); path.addRoundRect(r, 24, 24, Path.Direction.CW); c.clipPath(path);
        paint.setColor(0x0dffffff);
        for (float y = 45; y < 1035; y += 9) c.drawRect(70, y, 1850, y + 3, paint);
        if (kira != null) {
            float[][] at = {{230, 230}, {1700, 210}, {1760, 880}, {180, 900}, {960, 190}};
            for (int i = 0; i < at.length; i++) {
                float a = (float) Math.sin(((t * 0.45f + i * 0.31f) % 1f) * Math.PI);
                float size = 24 + 34 * a;
                paint.setAlpha((int) (150 * a));
                r.set(at[i][0] - size / 2, at[i][1] - size / 2, at[i][0] + size / 2, at[i][1] + size / 2);
                c.drawBitmap(kira, null, r, paint);
            }
            paint.setAlpha(255);
        }
        c.restore();
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(5); paint.setColor(0xff6fd6ff);
        r.set(72, 47, 1848, 1033); c.drawRoundRect(r, 23, 23, paint);
        paint.setStrokeWidth(2); paint.setColor(0x66ffffff);
        r.set(80, 55, 1840, 1025); c.drawRoundRect(r, 18, 18, paint);
        paint.setStyle(Paint.Style.FILL);
    }

    /** The title band, like the pause menu's: a lighter blue strip with a glowing lower edge. */
    private void header(Canvas c) {
        c.save();
        r.set(70, 45, 1850, 1035); path.reset(); path.addRoundRect(r, 24, 24, Path.Direction.CW); c.clipPath(path);
        shade(new LinearGradient(0, 45, 0, 165, 0xff3a8be0, 0xff1d58b0, Shader.TileMode.CLAMP));
        c.drawRect(70, 45, 1850, 165, paint);
        shade(new LinearGradient(0, 160, 0, 172, 0xffaef0ff, 0x00aef0ff, Shader.TileMode.CLAMP));
        c.drawRect(70, 160, 1850, 172, paint);
        paint.setShader(null);
        c.restore();
        title(c, TITLES[page], 120, 132);
        pill(c, closeRect(), false, 0);
        if (messageFont != null) {
            float s = 30f / messageFont.cellHeight;
            bBox(c, 1548, 102, 38);
            text(c, page == PAGE_ROOT ? "Close" : "Back", 1600, 114, s, 0xff26324a, false, 180);
        }
    }

    /** The hint strip along the bottom, matching the header. */
    private void footer(Canvas c) {
        c.save();
        r.set(70, 45, 1850, 1035); path.reset(); path.addRoundRect(r, 24, 24, Path.Direction.CW); c.clipPath(path);
        shade(new LinearGradient(0, 955, 0, 1035, 0xff1d58b0, 0xff2f78cf, Shader.TileMode.CLAMP));
        c.drawRect(70, 955, 1850, 1035, paint);
        shade(new LinearGradient(0, 945, 0, 957, 0x00aef0ff, 0xffaef0ff, Shader.TileMode.CLAMP));
        c.drawRect(70, 945, 1850, 957, paint);
        paint.setShader(null);
        c.restore();
        float s = messageFont != null ? 28f / messageFont.cellHeight : 1;
        float x = 120, y = 1006;
        if (aButton != null) { r.set(x, 971, x + 46, 1017); c.drawBitmap(aButton, null, r, paint); x += 56; }
        x = text(c, "Select", x, y, s, 0xffffffff, false, 400) + 50;
        bBox(c, x, 994, 40); x += 52;
        x = text(c, "Back", x, y, s, 0xffffffff, false, 300) + 50;
        if (page != PAGE_ROOT) { x = text(c, "Left / Right: Change", x, y, s, 0xffffffff, false, 500) + 50; }
        text(c, "D-pad / Stick: Move", x, y, s, 0xffffffff, false, 500);
    }

    private void help(Canvas c) {
        if (messageFont == null) return;
        float s = 28f / messageFont.cellHeight;
        String[] lines = HELP[page];
        float y = page == PAGE_ROOT ? 860 : rowRect(rows().length - 1).bottom + 70;
        for (String line : lines) {
            text(c, line, page == PAGE_ROOT ? W / 2 : 150, y, s, 0xffd8ecff, page == PAGE_ROOT, 1600);
            y += 46;
        }
    }

    private void row(Canvas c, Row row, RectF at, boolean focused, float liftAmount, float t) {
        RectF b = new RectF(at.left, at.top - 3 * liftAmount, at.right, at.bottom - 3 * liftAmount);
        pill(c, b, focused, liftAmount);
        float s = messageFont != null ? (page == PAGE_ROOT ? 40f : 36f) / messageFont.cellHeight : 1;
        float baseline = b.centerY() + (messageFont != null ? 13 * s : 12);
        int ink = focused ? 0xff3d2a00 : 0xff26324a;
        float left = b.left + b.height() * 0.55f;
        if (focused && star != null) {
            float size = b.height() * 0.62f;
            c.save();
            c.rotate((float) Math.sin(t * 3) * 12, b.left + b.height() * 0.42f, b.centerY());
            r.set(b.left + b.height() * 0.42f - size / 2, b.centerY() - size / 2, b.left + b.height() * 0.42f + size / 2, b.centerY() + size / 2);
            c.drawBitmap(star, null, r, paint);
            c.restore();
            left += b.height() * 0.3f;
        }
        boolean centred = row.kind == RESUME || row.kind == WIIDS || row.kind == RESET;
        if (centred) {
            text(c, row.label, b.centerX() + (focused ? b.height() * 0.15f : 0), baseline, s, ink, true, b.width() - 2 * b.height());
            return;
        }
        text(c, row.label, left, baseline, s, ink, false, b.width() * 0.52f);
        String value = valueText(row);
        float right = b.right - b.height() * 0.5f;
        if (row.kind == OPEN) {
            arrow(c, right - 8, b.centerY(), 16, 1, ink);
            return;
        }
        float vw = width(value, s);
        if (row.kind == CHOICE || row.kind == STEP) {
            arrow(c, right - 8, b.centerY(), 13, 1, ink);
            text(c, value, right - 40 - vw, baseline, s, ink, false, 700);
            arrow(c, right - 72 - vw, b.centerY(), 13, -1, ink);
        } else {
            text(c, value, right - vw, baseline, s, row.kind == TOGGLE && values[row.key] != 0 ? (focused ? 0xff2a6a00 : 0xff1c6b2e) : ink, false, 700);
        }
    }

    private String valueText(Row row) {
        switch (row.kind) {
            case CHOICE:
                for (int i = 0; i < row.values.length; i++) if (row.values[i] == values[row.key]) return row.names[i];
                return row.names[0];
            case TOGGLE: return values[row.key] != 0 ? "On" : "Off";
            case STEP: return values[row.key] + "%";
            default: return "";
        }
    }

    /** Galaxy's pill button: white to pale blue with a cyan rim; the focused one star-gold. */
    private void pill(Canvas c, RectF b, boolean focused, float liftAmount) {
        float rad = b.height() / 2;
        paint.setColor(0x55000000);
        r.set(b.left + 4, b.top + 8, b.right + 4, b.bottom + 8);
        c.drawRoundRect(r, rad, rad, paint);
        if (focused) {
            paint.setColor(Color.argb((int) (90 * liftAmount), 255, 214, 64));
            r.set(b.left - 10, b.top - 10, b.right + 10, b.bottom + 10);
            c.drawRoundRect(r, rad + 10, rad + 10, paint);
        }
        shade(new LinearGradient(0, b.top, 0, b.bottom,
            focused ? 0xfffffbd6 : 0xffffffff, focused ? 0xffffcf3a : 0xffbcd8f6, Shader.TileMode.CLAMP));
        c.drawRoundRect(b, rad, rad, paint);
        shade(new LinearGradient(0, b.top, 0, b.centerY(), 0x99ffffff, 0x00ffffff, Shader.TileMode.CLAMP));
        r.set(b.left + rad * 0.4f, b.top + 4, b.right - rad * 0.4f, b.centerY());
        c.drawRoundRect(r, rad * 0.6f, rad * 0.6f, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(5);
        paint.setColor(focused ? 0xffff9a12 : 0xff47c4f5);
        r.set(b.left + 2.5f, b.top + 2.5f, b.right - 2.5f, b.bottom - 2.5f);
        c.drawRoundRect(r, rad - 2.5f, rad - 2.5f, paint);
        paint.setStrokeWidth(2);
        paint.setColor(0xaa0b2550);
        c.drawRoundRect(b, rad, rad, paint);
        paint.setStyle(Paint.Style.FILL);
    }

    /** The title in the game's outlined menu lettering. */
    private void title(Canvas c, String value, float x, float baseline) {
        if (menuFont != null && menuFont.covers(value)) {
            glyph.setColorFilter(null); glyph.setAlpha(255);
            menuFont.draw(c, value, x, baseline, 64f / menuFont.cellHeight, 0, glyph);
        } else {
            text(c, value, x, baseline, messageFont != null ? 56f / messageFont.cellHeight : 2, 0xffffffff, false, 900);
        }
    }

    /** The B button glyph the game shows beside "Back": a white key with a dark letter. */
    private void bBox(Canvas c, float x, float centreY, float size) {
        r.set(x, centreY - size / 2, x + size * 0.86f, centreY + size / 2);
        paint.setColor(0xff1b1b1b); c.drawRoundRect(r, 5, 5, paint);
        r.inset(3, 3);
        paint.setColor(0xfff4f4f4); c.drawRoundRect(r, 3, 3, paint);
        if (messageFont != null) {
            float s = size * 0.72f / messageFont.cellHeight;
            text(c, "B", x + size * 0.43f, centreY + 10 * s, s, 0xff1b1b1b, true, size);
        }
    }

    /** Fills with [shader] at full strength (a shader keeps the paint's alpha from the last colour). */
    private void shade(Shader shader) {
        paint.setColor(0xffffffff);
        paint.setShader(shader);
    }

    /** A small triangle pointing right (dir 1) or left (dir -1) with its tip at x. */
    private void arrow(Canvas c, float x, float cy, float size, int dir, int color) {
        float back = x - dir * size;
        path.reset();
        path.moveTo(back, cy - size);
        path.lineTo(back, cy + size);
        path.lineTo(x, cy);
        path.close();
        paint.setColor(color);
        c.drawPath(path, paint);
    }

    private float width(String value, float s) {
        if (messageFont != null && messageFont.covers(value)) return messageFont.width(value, s, 1);
        fallback.setTextSize(26 * s);
        return fallback.measureText(value);
    }

    /** Message text in the game's lettering, tinted; returns the end x. */
    private float text(Canvas c, String value, float x, float y, float s, int color, boolean centred, float maxWidth) {
        if (value == null || value.isEmpty()) return x;
        if (messageFont == null || !messageFont.covers(value)) {
            fallback.setTextSize(26 * s); fallback.setColor(color);
            float w = fallback.measureText(value);
            float left = centred ? x - w / 2 : x;
            c.drawText(value, left, y, fallback);
            return left + w;
        }
        float w = messageFont.width(value, s, 1);
        if (w > maxWidth && w > 0) { s *= maxWidth / w; w = maxWidth; }
        float left = centred ? x - w / 2 : x;
        glyph.setColorFilter(new PorterDuffColorFilter(color | 0xff000000, PorterDuff.Mode.SRC_IN)); glyph.setAlpha(Color.alpha(color));
        float end = messageFont.draw(c, value, left, y, s, 1, glyph);
        glyph.setColorFilter(null); glyph.setAlpha(255);
        return end;
    }
}
