package dev.wiids.cityfolk;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

import com.driftdroid.android.dualscreen.WiiDsSkin;

/**
 * The Animal Crossing lower screen, a town noticeboard with one sheet pinned to it. In town the
 * sheet is the live map. When the game needs the Wii Remote pointer (title, Rover, choices, naming,
 * Pockets, the game's own map) the sheet becomes the aiming surface; the pointer itself is drawn
 * only by the game on the upper screen.
 */
final class CityFolkBoard extends View {
    private static final int MAP = 0, POINT = 1;
    private static final int KEY_MENU = 0, KEY_MAP = 1, KEY_POINT = 2, KEY_POCKETS = 3, KEY_SELECT = 4, KEY_BACK = 5;

    private final TownSkin skin = new TownSkin();
    private final CityFolkGameActivity owner;
    private final CityFolkTownMap map;
    private final RectF sheet = new RectF(), surface = new RectF(), top = new RectF(), r = new RectF();
    private final RectF[] keys = {new RectF(), new RectF(), new RectF(), new RectF(), new RectF(), new RectF()};
    private final CityFolkPockets pockets;
    private final com.driftdroid.android.dualscreen.WiiDsBlackout blackout = new com.driftdroid.android.dualscreen.WiiDsBlackout();
    private final CityFolkState state = new CityFolkState();
    private int view = MAP, pressed = -1, aiming = MotionEvent.INVALID_POINTER_ID, buttonHeld;
    // Trackpad, as in Super Mario Galaxy's lower screen: finger motion moves the game's pointer relative to where
    // it is; lifting and touching again continues from there. A full swipe across the pad crosses the whole screen.
    private float cursorX, cursorY, lastX, lastY;
    private float u = 1;
    private static final long MIN_HOLD_MS = 120;
    private long heldSince;
    private boolean pointerOut;
    private int dragging = MotionEvent.INVALID_POINTER_ID;
    private final Runnable release = this::releaseButton;
    private final Runnable pressSelect = this::pressSelect;

    CityFolkBoard(Context context, CityFolkGameActivity owner, CityFolkArt art) {
        super(context);
        this.owner = owner;
        map = new CityFolkTownMap(skin, art);
        pockets = new CityFolkPockets(skin, art);
        setContentDescription("Animal Crossing town noticeboard");
    }

    private void releaseButton() {
        if (buttonHeld != 0) owner.touchButton(buttonHeld, false);
        buttonHeld = 0;
    }

    private void pressSelect() {
        if (buttonHeld != 0 || !showingPockets()) return;
        buttonHeld = CityFolkGameActivity.A;
        heldSince = SystemClock.uptimeMillis();
        owner.touchButton(buttonHeld, true);
        // Held until the finger lifts; if it already lifted, a normal tap length.
        if (dragging == MotionEvent.INVALID_POINTER_ID) postDelayed(release, 160);
    }

    void tick() {
        state.refresh();
        map.refresh();
        pockets.refresh();
        // The pointer belongs to the pointer sheet. Once the map has the sheet again, the game's pointer is put away
        // so A goes back to acting on what the character faces.
        if (pointerOut && aiming == MotionEvent.INVALID_POINTER_ID && showingMap()) { pointerOut = false; owner.aim(0, 0, false); }
        invalidate();
    }

    /** Ends every held touch: the pointer leaves the screen and held buttons are released. */
    void releaseTouches() {
        if (aiming != MotionEvent.INVALID_POINTER_ID) { owner.aim(0, 0, false); pointerOut = false; }
        aiming = MotionEvent.INVALID_POINTER_ID;
        removeCallbacks(release);
        removeCallbacks(pressSelect);
        dragging = MotionEvent.INVALID_POINTER_ID;
        if (buttonHeld != 0) owner.touchButton(buttonHeld, false);
        buttonHeld = 0;
        pressed = -1;
        invalidate();
    }

    /** The map owns the sheet only outdoors in town, and only while the game shows no pointer screen. */
    private boolean mapAvailable() {
        if (state.scene() != CityFolkState.TOWN || !map.ready(state)) return false;
        // While the game's Map screen is open, this sheet may show the map only if the upper copy is not drawn.
        return state.pointerScreen() == CityFolkState.NO_POINTER_SCREEN
                || (state.pointerScreen() == CityFolkState.TOWN_MAP && state.guestMapHidden());
    }

    private boolean guestMapOnLower() { return state.pointerScreen() == CityFolkState.TOWN_MAP && state.guestMapHidden(); }

    /** Pockets belong to this sheet only while the game is not drawing its own window above. */
    private boolean showingPockets() {
        return state.pointerScreen() == CityFolkState.POCKETS && pockets.owned() && !pockets.unreadableChoice();
    }

    private boolean showingMap() { return mapAvailable() && (view == MAP || guestMapOnLower()); }

    private boolean inPlay() {
        int scene = state.scene();
        return scene == CityFolkState.TOWN || scene == CityFolkState.INDOORS || scene == CityFolkState.CITY;
    }

    @Override protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        u = Math.min(w, h) / 100f;
        float pad = u * 4.5f;
        top.set(pad, pad, w - pad, pad + u * 12);
        keys[KEY_MENU].set(top.right - u * 13, top.top, top.right, top.top + u * 12);
        float bottom = h - pad, keyHeight = u * 14;
        sheet.set(pad, top.bottom + u * 3, w - pad, bottom - keyHeight - u * 3.5f);
        surface.set(sheet.left + u * 3, sheet.top + u * 3, sheet.right - u * 3, sheet.bottom - u * 3);
        float gap = u * 2.6f, width = (w - pad * 2 - gap * 2) / 3;
        keys[KEY_MAP].set(pad, bottom - keyHeight, pad + width, bottom);
        keys[KEY_POINT].set(pad + width + gap, bottom - keyHeight, pad + width * 2 + gap, bottom);
        keys[KEY_POCKETS].set(pad + (width + gap) * 2, bottom - keyHeight, w - pad, bottom);
        keys[KEY_SELECT].set(pad, bottom - keyHeight, w - pad, bottom);
        keys[KEY_BACK].set(pad, bottom - keyHeight, w - pad, bottom);
    }

    private boolean keyShown(int key) {
        switch (key) {
            case KEY_MENU: return true;
            case KEY_MAP: return inPlay() && mapAvailable() && !guestMapOnLower();
            case KEY_POINT: return inPlay() && mapAvailable() && !guestMapOnLower();
            case KEY_POCKETS: return inPlay() && state.pointerScreen() == CityFolkState.NO_POINTER_SCREEN;
            case KEY_BACK: return showingPockets() || (guestMapOnLower() && mapAvailable());
            default: return !showingMap() && !showingPockets() && (!inPlay() || state.pointerScreen() != CityFolkState.NO_POINTER_SCREEN);
        }
    }

    private int keyAt(float x, float y) {
        for (int key = 0; key < keys.length; key++) if (keyShown(key) && bounds(key).contains(x, y)) return key;
        return -1;
    }

    /** Without the map, Pockets shares the strip with Select instead of with the two view signs. */
    private RectF bounds(int key) {
        if (key == KEY_POCKETS && !mapAvailable()) {
            r.set(keys[KEY_SELECT].centerX() + u * 1.3f, keys[KEY_SELECT].top, keys[KEY_SELECT].right, keys[KEY_SELECT].bottom);
            return r;
        }
        if (key == KEY_SELECT && keyShown(KEY_POCKETS)) {
            r.set(keys[KEY_SELECT].left, keys[KEY_SELECT].top, keys[KEY_SELECT].centerX() - u * 1.3f, keys[KEY_SELECT].bottom);
            return r;
        }
        return keys[key];
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (blackout.fullyBlack() && event.getActionMasked() == MotionEvent.ACTION_DOWN) return true;
        int action = event.getActionMasked(), index = event.getActionIndex(), id = event.getPointerId(index);
        float x = event.getX(index), y = event.getY(index);
        if (action == MotionEvent.ACTION_CANCEL) { releaseTouches(); return true; }
        boolean down = action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN;
        boolean up = action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP;
        if (down) {
            int key = keyAt(x, y);
            if (key >= 0) {
                pressed = key;
                // Guest buttons are held for as long as the finger is, like the buttons they stand for.
                int mask = key == KEY_SELECT ? CityFolkGameActivity.A : key == KEY_POCKETS ? CityFolkGameActivity.MINUS
                        : key == KEY_BACK ? CityFolkGameActivity.B : 0;
                if (mask != 0 && buttonHeld == 0) { buttonHeld = mask; heldSince = SystemClock.uptimeMillis(); removeCallbacks(release); owner.touchButton(mask, true); }
            } else if (showingPockets()) {
                // The game's pointer goes to the tapped slot or choice and A is pressed there; the game does the rest.
                // A stays held while the finger is down: dragging moves the game's pointer between targets,
                // exactly the game's own press, drag and release; a quick tap is a normal select.
                float[] target = pockets.target(x, y);
                if (target != null && buttonHeld == 0) {
                    pointerOut = true;
                    dragging = id;
                    owner.aim(target[0], target[1], true);
                    removeCallbacks(pressSelect);
                    postDelayed(pressSelect, 70);
                }
            } else if (showingMap() && surface.contains(x, y)) {
                map.pick(x, y, u);
            } else if (!showingMap() && surface.contains(x, y) && aiming == MotionEvent.INVALID_POINTER_ID) {
                aiming = id;
                lastX = x; lastY = y;
                pointerOut = true;
                owner.aim(cursorX, cursorY, true); // show the pointer where it was left
            }
        }
        int drag = event.findPointerIndex(dragging);
        if (drag >= 0 && action == MotionEvent.ACTION_MOVE && showingPockets()) {
            float[] over = pockets.dropTarget(event.getX(drag), event.getY(drag));
            if (over != null) owner.aim(over[0], over[1], true);
        }
        if (up && id == dragging) {
            dragging = MotionEvent.INVALID_POINTER_ID;
            removeCallbacks(pressSelect);
            if (buttonHeld == CityFolkGameActivity.A) postDelayed(release, Math.max(0, MIN_HOLD_MS - (SystemClock.uptimeMillis() - heldSince)));
        }
        int aim = event.findPointerIndex(aiming);
        // The lift's own position counts too: the last stretch of a slide arrives with the finger leaving.
        if (aim >= 0) {
            pointerOut = true;
            float px = event.getX(aim), py = event.getY(aim);
            cursorX = Math.max(-1f, Math.min(1f, cursorX + 2 * (px - lastX) / surface.width()));
            cursorY = Math.max(-1f, Math.min(1f, cursorY + 2 * (py - lastY) / surface.height()));
            lastX = px; lastY = py;
            owner.aim(cursorX, cursorY, true);
        }
        if (up) {
            if (id == aiming) aiming = MotionEvent.INVALID_POINTER_ID; // the pointer stays where it was left
            else if (pressed >= 0) {
                int key = pressed;
                pressed = -1;
                // A quick tap is still held long enough for the game to sample it.
                if (buttonHeld != 0) postDelayed(release, Math.max(0, MIN_HOLD_MS - (SystemClock.uptimeMillis() - heldSince)));
                if (key == keyAt(x, y)) {
                    if (key == KEY_MENU) owner.openMenu();
                    else if (key == KEY_MAP) view = MAP;
                    else if (key == KEY_POINT) view = POINT;
                }
            }
        }
        invalidate();
        return true;
    }

    @Override protected void onDraw(Canvas c) {
        skin.backdrop(c, getWidth(), getHeight(), u);
        drawTop(c);
        skin.paper(c, sheet, u * 3, u);
        skin.pin(c, sheet.left + u * 6, sheet.top + u * 2.6f, u * 1.4f, TownSkin.RED);
        skin.pin(c, sheet.right - u * 6, sheet.top + u * 2.6f, u * 1.4f, TownSkin.SKY);
        if (showingPockets()) pockets.draw(c, surface, u);
        else if (showingMap()) map.draw(c, surface, state, u);
        else drawPointerSheet(c);
        boolean pick = keyShown(KEY_MAP);
        if (pick) {
            drawSign(c, KEY_MAP, "Map", "map", showingMap());
            drawSign(c, KEY_POINT, "Pointer", "point", !showingMap());
            skin.focus(c, keys[showingMap() ? KEY_MAP : KEY_POINT], WiiDsSkin.pulse(), u);
        }
        if (keyShown(KEY_SELECT)) drawSign(c, KEY_SELECT, "Select", "check", true);
        if (keyShown(KEY_BACK)) drawSign(c, KEY_BACK, "Back", "back", false);
        if (keyShown(KEY_POCKETS)) drawSign(c, KEY_POCKETS, "Pockets", "pockets", false);
        if (WiiDsSkin.motion() && pick) postInvalidateDelayed(40);
        // Owner rule: the lower screen goes dark with the upper one (start-up, fades, scene changes,
        // cutscenes and dialogue) but stays live whenever the game is waiting for the pointer.
        boolean waiting = state.pointerScreen() != CityFolkState.NO_POINTER_SCREEN || state.textBox() == 2;
        int level = state.topLevel();
        boolean black = !state.valid || state.scene() == CityFolkState.STARTING || state.wipe() == 2
                || (level >= 0 && level < 10) || state.stageChanging();
        boolean quiet = state.textBox() == 1 || (state.demoStage() && !waiting);
        blackout.target(!waiting && (black || quiet) ? 1f : 0f);
        if (blackout.draw(c, getWidth(), getHeight())) postInvalidateOnAnimation();
    }

    private void drawTop(Canvas c) {
        String title = !state.valid ? "Animal Crossing" : state.scene() == CityFolkState.TOWN ? "Town"
                : state.scene() == CityFolkState.CITY ? "The city" : state.scene() == CityFolkState.INDOORS ? "Indoors" : "Animal Crossing";
        String caption = showingPockets() ? "Pockets" : showingMap() ? (map.charted() ? "Town map" : "Town map · not charted yet") : state.pointerScreen() == CityFolkState.POCKETS ? "Pockets are on the upper screen"
                : state.pointerScreen() == CityFolkState.TOWN_MAP ? "Map is on the upper screen" : "Wii Remote pointer";
        float width = keys[KEY_MENU].left - top.left - u * 4;
        skin.ink(c, caption, top.left + u * 0.6f, top.top + u * 2.6f, u * 3.2f, u * 2.4f, width, 0xFFFFF3D2, TownSkin.BOLD, Paint.Align.LEFT);
        skin.ink(c, title, top.left + u * 0.4f, top.top + u * 8.4f, u * 7.2f, u * 4, width, 0xFFFFFBEA, TownSkin.BOLD, Paint.Align.LEFT);
        RectF key = keys[KEY_MENU];
        skin.sign(c, key, TownSkin.WOOD_LOW, pressed == KEY_MENU ? WiiDsSkin.PRESSED : 0, u);
        float dy = pressed == KEY_MENU ? u * 0.6f : 0;
        skin.glyph(c, "gear", key.centerX(), key.centerY() - u * 1.5f + dy, u * 5.6f, 0xFFFFFBEA);
        skin.ink(c, "WiiDS", key.centerX(), key.bottom - u * 2.7f + dy, u * 2.6f, u * 2, key.width() - u * 2, 0xFFFFFBEA, TownSkin.BOLD, Paint.Align.CENTER);
    }

    private void drawSign(Canvas c, int key, String label, String glyph, boolean primary) {
        RectF area = new RectF(bounds(key));
        int state = pressed == key ? WiiDsSkin.PRESSED : 0;
        if (key == KEY_MAP || key == KEY_POINT) {
            // The two view signs: the one in use is painted, the other is bare wood.
            skin.sign(c, area, primary ? TownSkin.LEAF : TownSkin.WOOD, state, u);
            float dy = pressed == key ? u * 0.6f : 0;
            skin.glyph(c, glyph, area.left + u * 7, area.centerY() + dy, u * 5.4f, 0xFFFFFBEA);
            skin.ink(c, label, area.left + u * 11.5f, area.centerY() + dy, u * 4.6f, u * 3, area.width() - u * 14, 0xFFFFFBEA, TownSkin.BOLD, Paint.Align.LEFT);
        } else {
            skin.key(c, area, label, glyph, state, primary, u);
        }
    }

    /** Grid paper marked with the television's frame, the area the pointer is aimed across. */
    private void drawPointerSheet(Canvas c) {
        Paint line = skin.linePaint(u * 0.16f, WiiDsSkin.alpha(TownSkin.SKY, 0.35f));
        float step = surface.width() / 16f;
        for (float x = surface.left + step; x < surface.right - 1; x += step) c.drawLine(x, surface.top, x, surface.bottom, line);
        for (float y = surface.top + step; y < surface.bottom - 1; y += step) c.drawLine(surface.left, y, surface.right, y, line);
        Paint frame = skin.linePaint(u * 0.5f, WiiDsSkin.alpha(TownSkin.INK_SOFT, 0.8f));
        frame.setPathEffect(new DashPathEffect(new float[]{u * 2, u * 1.6f}, 0));
        c.drawRoundRect(surface, u * 2, u * 2, frame);
        frame.setPathEffect(null);
        boolean held = aiming != MotionEvent.INVALID_POINTER_ID;
        skin.glyph(c, "point", surface.centerX(), surface.centerY() - u * 7, u * 12, WiiDsSkin.alpha(TownSkin.INK_SOFT, held ? 0.25f : 0.55f));
        skin.ink(c, "Slide to move the pointer", surface.centerX(), surface.centerY() + u * 5, u * 4.6f, u * 3, surface.width() - u * 8,
                WiiDsSkin.alpha(TownSkin.INK, held ? 0.3f : 0.85f), TownSkin.BOLD, Paint.Align.CENTER);
        skin.ink(c, "Like a trackpad: lift and slide again to keep going.", surface.centerX(), surface.centerY() + u * 10.5f, u * 3.6f, u * 2.6f,
                surface.width() - u * 8, WiiDsSkin.alpha(TownSkin.INK_SOFT, held ? 0.3f : 0.9f), TownSkin.MEDIUM, Paint.Align.CENTER);
    }
}
