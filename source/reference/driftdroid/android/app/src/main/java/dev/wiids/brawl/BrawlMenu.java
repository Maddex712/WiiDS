package dev.wiids.brawl;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;
import android.graphics.Shader;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Brawl's WiiDS menu on the AYN Thor, the twin of the Galaxy add-on's settings window: opened by Back, the game
 * frozen and silent underneath (GameActivity.nativeSetPaused). Drawn at 1920x1080 in Brawl's own look: a charcoal
 * window with a brushed-metal title band, the title in the game's display font (font_hira) and the rows in its menu
 * font (font_melee), the series emblems from its HUD, all from the owner's disc (scripts/extract_fonts.py and
 * stage_lower_assets.py into files/ThorHud/brawl).
 *
 * Controls: D-pad / left stick move, Left / Right change a value, A selects, B or Back goes back, Start closes.
 * Touch works too.
 */
final class BrawlMenu extends View {
    static final float W = 1920, H = 1080;

    // Settings the menu shows and changes (Host.get / Host.set).
    static final int RESOLUTION = 0, ASPECT = 1, SHOW_FPS = 2, LAYOUT = 3, L1_SHIELD = 4, RUMBLE = 5, MODE = 6,
            SKIP_INTRO = 7, AUTO_PAUSE = 8;
    // Actions.
    static final int RESUME = 100, LEAVE = 101, RESTART = 102, BACK_UP = 103, RESTORE = 200;  // RESTORE + backup index

    static final int PAGE_ROOT = 0, PAGE_DISPLAY = 1, PAGE_CONTROLS = 2, PAGE_GAME = 3, PAGE_SAVES = 4;
    private static final String[] TITLES = {"WiiDS Menu", "Display", "Controls", "Game", "Saves"};

    interface Host {
        int get(int key);
        void set(int key, int value);
        /** Resume, Return to WiiDS, Restart, Back up now, Restore a backup. */
        void act(int action);
        String leaveLabel();
        boolean projectPlusInstalled();
        /** The mode the game is running now (0 vanilla, 1 Project+): Restart appears when MODE differs. */
        int runningMode();
        /** Backups, newest first, as labels ("Oct 29, 11:40 PM"). */
        List<String> backups();
        /** A line to show on the Saves page after Back up now ("Backed up just now"), or null. */
        String savesNote();
    }

    private static final class Row {
        final int kind, key;  // kind: OPEN, CHOICE, TOGGLE, ACTION
        final String label;
        final String[] names;
        Row(int kind, int key, String label, String[] names) { this.kind = kind; this.key = key; this.label = label; this.names = names; }
    }
    private static final int OPEN = 0, CHOICE = 1, TOGGLE = 2, ACTION = 3;
    private static Row open(String label, int page) { return new Row(OPEN, page, label, null); }
    private static Row choice(String label, int key, String... names) { return new Row(CHOICE, key, label, names); }
    private static Row toggle(String label, int key) { return new Row(TOGGLE, key, label, null); }
    private static Row action(String label, int id) { return new Row(ACTION, id, label, null); }

    private final Host host;
    private int page = PAGE_ROOT, focus;
    private final int[] lastFocus = new int[5];
    private final float[] lift = new float[12];
    private int confirmRestore = -1;  // a backup waiting for its second A
    private int touchRow = -1;
    private int stickDir;
    private long nextRepeat, lastFrame;
    private final long born = SystemClock.uptimeMillis();
    private boolean leaving;

    private final GameFont titleFont, rowFont;
    private final Bitmap emblem;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint glyph = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fallback = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();
    private final Path path = new Path();

    BrawlMenu(Context context, File art, Host host) {
        super(context);
        this.host = host;
        setFocusable(true);
        setFocusableInTouchMode(true);
        File fonts = new File(art, "fonts");
        titleFont = GameFont.load(fonts, "font_hira");
        rowFont = GameFont.load(fonts, "font_melee");
        emblem = BitmapFactory.decodeFile(new File(art, "mark_20.png").getPath());
        fallback.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.BOLD));
    }

    // ------------------------------------------------------------------ pages
    private Row[] rows() {
        List<Row> rows = new ArrayList<>();
        switch (page) {
            case PAGE_ROOT:
                rows.add(action("Resume", RESUME));
                rows.add(open("Display", PAGE_DISPLAY));
                rows.add(open("Controls", PAGE_CONTROLS));
                rows.add(open("Game", PAGE_GAME));
                rows.add(open("Saves", PAGE_SAVES));
                rows.add(action(host.leaveLabel(), LEAVE));
                break;
            case PAGE_DISPLAY:
                // The scale of Brawl's own 480 lines beside each: the whole numbers keep pixels even.
                rows.add(choice("Resolution", RESOLUTION, "Match screen", "480p · 1× (Wii)", "720p · 1.5×", "960p · 2×",
                        "1080p · 2.25×", "1440p · 3×", "1920p · 4×"));
                rows.add(choice("Aspect ratio", ASPECT, "16:9", "4:3", "Fill the screen"));
                rows.add(toggle("FPS counter", SHOW_FPS));
                break;
            case PAGE_CONTROLS:
                rows.add(choice("Face buttons", LAYOUT, "GameCube", "Standard"));
                rows.add(toggle("L1 shields", L1_SHIELD));
                rows.add(toggle("Rumble", RUMBLE));
                break;
            case PAGE_GAME:
                if (host.projectPlusInstalled()) rows.add(choice("Mode", MODE, "Vanilla Brawl", "Project+"));
                rows.add(toggle("Skip intro screens", SKIP_INTRO));
                rows.add(toggle("Pause when you leave", AUTO_PAUSE));
                if (host.projectPlusInstalled() && host.get(MODE) != host.runningMode()) {
                    rows.add(action("Restart now in " + (host.get(MODE) == 1 ? "Project+" : "Vanilla Brawl"), RESTART));
                }
                break;
            case PAGE_SAVES: {
                rows.add(action("Back up now", BACK_UP));
                List<String> backups = host.backups();
                for (int i = 0; i < backups.size(); i++) {
                    rows.add(action(confirmRestore == i ? "Press A again to restore" : "Restore " + backups.get(i), RESTORE + i));
                }
                break;
            }
            default: break;
        }
        return rows.toArray(new Row[0]);
    }

    private String[] help() {
        switch (page) {
            case PAGE_ROOT: return new String[] {"The game is paused while this menu is open. Settings save as they change."};
            case PAGE_DISPLAY: return new String[] {"Resolution and the FPS counter change at once.",
                    "Between 4:3 and a widescreen ratio, Brawl switches the next time it starts."};
            case PAGE_CONTROLS: return new String[] {host.get(LAYOUT) == 0
                    ? "GameCube: A bottom, B left, X right, Y top, the way a GameCube pad sits."
                    : "Standard: A bottom, B right, X left, Y top, as printed on the Thor.",
                    "ZL and ZR always shield; R1 grabs."};
            case PAGE_GAME: return new String[] {"Skip intro screens passes the wrist-strap and title screens for you, straight to the main menu.",
                    "Pause when you leave pauses a battle when the WiiDS overlay or Home takes the screen."};
            case PAGE_SAVES: {
                String note = host.savesNote();
                return new String[] {note != null ? note : "A backup is made each time Brawl starts; the last "
                        + SaveBackups.KEEP + " are kept.", "Restoring backs up the current save first, then restarts Brawl."};
            }
            default: return new String[0];
        }
    }

    // ------------------------------------------------------------------ input
    private void move(int dir) {
        Row[] rows = rows();
        if (focus >= rows.length) focus = rows.length - 1;
        if (dir == -1 && focus > 0) { focus--; confirmRestore = -1; }
        else if (dir == 1 && focus < rows.length - 1) { focus++; confirmRestore = -1; }
        else if (dir == -2 || dir == 2) change(rows[focus], dir > 0 ? 1 : -1);
        invalidate();
    }

    private void change(Row row, int delta) {
        if (row.kind == CHOICE) {
            int next = Math.max(0, Math.min(row.names.length - 1, host.get(row.key) + delta));
            if (next != host.get(row.key)) host.set(row.key, next);
        } else if (row.kind == TOGGLE) {
            host.set(row.key, delta > 0 ? 1 : 0);
        }
    }

    void setSavePending(boolean pending) { leaving = pending; stickDir = 0; invalidate(); }

    private void select() {
        Row[] rows = rows();
        if (focus >= rows.length) return;
        Row row = rows[focus];
        switch (row.kind) {
            case OPEN: lastFocus[page] = focus; page = row.key; focus = 0; confirmRestore = -1; break;
            case CHOICE: host.set(row.key, (host.get(row.key) + 1) % row.names.length); break;
            case TOGGLE: host.set(row.key, host.get(row.key) != 0 ? 0 : 1); break;
            case ACTION:
                if (row.key >= RESTORE) {
                    int index = row.key - RESTORE;
                    if (confirmRestore != index) { confirmRestore = index; break; }
                    leaving = true;
                }
                if (row.key == LEAVE || row.key == RESTART) leaving = true;
                host.act(row.key);
                break;
            default: break;
        }
        invalidate();
    }

    private void back() {
        if (page == PAGE_ROOT) { host.act(RESUME); return; }
        focus = page;  // each page's row on the root sits at its page number (Resume is row 0)
        page = PAGE_ROOT;
        confirmRestore = -1;
        invalidate();
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        if (leaving) return true;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: move(-1); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(1); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: move(-2); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: move(2); return true;
            default: return true;  // nothing reaches the game while the menu is up; A/B/Start act when released
        }
    }

    @Override public boolean onKeyUp(int code, KeyEvent e) {
        if (leaving) return true;
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_DPAD_CENTER: select(); return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE: back(); return true;
            case KeyEvent.KEYCODE_BUTTON_START: host.act(RESUME); return true;
            default: return true;
        }
    }

    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        if (leaving) return true;
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
        if (leaving) return true;
        float scale = Math.min(getWidth() / W, getHeight() / H);
        if (scale <= 0) return true;
        float x = (e.getX() - (getWidth() - W * scale) / 2) / scale, y = (e.getY() - (getHeight() - H * scale) / 2) / scale;
        Row[] rows = rows();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                touchRow = -1;
                if (closeRect().contains(x, y)) { touchRow = -2; return true; }
                for (int i = 0; i < rows.length; i++) if (rowRect(i).contains(x, y)) { touchRow = i; focus = i; invalidate(); break; }
                return true;
            case MotionEvent.ACTION_UP:
                if (touchRow == -2 && closeRect().contains(x, y)) back();
                else if (touchRow >= 0 && touchRow < rows.length && rowRect(touchRow).contains(x, y)) select();
                touchRow = -1;
                return true;
            case MotionEvent.ACTION_CANCEL: touchRow = -1; return true;
            default: return true;
        }
    }

    // ------------------------------------------------------------------ layout
    private RectF rowRect(int i) {
        float top = 196, h = 92, gap = 16;
        return new RectF(150, top + i * (h + gap), 1770, top + i * (h + gap) + h);
    }

    private RectF closeRect() { return new RectF(1540, 72, 1800, 136); }

    // ------------------------------------------------------------------ drawing
    @Override protected void onDraw(Canvas c) {
        long now = SystemClock.uptimeMillis();
        float dt = lastFrame == 0 ? 0 : Math.min(0.1f, (now - lastFrame) / 1000f);
        lastFrame = now;
        if (stickDir != 0 && now >= nextRepeat) { move(stickDir); nextRepeat = now + 90; }
        float t = (now - born) / 1000f;
        float scale = Math.min(getWidth() / W, getHeight() / H);
        c.drawColor(0xB4000000);  // the frozen game, dimmed
        c.save();
        c.translate((getWidth() - W * scale) / 2, (getHeight() - H * scale) / 2);
        c.scale(scale, scale);
        float open = Math.min(1, t / 0.14f);
        c.translate(0, 20 * (float) Math.pow(1 - open, 3));
        window(c);
        header(c);
        footer(c);
        Row[] rows = rows();
        if (focus >= rows.length) focus = rows.length - 1;
        for (int i = 0; i < rows.length; i++) {
            lift[i] += ((i == focus ? 1 : 0) - lift[i]) * Math.min(1, dt / 0.08f);
            row(c, rows[i], rowRect(i), i == focus, lift[i]);
        }
        String[] help = help();
        float y = rowRect(Math.max(0, rows.length - 1)).bottom + 64;
        for (String line : help) { text(rowFont, line, 160, y, 30, 0xFFB9C0CC, false, 1600); y += 46; }
        c.restore();
        postInvalidateOnAnimation();
    }

    /** The window: Brawl's charcoal with faint diagonal light, a silver rim. */
    private void window(Canvas c) {
        r.set(70, 45, 1850, 1035);
        shade(new LinearGradient(0, 45, 0, 1035, 0xFF262A32, 0xFF0C0E12, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, 18, 18, paint);
        paint.setShader(null);
        c.save();
        path.reset(); path.addRoundRect(r, 18, 18, Path.Direction.CW); c.clipPath(path);
        paint.setColor(0x09FFFFFF);
        for (int i = -6; i < 14; i++) {
            float x = i * 170f;
            path.reset();
            path.moveTo(x, 45); path.lineTo(x + 70, 45); path.lineTo(x - 330, 1035); path.lineTo(x - 400, 1035); path.close();
            c.drawPath(path, paint);
        }
        // The Smash emblem, large and faint, behind the rows.
        if (emblem != null) {
            paint.setColorFilter(whiteShape(0x10FFFFFF));
            r.set(1180, 300, 1840, 960);
            c.drawBitmap(emblem, null, r, paint);
            paint.setColorFilter(null);
        }
        c.restore();
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(5);
        shade(new LinearGradient(0, 45, 0, 1035, 0xFFE4E8EE, 0xFF7C838F, Shader.TileMode.CLAMP));
        r.set(72, 47, 1848, 1033); c.drawRoundRect(r, 17, 17, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);
    }

    /** The title band: brushed metal, like the plate behind the Smash Bros. logo. */
    private void header(Canvas c) {
        c.save();
        r.set(70, 45, 1850, 1035); path.reset(); path.addRoundRect(r, 18, 18, Path.Direction.CW); c.clipPath(path);
        shade(new LinearGradient(0, 45, 0, 160, 0xFFF1F3F6, 0xFF9AA1AC, Shader.TileMode.CLAMP));
        c.drawRect(70, 45, 1850, 160, paint);
        paint.setShader(null);
        paint.setColor(0x0E000000);
        for (float y = 48; y < 160; y += 4) c.drawRect(70, y, 1850, y + 1.5f, paint);
        paint.setColor(0xFFE2242C);  // Brawl's red edge under the plate
        c.drawRect(70, 160, 1850, 166, paint);
        c.restore();
        if (emblem != null) {
            paint.setColorFilter(whiteShape(0xFF22262E));
            r.set(118, 62, 198, 142);
            c.drawBitmap(emblem, null, r, paint);
            paint.setColorFilter(null);
        }
        text(titleFont, TITLES[page], 222, 132, 66, 0xFF20242C, false, 1200);
        // Close / Back: a dark key with the B glyph.
        RectF close = closeRect();
        paint.setColor(0xFF22262E);
        c.drawRoundRect(close, 32, 32, paint);
        button(c, "B", close.left + 44, close.centerY(), 22, 0xFFF4F4F4, 0xFF22262E);
        text(rowFont, page == PAGE_ROOT ? "Close" : "Back", close.left + 82, close.centerY() + 11, 32, Color.WHITE, false, 160);
    }

    /** Hints along the bottom. */
    private void footer(Canvas c) {
        c.save();
        r.set(70, 45, 1850, 1035); path.reset(); path.addRoundRect(r, 18, 18, Path.Direction.CW); c.clipPath(path);
        paint.setColor(0xFF15181E);
        c.drawRect(70, 960, 1850, 1035, paint);
        paint.setColor(0x55FFFFFF);
        c.drawRect(70, 960, 1850, 962, paint);
        c.restore();
        float x = 130, y = 1009;
        button(c, "A", x + 22, 997, 22, 0xFFF4F4F4, 0xFF15181E); x += 58;
        x = text(rowFont, "Select", x, y, 30, Color.WHITE, false, 300) + 56;
        button(c, "B", x + 22, 997, 22, 0xFFF4F4F4, 0xFF15181E); x += 58;
        x = text(rowFont, "Back", x, y, 30, Color.WHITE, false, 300) + 56;
        if (page != PAGE_ROOT && page != PAGE_SAVES) x = text(rowFont, "Left / Right: Change", x, y, 30, Color.WHITE, false, 500) + 56;
        text(rowFont, "Start: Resume", x, y, 30, Color.WHITE, false, 400);
    }

    /** A row: a slanted slate plate; the focused one bright silver with Brawl's red edge. */
    private void row(Canvas c, Row row, RectF at, boolean focused, float liftAmount) {
        float skew = 22;
        RectF b = new RectF(at.left + 10 * liftAmount, at.top, at.right + 10 * liftAmount, at.bottom);
        path.reset();
        path.moveTo(b.left + skew, b.top); path.lineTo(b.right, b.top); path.lineTo(b.right - skew, b.bottom);
        path.lineTo(b.left, b.bottom); path.close();
        c.save();
        c.translate(5, 7);
        paint.setColor(0x66000000);
        c.drawPath(path, paint);
        c.restore();
        shade(new LinearGradient(0, b.top, 0, b.bottom, focused ? 0xFFFFFFFF : 0xFF3A404B,
                focused ? 0xFFC9CFD8 : 0xFF23272F, Shader.TileMode.CLAMP));
        c.drawPath(path, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        paint.setColor(focused ? 0xFFFFFFFF : 0xFF59606D);
        c.drawPath(path, paint);
        paint.setStyle(Paint.Style.FILL);
        if (focused) {
            // Brawl's red cut down the left edge of the selection.
            path.reset();
            path.moveTo(b.left + skew, b.top); path.lineTo(b.left + skew + 16, b.top); path.lineTo(b.left + 16, b.bottom);
            path.lineTo(b.left, b.bottom); path.close();
            paint.setColor(0xFFE2242C);
            c.drawPath(path, paint);
        }
        int ink = focused ? 0xFF14171C : Color.WHITE;
        float baseline = b.centerY() + 14, size = 40;
        boolean centred = row.kind == ACTION && page == PAGE_ROOT;
        if (centred) { text(rowFont, row.label, b.centerX(), baseline, size, ink, true, b.width() - 120); return; }
        text(rowFont, row.label, b.left + 60, baseline, size, ink, false, b.width() * 0.55f);
        float right = b.right - 60;
        if (row.kind == OPEN) { arrow(c, right, b.centerY(), 15, 1, ink); return; }
        if (row.kind == ACTION) return;
        String value = row.kind == TOGGLE ? (host.get(row.key) != 0 ? "On" : "Off") : row.names[Math.max(0,
                Math.min(row.names.length - 1, host.get(row.key)))];
        int valueInk = row.kind == TOGGLE && host.get(row.key) != 0 ? (focused ? 0xFFB3141B : 0xFFFF6A6F) : ink;
        float vw = width(rowFont, value, size);
        if (row.kind == CHOICE) {
            arrow(c, right, b.centerY(), 12, 1, ink);
            text(rowFont, value, right - 34 - vw, baseline, size, valueInk, false, 700);
            arrow(c, right - 64 - vw, b.centerY(), 12, -1, ink);
        } else {
            text(rowFont, value, right - vw, baseline, size, valueInk, false, 700);
        }
    }

    /** A round face-button glyph with its letter. */
    private void button(Canvas c, String letter, float cx, float cy, float radius, int fill, int ink) {
        paint.setColor(fill);
        c.drawCircle(cx, cy, radius, paint);
        text(rowFont, letter, cx, cy + radius * 0.48f, radius * 1.35f, ink, true, radius * 2);
    }

    private void arrow(Canvas c, float x, float cy, float size, int dir, int color) {
        float back = x - dir * size;
        path.reset();
        path.moveTo(back, cy - size); path.lineTo(back, cy + size); path.lineTo(x, cy); path.close();
        paint.setColor(color);
        c.drawPath(path, paint);
    }

    /** The emblem textures are white on black: brightness becomes the shape, filled with `color`. */
    private static ColorMatrixColorFilter whiteShape(int color) {
        float a = Color.alpha(color) / 255f;
        return new ColorMatrixColorFilter(new float[] {
            0, 0, 0, 0, Color.red(color), 0, 0, 0, 0, Color.green(color), 0, 0, 0, 0, Color.blue(color),
            0.33f * a, 0.33f * a, 0.33f * a, 0, 0});
    }

    private void shade(Shader shader) { paint.setColor(0xFFFFFFFF); paint.setShader(shader); }

    private float width(GameFont font, String value, float size) {
        if (font != null && font.covers(value)) return font.width(value, size / font.cellHeight, 0);
        fallback.setTextSize(size * 0.8f);
        return fallback.measureText(value);
    }

    /** Text in one of the game's fonts at `size` (its cell height), tinted; returns the end x. */
    private float text(GameFont font, String value, float x, float y, float size, int color, boolean centred, float maxWidth) {
        if (value == null || value.isEmpty()) return x;
        if (font == null || !font.covers(value)) {
            fallback.setTextSize(size * 0.8f); fallback.setColor(color);
            float w = fallback.measureText(value);
            float left = centred ? x - w / 2 : x;
            canvasDraw(value, left, y);
            return left + w;
        }
        float s = size / font.cellHeight;
        float w = font.width(value, s, 0);
        if (w > maxWidth && w > 0) { s *= maxWidth / w; w = maxWidth; }
        float left = centred ? x - w / 2 : x;
        glyph.setColorFilter(new PorterDuffColorFilter(color | 0xFF000000, PorterDuff.Mode.SRC_IN));
        glyph.setAlpha(Color.alpha(color));
        float end = font.draw(currentCanvas, value, left, y, s, 0, glyph);
        glyph.setColorFilter(null);
        glyph.setAlpha(255);
        return end;
    }

    private Canvas currentCanvas;
    private void canvasDraw(String value, float x, float y) { if (currentCanvas != null) currentCanvas.drawText(value, x, y, fallback); }

    @Override public void draw(Canvas canvas) {
        currentCanvas = canvas;
        super.draw(canvas);
        currentCanvas = null;
    }
}
