package com.driftdroid.android.dualscreen;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;

import java.util.ArrayList;
import java.util.List;
import java.util.function.IntConsumer;
import java.util.function.Supplier;

/**
 * The WiiDS pause menu as one drawn surface. It never takes window focus: the game Activity keeps
 * controller focus and forwards keys and stick motion here while a page is open, so moving the menu
 * between the lower display and the game display is only a change of parent view.
 */
public final class WiiDsMenuView extends View {
    public static final int TILE = 0, ROW = 1, KEY = 2;

    public static final class Item {
        final int kind;
        final String label, glyph;
        Supplier<String> value;
        Runnable action;
        IntConsumer adjust;
        boolean danger, primary;
        final RectF bounds = new RectF();

        Item(int kind, String label, String glyph) { this.kind = kind; this.label = label; this.glyph = glyph; }
    }

    public static final class Page {
        final String title;
        final List<Item> items = new ArrayList<>();
        Supplier<String> note;
        Runnable back;
        int focus;

        public Page(String title) { this.title = title; }
        public Page note(String message) { note = () -> message; return this; }
        public Page note(Supplier<String> message) { note = message; return this; }
        /** What B does. Without it, B resumes the game. */
        public Page back(Runnable action) { back = action; return this; }
        public Page tile(String label, String glyph, Runnable action) { return add(TILE, label, glyph, action, false); }
        public Page dangerTile(String label, String glyph, Runnable action) { return add(TILE, label, glyph, action, true); }
        public Page key(String label, String glyph, Runnable action) { return add(KEY, label, glyph, action, false); }
        public Page primaryKey(String label, String glyph, Runnable action) {
            add(KEY, label, glyph, action, false);
            items.get(items.size() - 1).primary = true;
            return this;
        }
        /** A setting. A and tap step forward; left and right step either way. */
        public Page row(String label, Supplier<String> value, IntConsumer adjust) {
            Item item = new Item(ROW, label, "");
            item.value = value;
            item.adjust = adjust;
            item.action = () -> adjust.accept(1);
            items.add(item);
            return this;
        }
        public Page focus(int index) { focus = index; return this; }

        private Page add(int kind, String label, String glyph, Runnable action, boolean danger) {
            Item item = new Item(kind, label, glyph);
            item.action = action;
            item.danger = danger;
            items.add(item);
            return this;
        }
    }

    private final WiiDsSkin skin;
    private final String game;
    private final Bitmap art;
    private final Runnable closed;
    private final RectF panel = new RectF(), header = new RectF(), hint = new RectF(), body = new RectF(), noteBox = new RectF();
    private Page page;
    private boolean overlay;
    private float u = 1;
    private int pressed = -1;
    private long openedAt, stickAt;
    private boolean laidOut;

    /** {@code closed} runs once whenever the menu goes from open to closed, on the UI thread. */
    public WiiDsMenuView(Context context, WiiDsSkin skin, String game, Bitmap art, Runnable closed) {
        super(context);
        this.skin = skin;
        this.game = game;
        this.art = art;
        this.closed = closed;
        setVisibility(GONE);
        setFocusable(false);
        setClickable(true); // swallow touches aimed at whatever is underneath
        setContentDescription("WiiDS menu");
    }

    public boolean isOpen() { return page != null; }

    /** Opens the menu or moves to another page. */
    public void show(Page next) {
        boolean opening = page == null;
        page = next;
        pressed = -1;
        laidOut = false;
        if (opening) openedAt = SystemClock.uptimeMillis();
        setVisibility(VISIBLE);
        announceForAccessibility(next.title);
        invalidate();
    }

    public void close() {
        if (page == null) return;
        page = null;
        pressed = -1;
        setVisibility(GONE);
        closed.run();
    }

    /** Re-parents the same menu, keeping the page and focus. {@code overlay} draws over a paused game. */
    public void attach(ViewGroup parent, boolean overlay) {
        ViewGroup current = (ViewGroup) getParent();
        this.overlay = overlay;
        laidOut = false;
        pressed = -1;
        if (current != parent) {
            if (current != null) current.removeView(this);
            parent.addView(this, new ViewGroup.LayoutParams(-1, -1));
        }
        invalidate();
    }

    public void detach() {
        ViewGroup current = (ViewGroup) getParent();
        if (current != null) current.removeView(this);
        pressed = -1;
    }

    public void refresh() { laidOut = false; invalidate(); }

    // Input from the owning Activity.

    /** Returns true when the menu consumed the key. Call only while {@link #isOpen()}. */
    public boolean key(KeyEvent event) {
        if (page == null) return false;
        int code = event.getKeyCode();
        boolean down = event.getAction() == KeyEvent.ACTION_DOWN, up = event.getAction() == KeyEvent.ACTION_UP;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: if (down) move(0, -1); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: if (down) move(0, 1); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: if (down) move(-1, 0); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: if (down) move(1, 0); return true;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
                if (down && event.getRepeatCount() == 0) { pressed = page.focus; invalidate(); }
                if (up) { boolean armed = pressed == page.focus; pressed = -1; invalidate(); if (armed) activate(page.focus); }
                return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE: case KeyEvent.KEYCODE_BUTTON_SELECT:
                if (up) back();
                return true;
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_BUTTON_MODE:
                if (up) close();
                return true;
            default:
                return KeyEvent.isGamepadButton(code);
        }
    }

    /** Left stick and hat navigation with a 220 ms repeat. */
    public boolean stick(MotionEvent event) {
        if (page == null || !event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.getActionMasked() != MotionEvent.ACTION_MOVE) return false;
        float x = event.getAxisValue(MotionEvent.AXIS_X), y = event.getAxisValue(MotionEvent.AXIS_Y);
        if (Math.abs(x) < 0.6f && Math.abs(y) < 0.6f) { stickAt = 0; return true; }
        long now = SystemClock.uptimeMillis();
        if (now - stickAt < 220) return true;
        stickAt = now;
        if (Math.abs(y) >= Math.abs(x)) move(0, y < 0 ? -1 : 1); else move(x < 0 ? -1 : 1, 0);
        return true;
    }

    private void back() {
        if (page == null) return;
        if (page.back != null) page.back.run(); else close();
    }

    private void activate(int index) {
        if (page == null || index < 0 || index >= page.items.size()) return;
        Runnable action = page.items.get(index).action;
        if (action != null) action.run();
        invalidate();
    }

    private int firstKey() {
        for (int i = 0; i < page.items.size(); i++) if (page.items.get(i).kind == KEY) return i;
        return page.items.size();
    }

    private void move(int dx, int dy) {
        int count = page.items.size();
        if (count == 0) return;
        int keys = firstKey(), focus = Math.max(0, Math.min(count - 1, page.focus));
        Item item = page.items.get(focus);
        int next = focus;
        if (item.kind == KEY) {
            if (dx != 0) next = Math.max(keys, Math.min(count - 1, focus + dx));
            else if (dy < 0 && keys > 0) next = keys - 1;
        } else if (item.kind == ROW) {
            if (dx != 0 && item.adjust != null) { item.adjust.accept(dx); invalidate(); return; }
            if (dy != 0) next = focus + dy;
        } else {
            int columns = columns(keys);
            if (dx != 0) { int target = focus + dx; if (target >= 0 && target < keys && target / columns == focus / columns) next = target; }
            if (dy != 0) { int target = focus + dy * columns; next = target < 0 ? focus : target >= keys ? (keys < count ? keys : focus) : target; }
        }
        next = Math.max(0, Math.min(count - 1, next));
        if (next != page.focus) { page.focus = next; invalidate(); }
    }

    private int columns(int tiles) { return tiles <= 1 ? 1 : 2; }

    // Touch. The first tap acts; there is no focus step.

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (page == null) return false;
        int hit = -1;
        for (int i = 0; i < page.items.size(); i++) if (page.items.get(i).bounds.contains(event.getX(), event.getY())) hit = i;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pressed = hit;
                if (hit >= 0) page.focus = hit;
                break;
            case MotionEvent.ACTION_MOVE:
                if (hit != pressed) pressed = -1;
                break;
            case MotionEvent.ACTION_UP: {
                int target = pressed;
                pressed = -1;
                if (target >= 0 && target == hit) {
                    Item item = page.items.get(target);
                    // The value sits in the right 46% of a setting row; its left end steps back.
                    float valueLeft = item.bounds.right - item.bounds.width() * 0.46f;
                    if (item.kind == ROW && item.adjust != null && event.getX() >= valueLeft
                            && event.getX() < valueLeft + item.bounds.width() * 0.16f) item.adjust.accept(-1);
                    else activate(target);
                }
                break;
            }
            default:
                pressed = -1;
        }
        invalidate();
        return true;
    }

    // Layout and drawing.

    private void layout() {
        float w = getWidth(), h = getHeight();
        if (overlay) {
            float ph = h * 0.92f, pw = Math.min(w * 0.94f, ph * 1.34f);
            panel.set((w - pw) / 2, (h - ph) / 2, (w + pw) / 2, (h + ph) / 2);
        } else {
            float inset = Math.min(w, h) * 0.035f;
            panel.set(inset, inset, w - inset, h - inset);
        }
        u = Math.min(panel.width(), panel.height()) / 100f;
        float pad = u * 5;
        header.set(panel.left + pad, panel.top + pad, panel.right - pad, panel.top + pad + u * 15);
        hint.set(panel.left + pad, panel.bottom - pad - u * 6, panel.right - pad, panel.bottom - pad);
        body.set(panel.left + pad, header.bottom + u * 3.5f, panel.right - pad, hint.top - u * 3);
        if (page == null) return;
        int count = page.items.size(), keys = firstKey();
        float top = body.top, bottom = body.bottom, gap = u * 2.6f;
        if (keys < count) {
            float keyHeight = u * 14, width = (body.width() - gap * (count - keys - 1)) / (count - keys);
            for (int i = keys; i < count; i++) {
                float left = body.left + (i - keys) * (width + gap);
                page.items.get(i).bounds.set(left, bottom - keyHeight, left + width, bottom);
            }
            bottom -= keyHeight + gap;
        }
        String message = page.note == null ? null : page.note.get();
        noteBox.setEmpty();
        boolean rows = keys > 0 && page.items.get(0).kind == ROW;
        if (message != null && !message.isEmpty()) {
            float height = keys == 0 ? bottom - top : Math.min(skin.noteHeight(message, body.width(), u), (bottom - top) * 0.45f);
            noteBox.set(body.left, top, body.right, top + height);
            top += height + gap;
        }
        if (keys == 0) return;
        if (rows) {
            float height = Math.min(u * 14, (bottom - top - gap * (keys - 1)) / keys);
            for (int i = 0; i < keys; i++) {
                page.items.get(i).bounds.set(body.left, top, body.right, top + height);
                top += height + gap;
            }
        } else {
            int columns = columns(keys), lines = (keys + columns - 1) / columns;
            float width = (body.width() - gap * (columns - 1)) / columns, height = (bottom - top - gap * (lines - 1)) / lines;
            for (int i = 0; i < keys; i++) {
                float left = body.left + (i % columns) * (width + gap), y = top + (i / columns) * (height + gap);
                page.items.get(i).bounds.set(left, y, left + width, y + height);
            }
        }
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) { laidOut = false; }

    @Override protected void onDraw(Canvas c) {
        if (page == null || getWidth() == 0) return;
        if (!laidOut) { layout(); laidOut = true; }
        boolean motion = WiiDsSkin.motion();
        float t = motion ? Math.min(1f, (SystemClock.uptimeMillis() - openedAt) / 160f) : 1f, ease = 1f - (1f - t) * (1f - t);
        if (overlay) c.drawColor(WiiDsSkin.alpha(skin.scrim(), ease));
        else skin.backdrop(c, getWidth(), getHeight(), u);
        int layer = c.saveLayerAlpha(0, 0, getWidth(), getHeight(), Math.round(255 * ease));
        float scale = 0.96f + 0.04f * ease;
        c.scale(scale, scale, panel.centerX(), panel.centerY());
        skin.panel(c, panel, u);
        skin.header(c, header, page.title, game, art, u);
        String message = page.note == null ? null : page.note.get();
        if (!noteBox.isEmpty() && message != null) skin.note(c, noteBox, message, u);
        int focus = Math.max(0, Math.min(page.items.size() - 1, page.focus));
        for (int i = 0; i < page.items.size(); i++) {
            Item item = page.items.get(i);
            int state = (i == focus ? WiiDsSkin.FOCUSED : 0) | (i == pressed ? WiiDsSkin.PRESSED : 0);
            if (item.kind == TILE) skin.tile(c, item.bounds, item.label, item.glyph, state, item.danger, u);
            else if (item.kind == ROW) skin.row(c, item.bounds, item.label, item.value == null ? "" : item.value.get(), state, item.adjust != null, u);
            else skin.key(c, item.bounds, item.label, item.glyph, state, item.primary, u);
        }
        if (!page.items.isEmpty()) skin.focus(c, page.items.get(focus).bounds, WiiDsSkin.pulse(), u);
        boolean rows = !page.items.isEmpty() && page.items.get(focus).kind == ROW;
        skin.hint(c, hint, rows ? "◀ ▶  Change      B  Back      START  Resume" : "A  Select      B  Back      START  Resume", u);
        c.restoreToCount(layer);
        if (motion) { if (t < 1) postInvalidateOnAnimation(); else postInvalidateDelayed(40); }
    }
}
