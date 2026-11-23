package com.driftdroid.android.dualscreen;

import android.animation.ValueAnimator;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;

/**
 * Materials for one game's WiiDS surfaces. The menu view and the lower boards own layout and
 * input; a skin only paints. All sizes are in {@code u}, one hundredth of the panel's short side.
 */
public abstract class WiiDsSkin {
    public static final int FOCUSED = 1, PRESSED = 2, DISABLED = 4;

    protected final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    protected final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    protected final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    protected final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Path path = new Path();
    private final RectF box = new RectF();

    protected WiiDsSkin() {
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeJoin(Paint.Join.ROUND);
        stroke.setStrokeCap(Paint.Cap.ROUND);
    }

    /** Full-bleed material behind the panel when the menu owns a whole display. */
    public abstract void backdrop(Canvas c, float w, float h, float u);
    public abstract void panel(Canvas c, RectF panel, float u);
    public abstract void header(Canvas c, RectF r, String title, String subtitle, Bitmap art, float u);
    public abstract void tile(Canvas c, RectF r, String label, String glyph, int state, boolean danger, float u);
    public abstract void row(Canvas c, RectF r, String label, String value, int state, boolean adjustable, float u);
    public abstract void key(Canvas c, RectF r, String label, String glyph, int state, boolean primary, float u);
    public abstract void note(Canvas c, RectF r, String message, float u);
    public abstract void hint(Canvas c, RectF r, String message, float u);
    /** The single controller-focus perimeter. {@code phase} runs 0..1 and is constant with reduced motion. */
    public abstract void focus(Canvas c, RectF r, float phase, float u);
    /** Height the note needs at this width, so the view can place the rows under it. */
    public abstract float noteHeight(String message, float width, float u);
    /** Colour behind a paused game when the menu shares the game's display. */
    public int scrim() { return 0xB0000000; }

    // Shared helpers.

    public Paint imagePaint() { return image; }

    public Paint linePaint(float width, int color) {
        stroke.setShader(null);
        stroke.setStrokeWidth(width);
        stroke.setColor(color);
        return stroke;
    }

    public Paint fillPaint(int color) {
        fill.setShader(null);
        fill.setColor(color);
        return fill;
    }

    public static boolean motion() {
        try { return ValueAnimator.areAnimatorsEnabled(); } catch (RuntimeException old) { return true; }
    }

    public static float pulse() {
        if (!motion()) return 1f;
        return 0.5f + 0.5f * (float) Math.cos(SystemClock.uptimeMillis() / 520.0 * Math.PI);
    }

    public static int shade(int color, float factor) {
        int r = Math.min(255, Math.round(((color >> 16) & 255) * factor));
        int g = Math.min(255, Math.round(((color >> 8) & 255) * factor));
        int b = Math.min(255, Math.round((color & 255) * factor));
        return (color & 0xff000000) | r << 16 | g << 8 | b;
    }

    public static int mix(int a, int b, float t) {
        int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, aa = a >>> 24;
        int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255, ba = b >>> 24;
        return Math.round(aa + (ba - aa) * t) << 24 | Math.round(ar + (br - ar) * t) << 16
                | Math.round(ag + (bg - ag) * t) << 8 | Math.round(ab + (bb - ab) * t);
    }

    public static int alpha(int color, float a) {
        return (Math.max(0, Math.min(255, Math.round((color >>> 24) * a))) << 24) | (color & 0xffffff);
    }

    /** Rectangle with chamfered corners, the machined shape used for Orre plates. */
    protected Path chamfer(RectF r, float cut) {
        cut = Math.min(cut, Math.min(r.width(), r.height()) / 2);
        path.rewind();
        path.moveTo(r.left + cut, r.top);
        path.lineTo(r.right - cut, r.top);
        path.lineTo(r.right, r.top + cut);
        path.lineTo(r.right, r.bottom - cut);
        path.lineTo(r.right - cut, r.bottom);
        path.lineTo(r.left + cut, r.bottom);
        path.lineTo(r.left, r.bottom - cut);
        path.lineTo(r.left, r.top + cut);
        path.close();
        return path;
    }

    protected TextPaint font(Typeface face, float size, int color) {
        text.setTypeface(face);
        text.setTextSize(size);
        text.setColor(color);
        text.setLetterSpacing(0);
        text.setTextAlign(Paint.Align.LEFT);
        text.setStyle(Paint.Style.FILL);
        text.setShader(null);
        return text;
    }

    /** Shrinks to {@code min}, then ellipsizes. Returns the string to draw; the paint keeps the final size. */
    protected String fit(String value, TextPaint paint, float width, float min) {
        while (paint.getTextSize() > min && paint.measureText(value) > width) paint.setTextSize(paint.getTextSize() - 1f);
        if (paint.measureText(value) <= width) return value;
        String cut = value;
        while (cut.length() > 1 && paint.measureText(cut + "…") > width) cut = cut.substring(0, cut.length() - 1);
        return cut + "…";
    }

    protected static float baseline(Paint paint, float centerY) {
        Paint.FontMetrics m = paint.getFontMetrics();
        return centerY - (m.ascent + m.descent) / 2;
    }

    protected StaticLayout paragraph(String message, TextPaint paint, float width) {
        return StaticLayout.Builder.obtain(message, 0, message.length(), new TextPaint(paint), Math.max(1, Math.round(width)))
                .setAlignment(Layout.Alignment.ALIGN_NORMAL).setLineSpacing(0, 1.18f).setIncludePad(false).build();
    }

    /** Line icons shared by both games. Drawn in a {@code size} box centred on (cx, cy). */
    public void glyph(Canvas c, String name, float cx, float cy, float size, int color) {
        if (name == null || name.isEmpty()) return;
        float s = size / 2, w = Math.max(1.5f, size * 0.11f);
        fill.setShader(null);
        fill.setColor(color);
        stroke.setShader(null);
        stroke.setColor(color);
        stroke.setStrokeWidth(w);
        Path p = path;
        p.rewind();
        switch (name) {
            case "play":
                p.moveTo(cx - s * 0.55f, cy - s * 0.8f); p.lineTo(cx + s * 0.8f, cy); p.lineTo(cx - s * 0.55f, cy + s * 0.8f); p.close();
                c.drawPath(p, fill);
                break;
            case "display":
                box.set(cx - s * 0.9f, cy - s * 0.7f, cx + s * 0.9f, cy + s * 0.35f);
                c.drawRoundRect(box, s * 0.14f, s * 0.14f, stroke);
                c.drawLine(cx, cy + s * 0.35f, cx, cy + s * 0.75f, stroke);
                c.drawLine(cx - s * 0.45f, cy + s * 0.78f, cx + s * 0.45f, cy + s * 0.78f, stroke);
                break;
            case "audio":
                p.moveTo(cx - s * 0.9f, cy - s * 0.3f); p.lineTo(cx - s * 0.45f, cy - s * 0.3f); p.lineTo(cx + s * 0.05f, cy - s * 0.75f);
                p.lineTo(cx + s * 0.05f, cy + s * 0.75f); p.lineTo(cx - s * 0.45f, cy + s * 0.3f); p.lineTo(cx - s * 0.9f, cy + s * 0.3f); p.close();
                c.drawPath(p, fill);
                box.set(cx - s * 0.25f, cy - s * 0.45f, cx + s * 0.55f, cy + s * 0.45f);
                c.drawArc(box, -55, 110, false, stroke);
                box.set(cx - s * 0.35f, cy - s * 0.8f, cx + s * 0.95f, cy + s * 0.8f);
                c.drawArc(box, -50, 100, false, stroke);
                break;
            case "pad":
                box.set(cx - s * 0.95f, cy - s * 0.55f, cx + s * 0.95f, cy + s * 0.55f);
                c.drawRoundRect(box, s * 0.45f, s * 0.45f, stroke);
                c.drawLine(cx - s * 0.62f, cy, cx - s * 0.18f, cy, stroke);
                c.drawLine(cx - s * 0.4f, cy - s * 0.22f, cx - s * 0.4f, cy + s * 0.22f, stroke);
                c.drawCircle(cx + s * 0.32f, cy + s * 0.12f, w * 0.75f, fill);
                c.drawCircle(cx + s * 0.6f, cy - s * 0.12f, w * 0.75f, fill);
                break;
            case "save":
                p.moveTo(cx - s * 0.75f, cy - s * 0.8f); p.lineTo(cx + s * 0.45f, cy - s * 0.8f); p.lineTo(cx + s * 0.75f, cy - s * 0.5f);
                p.lineTo(cx + s * 0.75f, cy + s * 0.8f); p.lineTo(cx - s * 0.75f, cy + s * 0.8f); p.close();
                c.drawPath(p, stroke);
                box.set(cx - s * 0.4f, cy + s * 0.05f, cx + s * 0.4f, cy + s * 0.8f);
                c.drawRect(box, stroke);
                c.drawLine(cx - s * 0.35f, cy - s * 0.42f, cx + s * 0.2f, cy - s * 0.42f, stroke);
                break;
            case "exit":
                p.moveTo(cx + s * 0.05f, cy - s * 0.8f); p.lineTo(cx - s * 0.8f, cy - s * 0.8f); p.lineTo(cx - s * 0.8f, cy + s * 0.8f); p.lineTo(cx + s * 0.05f, cy + s * 0.8f);
                c.drawPath(p, stroke);
                c.drawLine(cx - s * 0.25f, cy, cx + s * 0.85f, cy, stroke);
                p.rewind(); p.moveTo(cx + s * 0.45f, cy - s * 0.4f); p.lineTo(cx + s * 0.88f, cy); p.lineTo(cx + s * 0.45f, cy + s * 0.4f);
                c.drawPath(p, stroke);
                break;
            case "back":
                p.moveTo(cx - s * 0.2f, cy - s * 0.75f); p.lineTo(cx - s * 0.75f, cy - s * 0.3f); p.lineTo(cx - s * 0.2f, cy + s * 0.15f);
                c.drawPath(p, stroke);
                p.rewind(); p.moveTo(cx - s * 0.7f, cy - s * 0.3f); p.lineTo(cx + s * 0.3f, cy - s * 0.3f);
                p.cubicTo(cx + s * 0.95f, cy - s * 0.3f, cx + s * 0.95f, cy + s * 0.7f, cx + s * 0.3f, cy + s * 0.7f);
                p.lineTo(cx - s * 0.4f, cy + s * 0.7f);
                c.drawPath(p, stroke);
                break;
            case "map":
                p.moveTo(cx - s * 0.9f, cy - s * 0.55f); p.lineTo(cx - s * 0.3f, cy - s * 0.8f); p.lineTo(cx + s * 0.3f, cy - s * 0.55f); p.lineTo(cx + s * 0.9f, cy - s * 0.8f);
                p.lineTo(cx + s * 0.9f, cy + s * 0.55f); p.lineTo(cx + s * 0.3f, cy + s * 0.8f); p.lineTo(cx - s * 0.3f, cy + s * 0.55f); p.lineTo(cx - s * 0.9f, cy + s * 0.8f); p.close();
                c.drawPath(p, stroke);
                c.drawLine(cx - s * 0.3f, cy - s * 0.8f, cx - s * 0.3f, cy + s * 0.55f, stroke);
                c.drawLine(cx + s * 0.3f, cy - s * 0.55f, cx + s * 0.3f, cy + s * 0.8f, stroke);
                break;
            case "party":
                c.drawCircle(cx, cy, s * 0.82f, stroke);
                c.drawLine(cx - s * 0.82f, cy, cx - s * 0.3f, cy, stroke);
                c.drawLine(cx + s * 0.3f, cy, cx + s * 0.82f, cy, stroke);
                c.drawCircle(cx, cy, s * 0.28f, stroke);
                break;
            case "shadow":
                // The closed-door emblem of a Shadow Pokémon's heart: a diamond with a struck core.
                p.moveTo(cx, cy - s * 0.92f); p.lineTo(cx + s * 0.68f, cy); p.lineTo(cx, cy + s * 0.92f); p.lineTo(cx - s * 0.68f, cy); p.close();
                c.drawPath(p, stroke);
                p.rewind(); p.moveTo(cx, cy - s * 0.42f); p.lineTo(cx + s * 0.3f, cy); p.lineTo(cx, cy + s * 0.42f); p.lineTo(cx - s * 0.3f, cy); p.close();
                c.drawPath(p, fill);
                break;
            case "gear":
                for (int i = 0; i < 8; i++) {
                    double a = i * Math.PI / 4;
                    c.drawLine(cx + (float) Math.cos(a) * s * 0.62f, cy + (float) Math.sin(a) * s * 0.62f,
                            cx + (float) Math.cos(a) * s * 0.92f, cy + (float) Math.sin(a) * s * 0.92f, stroke);
                }
                c.drawCircle(cx, cy, s * 0.58f, stroke);
                c.drawCircle(cx, cy, s * 0.2f, fill);
                break;
            case "pockets":
                p.moveTo(cx - s * 0.75f, cy - s * 0.45f); p.lineTo(cx + s * 0.75f, cy - s * 0.45f);
                p.lineTo(cx + s * 0.75f, cy + s * 0.25f);
                p.cubicTo(cx + s * 0.75f, cy + s * 0.75f, cx + s * 0.3f, cy + s * 0.85f, cx, cy + s * 0.85f);
                p.cubicTo(cx - s * 0.3f, cy + s * 0.85f, cx - s * 0.75f, cy + s * 0.75f, cx - s * 0.75f, cy + s * 0.25f);
                p.close();
                c.drawPath(p, stroke);
                c.drawLine(cx - s * 0.75f, cy - s * 0.05f, cx + s * 0.75f, cy - s * 0.05f, stroke);
                p.rewind(); p.moveTo(cx - s * 0.4f, cy - s * 0.45f);
                p.cubicTo(cx - s * 0.4f, cy - s * 1.0f, cx + s * 0.4f, cy - s * 1.0f, cx + s * 0.4f, cy - s * 0.45f);
                c.drawPath(p, stroke);
                break;
            case "point":
                p.moveTo(cx - s * 0.45f, cy - s * 0.85f); p.lineTo(cx + s * 0.6f, cy + s * 0.15f); p.lineTo(cx + s * 0.08f, cy + s * 0.2f);
                p.lineTo(cx + s * 0.38f, cy + s * 0.8f); p.lineTo(cx + s * 0.1f, cy + s * 0.9f); p.lineTo(cx - s * 0.18f, cy + s * 0.32f);
                p.lineTo(cx - s * 0.45f, cy + s * 0.6f); p.close();
                c.drawPath(p, fill);
                break;
            case "check":
                p.moveTo(cx - s * 0.7f, cy + s * 0.05f); p.lineTo(cx - s * 0.2f, cy + s * 0.55f); p.lineTo(cx + s * 0.75f, cy - s * 0.5f);
                c.drawPath(p, stroke);
                break;
            default:
                c.drawCircle(cx, cy, s * 0.3f, fill);
        }
    }
}
