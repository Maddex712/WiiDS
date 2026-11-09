package dev.thor.mkdd;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.List;

/**
 * Double Dash's WiiDS menu, opened by Back over the frozen game (the twin of Brawl's): drawn on a
 * 1920x1080 grid in the duo screen's material, the game's own lettering from the owner's disc on
 * a quiet graphite window, the selection in the gold the game's rank numerals use.
 *
 * D-pad or stick move, Left / Right change a value, A selects, B or Back goes back, Start closes.
 * Both side face buttons go back, so B works in either face-button layout.
 */
final class DuoMenu extends View {
    static final float W = 1920, H = 1080;

    // Settings (Host.get / Host.set).
    static final int LAYOUT = 0, SCALE = 1, ASPECT = 2;
    // Actions.
    static final int RESUME = 100, LEAVE = 101, RESTART = 102, BACK_UP = 103, RESTORE = 200;  // RESTORE + backup index

    static final int PAGE_ROOT = 0, PAGE_CONTROLS = 1, PAGE_DISPLAY = 2, PAGE_SAVES = 3;
    private static final String[] TITLES = {"WiiDS menu", "Controls", "Display", "Saves"};
    private static final int OPEN = 0, CHOICE = 1, ACTION = 2;
    private static final String[] LAYOUTS = {"GameCube", "Standard"};
    private static final String[] SCALES = {"1x 480p", "2x 960p", "3x 1440p"};
    private static final String[] ASPECTS = {"4:3", "16:9"};

    interface Host {
        int get(int key);
        void set(int key, int value);
        void act(int action);
        /** Restart now (a new resolution, a restore) needs WiiDS to start the game again. */
        boolean canRestart();
        /** The render scale this run started with. */
        int runningScale();
        String leaveLabel();
        /** Backups, newest first, as labels. */
        List<String> backups();
        /** A line for the Saves page after Back up now, or null. */
        String savesNote();
    }

    private static final int BG = 0xf2121214, RAISED = 0xff232326, EDGE = 0x1affffff, DIM = 0x99000000,
        PAPER = 0xfff2efe6, MUTED = 0xff8b8b92, GOLD_TOP = 0xfffff6c8, GOLD_BOTTOM = 0xffd8b858;
    private static final Typeface BODY = Typeface.create("sans-serif", Typeface.NORMAL);

    private final Host host;
    private final DuoLettering type = new DuoLettering();
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final RectF rect = new RectF();
    private final Path path = new Path();
    private final int[] lastFocus = new int[4];
    private int page = PAGE_ROOT, focus, confirmRestore = -1, hatX, hatY, stickY;
    private float scale = 1, left, top;
    // This page's rows, rebuilt by rows().
    private final List<Integer> kinds = new ArrayList<>(), keys = new ArrayList<>();
    private final List<String> labels = new ArrayList<>();
    private final List<String[]> choices = new ArrayList<>();

    DuoMenu(Context context, DuoArt art, Host host) {
        super(context);
        this.host = host;
        type.setArt(art);
        setFocusable(true);
        setFocusableInTouchMode(true);
        text.setTypeface(BODY);
    }

    // ---- pages ----

    private void rows() {
        kinds.clear(); keys.clear(); labels.clear(); choices.clear();
        switch (page) {
            case PAGE_ROOT:
                row(ACTION, RESUME, "Resume", null);
                row(OPEN, PAGE_CONTROLS, "Controls", null);
                row(OPEN, PAGE_DISPLAY, "Display", null);
                row(OPEN, PAGE_SAVES, "Saves", null);
                row(ACTION, LEAVE, host.leaveLabel(), null);
                break;
            case PAGE_CONTROLS:
                row(CHOICE, LAYOUT, "Face buttons", LAYOUTS);
                break;
            case PAGE_DISPLAY:
                row(CHOICE, ASPECT, "Aspect ratio", ASPECTS);
                row(CHOICE, SCALE, "Resolution", SCALES);
                if (host.canRestart() && host.get(SCALE) != host.runningScale() - MkddSettings.MIN_SCALE)
                    row(ACTION, RESTART, "Restart now", null);
                break;
            case PAGE_SAVES: {
                row(ACTION, BACK_UP, "Back up now", null);
                List<String> backups = host.backups();
                for (int i = 0; i < backups.size(); i++)
                    row(ACTION, RESTORE + i, confirmRestore == i ? "Press A again to restore" : "Restore " + backups.get(i), null);
                break;
            }
            default: break;
        }
        if (focus >= kinds.size()) focus = Math.max(0, kinds.size() - 1);
    }

    private void row(int kind, int key, String label, String[] names) {
        kinds.add(kind); keys.add(key); labels.add(label); choices.add(names);
    }

    private String[] help() {
        switch (page) {
            case PAGE_ROOT: return new String[]{"The game is paused while this menu is open. Settings save as they change."};
            case PAGE_CONTROLS: return new String[]{host.get(LAYOUT) == 0
                    ? "GameCube: A bottom, B left, X right, Y top, the way a GameCube pad sits."
                    : "Standard: A bottom, B right, X left, Y top, as printed on the Thor.",
                    "R1 is Z (swap riders). L2 and R2 are the analog L and R."};
            case PAGE_DISPLAY:
                if (!keys.isEmpty() && keys.get(focus) == ASPECT && kinds.get(focus) == CHOICE)
                    return new String[]{"16:9 shows more of the course at the sides. It changes at once.",
                            "The race HUD fits the wider screen. Icons keep their shape; menus stay centred."};
                return new String[]{"The game draws at this many times the GameCube's 480 lines.",
                    host.get(SCALE) == host.runningScale() - MkddSettings.MIN_SCALE
                            ? "Higher looks sharper and uses more battery."
                            : "Applies when Double Dash starts again."};
            case PAGE_SAVES: {
                String note = host.savesNote();
                return new String[]{note != null ? note : "A backup is made each time Double Dash starts; the last "
                        + MkddSaves.KEEP + " are kept.", "Restoring backs up the current card first, then restarts the game."};
            }
            default: return new String[0];
        }
    }

    // ---- input ----

    private void move(int dir) {
        rows();
        if (kinds.isEmpty()) return;
        if (dir == -1 && focus > 0) { focus--; confirmRestore = -1; }
        else if (dir == 1 && focus < kinds.size() - 1) { focus++; confirmRestore = -1; }
        else if ((dir == -2 || dir == 2) && kinds.get(focus) == CHOICE) {
            int key = keys.get(focus), n = choices.get(focus).length;
            int next = Math.max(0, Math.min(n - 1, host.get(key) + (dir > 0 ? 1 : -1)));
            if (next != host.get(key)) host.set(key, next);
        }
        invalidate();
    }

    private void select() {
        rows();
        if (kinds.isEmpty()) return;
        int kind = kinds.get(focus), key = keys.get(focus);
        if (kind == OPEN) { lastFocus[page] = focus; page = key; focus = 0; confirmRestore = -1; }
        else if (kind == CHOICE) host.set(key, (host.get(key) + 1) % choices.get(focus).length);
        else if (key >= RESTORE) {
            if (confirmRestore == key - RESTORE) { confirmRestore = -1; host.act(key); }
            else confirmRestore = key - RESTORE;
        } else host.act(key);
        invalidate();
    }

    private void back() {
        if (page == PAGE_ROOT) { host.act(RESUME); return; }
        page = PAGE_ROOT; focus = lastFocus[PAGE_ROOT]; confirmRestore = -1;
        invalidate();
    }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: move(-1); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(1); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: move(-2); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: move(2); return true;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
                if (e.getRepeatCount() == 0) select();
                return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BUTTON_X: case KeyEvent.KEYCODE_BACK:
                if (e.getRepeatCount() == 0) back();
                return true;
            case KeyEvent.KEYCODE_BUTTON_START:
                if (e.getRepeatCount() == 0) host.act(RESUME);
                return true;
            case KeyEvent.KEYCODE_VOLUME_UP: case KeyEvent.KEYCODE_VOLUME_DOWN: case KeyEvent.KEYCODE_VOLUME_MUTE:
                return false;
            default: return true;  // the game is frozen underneath: nothing else should react
        }
    }

    @Override public boolean onKeyUp(int code, KeyEvent e) {
        return code != KeyEvent.KEYCODE_VOLUME_UP && code != KeyEvent.KEYCODE_VOLUME_DOWN && code != KeyEvent.KEYCODE_VOLUME_MUTE;
    }

    /** The Thor's d-pad arrives as a hat; the stick moves up and down too. Edges only. */
    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE) return super.onGenericMotionEvent(e);
        int hx = Math.round(e.getAxisValue(MotionEvent.AXIS_HAT_X)), hy = Math.round(e.getAxisValue(MotionEvent.AXIS_HAT_Y));
        float sy = e.getAxisValue(MotionEvent.AXIS_Y);
        int stick = sy < -0.6f ? -1 : sy > 0.6f ? 1 : Math.abs(sy) < 0.3f ? 0 : stickY;
        if (hy != hatY && hy != 0) move(hy);
        if (hx != hatX && hx != 0) move(hx * 2);
        if (stick != stickY && stick != 0) move(stick);
        hatX = hx; hatY = hy; stickY = stick;
        return true;
    }

    // ---- drawing ----

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        scale = Math.min(w / W, h / H);
        left = (w - W * scale) * .5f; top = (h - H * scale) * .5f;
        type.place(left, top, scale);
    }

    @Override protected void onDraw(Canvas c) {
        rows();
        fill.setColor(DIM); c.drawRect(0, 0, getWidth(), getHeight(), fill);
        box(c, 480, 136, 1440, 944, 20, BG);
        stroke(c, 480, 136, 1440, 944, 20);
        type.letters(c, TITLES[page], 528, 184, 56, PAPER, PAPER, DuoLettering.LEFT, 864);
        float y = 296;
        for (int i = 0; i < kinds.size(); i++, y += 80) {
            boolean on = i == focus;
            if (on) box(c, 512, y, 1408, y + 72, 12, RAISED);
            int top = on ? GOLD_TOP : PAPER, bottom = on ? GOLD_BOTTOM : PAPER;
            String[] names = choices.get(i);
            float labelMax = names != null ? 480 : 816;
            type.letters(c, labels.get(i), 544, y + 18, 36, top, bottom, DuoLettering.LEFT, labelMax);
            if (names != null) {
                int index = Math.max(0, Math.min(names.length - 1, host.get(keys.get(i))));
                String value = names[index];
                float right = on ? 1336 : 1376;
                type.letters(c, value, right, y + 20, 32, on ? PAPER : MUTED, on ? PAPER : MUTED, DuoLettering.RIGHT, 0);
                if (on) {
                    float w = type.width(value, 32);
                    if (index > 0) arrow(c, right - w - 28, y + 36, -1);
                    if (index < names.length - 1) arrow(c, 1360, y + 36, 1);
                }
            } else if (kinds.get(i) == OPEN) arrow(c, 1368, y + 36, 1);
        }
        String[] lines = help();
        float hy = 816;
        for (String line : lines) { body(c, line, 528, hy, 26); hy += 40; }
    }

    private void box(Canvas c, float l, float t, float r, float b, float radius, int colour) {
        rect.set(left + l * scale, top + t * scale, left + r * scale, top + b * scale);
        fill.setColor(colour); c.drawRoundRect(rect, radius * scale, radius * scale, fill);
    }

    private void stroke(Canvas c, float l, float t, float r, float b, float radius) {
        rect.set(left + l * scale, top + t * scale, left + r * scale, top + b * scale);
        fill.setColor(EDGE); fill.setStyle(Paint.Style.STROKE); fill.setStrokeWidth(Math.max(1, 1.5f * scale));
        c.drawRoundRect(rect, radius * scale, radius * scale, fill);
        fill.setStyle(Paint.Style.FILL);
    }

    /** A small triangle pointing left (-1) or right (1) around a changeable value. */
    private void arrow(Canvas c, float x, float y, int dir) {
        float s = 10 * scale, cx = left + x * scale, cy = top + y * scale;
        path.reset();
        path.moveTo(cx + dir * s, cy);
        path.lineTo(cx - dir * s * .6f, cy - s);
        path.lineTo(cx - dir * s * .6f, cy + s);
        path.close();
        fill.setColor(MUTED); c.drawPath(path, fill);
    }

    /** Help sentences: a plain sans in the muted text colour, so long lines stay easy to read. */
    private void body(Canvas c, String s, float x, float y, float size) {
        text.setTextSize(size * scale); text.setColor(MUTED);
        float max = 864 * scale, w = text.measureText(s);
        if (w > max) text.setTextSize(size * scale * max / w);
        c.drawText(s, left + x * scale, top + (y + size) * scale, text);
    }
}
