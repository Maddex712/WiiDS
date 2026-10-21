package com.wither.nsmbw;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

/**
 * The on-screen Wii Remote, laid out the way the remote is held sideways: the cross-pad under the
 * left thumb, 1 and 2 under the right one.
 *
 * <p>Every finger is tracked on its own and stays live wherever it slides:
 * <ul>
 * <li>Over the buttons, whatever is under the finger is pressed, so a thumb can roll from 1 onto 2
 * (or across A, Shake, + and -), pass through the gaps between them and pick up the next button.
 * A finger that lands on empty space presses a button as soon as it slides onto one.</li>
 * <li>1 and 2 reach a little further than the other buttons, so their areas overlap: a thumb
 * resting on the seam holds both, for running jumps.</li>
 * <li>On the cross-pad the direction follows the finger through all eight directions. A finger on
 * the pad keeps steering it even when it drifts past the edge, and a finger that slides onto the
 * pad from elsewhere takes it over.</li>
 * <li>The menu button only opens when a press both starts and ends on it, never from a slide.</li>
 * </ul>
 */
final class TouchControlsView extends View {
    /** Bits as the native side expects them (VirtualRemote::TouchButton), plus local extras. */
    static final int LEFT = 0x001, RIGHT = 0x002, UP = 0x004, DOWN = 0x008;
    static final int ONE = 0x010, TWO = 0x020, A = 0x040, PLUS = 0x080, MINUS = 0x100;
    private static final int SHAKE = 0x1000, MENU = 0x2000;

    private static final int MODE_BUTTONS = 1;  // the finger presses whatever button it is over
    private static final int MODE_PAD = 2;      // the finger steers the cross-pad until lifted
    private static final int MODE_MENU = 3;     // the press started on the menu button

    interface Listener {
        void onTouchInput(int buttons, boolean shake);
        void onMenu();
        /** A tap on the game while its menus are up (menu mode), as fractions of the view. */
        void onTap(float x, float y);
    }

    private static final class Button {
        final int bit;
        final String label;
        final float reach;  // hit radius as a multiple of the drawn radius
        float x, y, r;
        Button(int bit, String label, float reach) { this.bit = bit; this.label = label; this.reach = reach; }
        boolean hit(float px, float py) {
            final float dx = px - x, dy = py - y, radius = r * reach;
            return dx * dx + dy * dy <= radius * radius;
        }
    }

    private final Button[] buttons = {
        new Button(TWO, "2", 1.4f), new Button(ONE, "1", 1.4f), new Button(A, "A", 1.25f),
        new Button(SHAKE, "SHAKE", 1.25f), new Button(MINUS, "−", 1.3f), new Button(PLUS, "+", 1.3f),
    };
    private float padX, padY, padR;
    private final SparseIntArray pointerModes = new SparseIntArray();  // pointer id -> MODE_*
    private final SparseIntArray pointerBits = new SparseIntArray();   // pointer id -> bits it holds
    private int held;
    private Listener listener;
    private float opacity = 0.6f;
    private boolean controlsVisible = true;
    private final android.util.SparseBooleanArray freeTaps = new android.util.SparseBooleanArray();
    private boolean menuMode;  // the game's menus are up: no remote, taps go to the game
    private boolean haptics = true;
    private final android.util.SparseArray<float[]> tapStarts = new android.util.SparseArray<>();
    private final android.util.SparseLongArray tapTimes = new android.util.SparseLongArray();

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path arrow = new Path();
    private final RectF rect = new RectF();

    TouchControlsView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        setFocusable(false);
    }

    void setListener(Listener listener) { this.listener = listener; }

    void setOpacity(float opacity) { this.opacity = opacity; invalidate(); }

    void setHaptics(boolean haptics) { this.haptics = haptics; }

    /** Hidden controls still leave the menu button, so the settings stay reachable. */
    void setControlsVisible(boolean visible) {
        controlsVisible = visible;
        release();
        invalidate();
    }

    /**
     * Menu mode, while the game's own menus are up (title, file select, player count, LAN menus):
     * the remote is hidden apart from the menu button, and a tap on the game is passed on as one.
     */
    void setMenuMode(boolean on) {
        if (menuMode == on) return;
        menuMode = on;
        release();
        invalidate();
    }

    private boolean showsControls() { return controlsVisible && !menuMode; }

    /** Lets go of everything, e.g. when the app loses focus or a menu opens. */
    void release() {
        pointerModes.clear();
        pointerBits.clear();
        update(0);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        layoutControls(getWidth(), getHeight());
        return super.onApplyWindowInsets(insets);
    }

    private void layoutControls(int w, int h) {
        if (w == 0 || h == 0) return;
        // Keep the thumbs' controls clear of a camera cutout on either short edge.
        float left = 0, right = 0;
        final WindowInsets insets = getRootWindowInsets();
        if (insets != null && insets.getDisplayCutout() != null) {
            left = insets.getDisplayCutout().getSafeInsetLeft();
            right = insets.getDisplayCutout().getSafeInsetRight();
        }
        final float u = h;  // everything scales with the short side
        padR = 0.21f * u;
        padX = left + 0.07f * u + padR;
        padY = 0.66f * u;
        place(TWO, w - right - 0.17f * u, 0.74f * u, 0.11f * u);
        place(ONE, w - right - 0.43f * u, 0.80f * u, 0.11f * u);
        place(A, w - right - 0.43f * u, 0.52f * u, 0.075f * u);
        place(SHAKE, w - right - 0.17f * u, 0.44f * u, 0.085f * u);
        place(MINUS, w / 2f - 0.13f * u, 0.08f * u, 0.05f * u);
        place(PLUS, w / 2f + 0.13f * u, 0.08f * u, 0.05f * u);
        place(MENU, w - right - 0.08f * u, 0.08f * u, 0.05f * u);
        invalidate();
    }

    private void place(int bit, float x, float y, float r) {
        for (Button b : buttons) {
            if (b.bit == bit) { b.x = x; b.y = y; b.r = r; }
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            release();
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            final int i = e.getActionIndex();
            tapStarts.put(e.getPointerId(i), new float[] {e.getX(i), e.getY(i)});
            tapTimes.put(e.getPointerId(i), e.getEventTime());
            press(e.getPointerId(i), e.getX(i), e.getY(i));
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < e.getPointerCount(); ++i) {
                slide(e.getPointerId(i), e.getX(i), e.getY(i));
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            final int i = e.getActionIndex();
            final int id = e.getPointerId(i);
            final boolean openMenu = pointerModes.get(id, 0) == MODE_MENU && menuAt(e.getX(i), e.getY(i));
            final float[] start = tapStarts.get(id);
            // Taps go to the game on its menus, and anywhere off the remote's buttons otherwise (the
            // settings gear over the pause menus).
            final boolean tappable = menuMode ? pointerModes.get(id, 0) != MODE_MENU : freeTaps.get(id, false);
            if (tappable && start != null && listener != null
                    && Math.hypot(e.getX(i) - start[0], e.getY(i) - start[1]) < 0.06f * getHeight()
                    && e.getEventTime() - tapTimes.get(id) < 800 && getWidth() > 0 && getHeight() > 0) {
                listener.onTap(e.getX(i) / getWidth(), e.getY(i) / getHeight());
            }
            tapStarts.remove(id);
            tapTimes.delete(id);
            freeTaps.delete(id);
            pointerModes.delete(id);
            pointerBits.delete(id);
            if (openMenu && listener != null) {
                listener.onMenu();
            }
        }
        int combined = 0;
        for (int i = 0; i < pointerBits.size(); ++i) {
            combined |= pointerBits.valueAt(i);
        }
        update(combined);
        return true;
    }

    private void press(int id, float x, float y) {
        if (menuAt(x, y)) {
            pointerModes.put(id, MODE_MENU);
            pointerBits.put(id, MENU);
        } else if (onPad(x, y)) {
            pointerModes.put(id, MODE_PAD);
            pointerBits.put(id, padDirection(x, y));
        } else {
            pointerModes.put(id, MODE_BUTTONS);
            pointerBits.put(id, buttonsAt(x, y));
            if (buttonsAt(x, y) == 0) freeTaps.put(id, true);
        }
    }

    private void slide(int id, float x, float y) {
        final int mode = pointerModes.get(id, 0);
        if (mode == MODE_PAD) {
            pointerBits.put(id, padDirection(x, y));
        } else if (mode == MODE_BUTTONS) {
            if (onPad(x, y)) {
                pointerModes.put(id, MODE_PAD);
                pointerBits.put(id, padDirection(x, y));
            } else {
                pointerBits.put(id, buttonsAt(x, y));
            }
        } else if (mode == MODE_MENU) {
            pointerBits.put(id, menuAt(x, y) ? MENU : 0);
        }
    }

    private boolean onPad(float x, float y) {
        if (!showsControls()) return false;
        final float dx = x - padX, dy = y - padY, reach = padR * 1.26f;
        return dx * dx + dy * dy <= reach * reach;
    }

    private boolean menuAt(float x, float y) {
        for (Button b : buttons) {
            if (b.bit == MENU) return b.hit(x, y);
        }
        return false;
    }

    /** Every button under the point (1 and 2 overlap on their seam); never the menu. */
    private int buttonsAt(float x, float y) {
        if (!showsControls()) return 0;
        int bits = 0;
        for (Button b : buttons) {
            if (b.bit != MENU && b.hit(x, y)) bits |= b.bit;
        }
        return bits;
    }

    /** Eight-way: a direction counts within 67.5 degrees of its axis, so diagonals press two. */
    private int padDirection(float x, float y) {
        final float dx = x - padX, dy = y - padY;
        if (dx * dx + dy * dy < (0.22f * padR) * (0.22f * padR)) return 0;
        int bits = 0;
        final float slope = 0.4142136f;  // tan(22.5 degrees)
        if (Math.abs(dx) >= slope * Math.abs(dy)) bits |= dx < 0 ? LEFT : RIGHT;
        if (Math.abs(dy) >= slope * Math.abs(dx)) bits |= dy < 0 ? UP : DOWN;
        return bits;
    }

    private void update(int bits) {
        if (bits == held) return;
        final int pressed = bits & ~held & ~MENU;
        held = bits;
        if (pressed != 0 && haptics) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        }
        if (listener != null) {
            listener.onTouchInput(bits & 0x1FF, (bits & SHAKE) != 0);
        }
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        final int base = Math.round(255 * opacity);
        if (showsControls()) {
            drawPad(canvas, base);
        }
        for (Button b : buttons) {
            if (!showsControls() && b.bit != MENU) continue;
            final boolean down = (held & b.bit) != 0;
            fill.setColor(Color.argb(down ? base * 3 / 4 : base / 3, 255, 255, 255));
            canvas.drawCircle(b.x, b.y, b.r, fill);
            stroke.setStrokeWidth(Math.max(2f, b.r * 0.06f));
            stroke.setColor(Color.argb(base, 255, 255, 255));
            canvas.drawCircle(b.x, b.y, b.r, stroke);
            text.setColor(Color.argb(Math.min(255, base + 60), down ? 30 : 255, down ? 30 : 255, down ? 30 : 255));
            text.setTextSize(b.label.length() > 2 ? b.r * 0.42f : b.r * 0.9f);
            canvas.drawText(b.label, b.x, b.y - (text.descent() + text.ascent()) / 2, text);
        }
    }

    private void drawPad(Canvas canvas, int base) {
        final float arm = padR * 0.36f;  // half the width of each bar
        fill.setColor(Color.argb(base / 3, 255, 255, 255));
        stroke.setStrokeWidth(Math.max(2f, padR * 0.025f));
        stroke.setColor(Color.argb(base, 255, 255, 255));
        rect.set(padX - padR, padY - arm, padX + padR, padY + arm);
        canvas.drawRoundRect(rect, arm * 0.4f, arm * 0.4f, fill);
        canvas.drawRoundRect(rect, arm * 0.4f, arm * 0.4f, stroke);
        rect.set(padX - arm, padY - padR, padX + arm, padY + padR);
        canvas.drawRoundRect(rect, arm * 0.4f, arm * 0.4f, fill);
        canvas.drawRoundRect(rect, arm * 0.4f, arm * 0.4f, stroke);
        drawArrow(canvas, base, LEFT, -1, 0);
        drawArrow(canvas, base, RIGHT, 1, 0);
        drawArrow(canvas, base, UP, 0, -1);
        drawArrow(canvas, base, DOWN, 0, 1);
    }

    private void drawArrow(Canvas canvas, int base, int bit, int ux, int uy) {
        final boolean down = (held & bit) != 0;
        final float tipX = padX + ux * padR * 0.86f, tipY = padY + uy * padR * 0.86f;
        final float backX = padX + ux * padR * 0.56f, backY = padY + uy * padR * 0.56f;
        final float half = padR * 0.16f;
        arrow.reset();
        arrow.moveTo(tipX, tipY);
        arrow.lineTo(backX - uy * half, backY - ux * half);
        arrow.lineTo(backX + uy * half, backY + ux * half);
        arrow.close();
        fill.setColor(Color.argb(down ? Math.min(255, base + 80) : base, 255, 255, 255));
        canvas.drawPath(arrow, fill);
    }
}
