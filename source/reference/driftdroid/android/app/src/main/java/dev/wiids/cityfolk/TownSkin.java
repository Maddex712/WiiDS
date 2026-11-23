package dev.wiids.cityfolk;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.text.StaticLayout;
import android.text.TextPaint;

import com.driftdroid.android.dualscreen.WiiDsSkin;

/**
 * The town noticeboard: a plank board, pinned paper, ink lettering and painted wooden signs.
 * Controls are small paper cards; the selected one gets a single orange ring.
 */
final class TownSkin extends WiiDsSkin {
    static final int WOOD_HI = 0xFFC9925A, WOOD = 0xFFA9713C, WOOD_LOW = 0xFF7C4C24, PAPER = 0xFFFCF6E2, PAPER_LOW = 0xFFEFE5C6,
            PAPER_EDGE = 0xFFD8CAA0, INK = 0xFF4A3018, INK_SOFT = 0xFF8A6E4E, LEAF = 0xFF43A050, LEAF_LOW = 0xFF2C7038,
            SKY = 0xFF4FAEE0, SUN = 0xFFF2B93B, PEACH = 0xFFEE8A5A, LILAC = 0xFFA58AD8, RED = 0xFFE0513F, FOCUS = 0xFFFF8A1E;
    static final Typeface BOLD = Typeface.create("sans-serif", Typeface.BOLD), MEDIUM = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    private final RectF r = new RectF(), q = new RectF();

    @Override public int scrim() { return 0xA0241405; }

    @Override public void backdrop(Canvas c, float w, float h, float u) {
        fill.setColor(0xFF000000); // a shader still takes the paint's alpha
        fill.setShader(new LinearGradient(0, 0, 0, h, WOOD_HI, WOOD, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, fill);
        fill.setShader(null);
        float plank = u * 17;
        int row = 0;
        for (float y = 0; y < h; y += plank, row++) {
            fill.setColor(alpha(WOOD_LOW, 0.55f));
            c.drawRect(0, y - u * 0.25f, w, y + u * 0.25f, fill);
            fill.setColor(0x30FFFFFF);
            c.drawRect(0, y + u * 0.25f, w, y + u * 0.55f, fill);
            // Grain and board ends, placed by row so the wood is the same every frame.
            stroke.setStrokeWidth(u * 0.22f);
            stroke.setColor(alpha(WOOD_LOW, 0.28f));
            for (int g = 0; g < 3; g++) {
                float gy = y + plank * (0.25f + 0.24f * g), gx = w * (((row * 37 + g * 53) % 100) / 100f);
                c.drawLine(gx, gy, Math.min(w, gx + w * 0.34f), gy + u * 0.4f, stroke);
            }
            float end = w * (((row * 61 + 23) % 70 + 15) / 100f);
            fill.setColor(alpha(WOOD_LOW, 0.5f));
            c.drawRect(end, y, end + u * 0.3f, y + plank, fill);
        }
    }

    @Override public void panel(Canvas c, RectF panel, float u) {
        paper(c, panel, u * 3, u);
        pin(c, panel.left + u * 7, panel.top + u * 3.2f, u * 1.5f, RED);
        pin(c, panel.right - u * 7, panel.top + u * 3.2f, u * 1.5f, SKY);
    }

    /** A sheet of notice paper with a soft drop shadow. */
    void paper(Canvas c, RectF sheet, float radius, float u) {
        q.set(sheet.left + u * 0.5f, sheet.top + u * 1.1f, sheet.right + u * 0.5f, sheet.bottom + u * 1.1f);
        fill.setColor(0x55301A08);
        c.drawRoundRect(q, radius, radius, fill);
        fill.setColor(0xFF000000); // a shader still takes the paint's alpha
        fill.setShader(new LinearGradient(0, sheet.top, 0, sheet.bottom, PAPER, PAPER_LOW, Shader.TileMode.CLAMP));
        c.drawRoundRect(sheet, radius, radius, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(PAPER_EDGE);
        c.drawRoundRect(sheet, radius, radius, stroke);
    }

    void pin(Canvas c, float x, float y, float radius, int color) {
        fill.setColor(0x50301A08);
        c.drawCircle(x + radius * 0.35f, y + radius * 0.5f, radius, fill);
        fill.setColor(color);
        c.drawCircle(x, y, radius, fill);
        fill.setColor(0x90FFFFFF);
        c.drawCircle(x - radius * 0.3f, y - radius * 0.35f, radius * 0.35f, fill);
    }

    /** A painted wooden sign. */
    void sign(Canvas c, RectF area, int paint, int state, float u) {
        boolean down = (state & PRESSED) != 0;
        float radius = Math.min(u * 3.2f, area.height() / 2);
        q.set(area.left, area.top + u * 0.9f, area.right, area.bottom + u * 0.9f);
        fill.setColor(shade(paint, 0.6f));
        c.drawRoundRect(q, radius, radius, fill);
        r.set(area);
        if (down) r.offset(0, u * 0.6f);
        fill.setColor(0xFF000000); // a shader still takes the paint's alpha
        fill.setShader(new LinearGradient(0, r.top, 0, r.bottom, mix(paint, 0xFFFFFFFF, down ? 0.02f : 0.18f), paint, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, radius, radius, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(shade(paint, 0.55f));
        c.drawRoundRect(r, radius, radius, stroke);
    }

    /** A paper card that takes a press. */
    void card(Canvas c, RectF area, int state, float u) {
        boolean down = (state & PRESSED) != 0, hot = (state & FOCUSED) != 0;
        float radius = u * 3.2f;
        q.set(area.left, area.top + u * 0.8f, area.right, area.bottom + u * 0.8f);
        fill.setColor(0x40301A08);
        c.drawRoundRect(q, radius, radius, fill);
        r.set(area);
        if (down) r.offset(0, u * 0.5f);
        fill.setColor(down ? 0xFFF3E7BF : hot ? 0xFFFFF7CF : 0xFFFFFDF2);
        c.drawRoundRect(r, radius, radius, fill);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(PAPER_EDGE);
        c.drawRoundRect(r, radius, radius, stroke);
    }

    void ink(Canvas c, String value, float x, float centerY, float size, float min, float width, int color, Typeface face, Paint.Align align) {
        TextPaint p = font(face, size, color);
        String shown = fit(value, p, width, min);
        p.setTextAlign(align);
        c.drawText(shown, x, baseline(p, centerY), p);
    }

    static int paintFor(String glyph) {
        switch (glyph == null ? "" : glyph) {
            case "play": return LEAF;
            case "display": return SKY;
            case "audio": return SUN;
            case "pad": return PEACH;
            case "save": return LILAC;
            case "exit": return RED;
            case "map": return LEAF;
            case "pockets": return PEACH;
            default: return WOOD;
        }
    }

    @Override public void header(Canvas c, RectF area, String title, String subtitle, Bitmap art, float u) {
        float right = area.right;
        if (art != null) {
            float height = area.height() * 0.8f, width = height * art.getWidth() / art.getHeight();
            r.set(area.right - width - u * 1.6f, area.centerY() - height / 2 - u * 0.8f, area.right, area.centerY() + height / 2 + u * 0.8f);
            c.save();
            c.rotate(-1.6f, r.centerX(), r.centerY());
            fill.setColor(0x40301A08);
            c.drawRect(r.left + u * 0.4f, r.top + u * 0.7f, r.right + u * 0.4f, r.bottom + u * 0.7f, fill);
            fill.setColor(0xFFFFFFFF);
            c.drawRect(r, fill);
            q.set(r.left + u * 0.8f, r.top + u * 0.8f, r.right - u * 0.8f, r.bottom - u * 0.8f);
            c.drawBitmap(art, null, q, image);
            fill.setColor(0xB0F6E7A6); // tape
            c.drawRect(r.centerX() - u * 4, r.top - u * 1.3f, r.centerX() + u * 4, r.top + u * 1.1f, fill);
            c.restore();
            right = r.left - u * 3;
        }
        ink(c, "WiiDS  ·  " + subtitle, area.left + u * 0.5f, area.top + u * 3.4f, u * 3.1f, u * 2.2f, right - area.left, LEAF_LOW, BOLD, Paint.Align.LEFT);
        ink(c, title, area.left + u * 0.3f, area.top + u * 10, u * 8, u * 4.4f, right - area.left, INK, BOLD, Paint.Align.LEFT);
        stroke.setStrokeWidth(u * 0.45f);
        stroke.setColor(alpha(LEAF, 0.8f));
        stroke.setPathEffect(new DashPathEffect(new float[]{u * 1.6f, u * 1.4f}, 0));
        c.drawLine(area.left, area.bottom + u * 1.3f, area.right, area.bottom + u * 1.3f, stroke);
        stroke.setPathEffect(null);
    }

    @Override public void tile(Canvas c, RectF area, String label, String glyph, int state, boolean danger, float u) {
        card(c, area, state, u);
        float dy = (state & PRESSED) != 0 ? u * 0.5f : 0;
        float d = Math.min(area.height() - u * 7, u * 13), cx = area.left + u * 4.5f + d / 2, cy = area.centerY() + dy;
        int paint = paintFor(glyph);
        fill.setColor(shade(paint, 0.7f));
        c.drawCircle(cx, cy + u * 0.5f, d / 2, fill);
        fill.setColor(paint);
        c.drawCircle(cx, cy, d / 2, fill);
        glyph(c, glyph, cx, cy, d * 0.52f, 0xFFFFFFFF);
        ink(c, label, cx + d / 2 + u * 3, cy, u * 4.8f, u * 3, area.right - cx - d / 2 - u * 5.5f, danger ? shade(RED, 0.75f) : INK, BOLD, Paint.Align.LEFT);
    }

    @Override public void row(Canvas c, RectF area, String label, String value, int state, boolean adjustable, float u) {
        card(c, area, state, u);
        float dy = (state & PRESSED) != 0 ? u * 0.5f : 0, valueWidth = area.width() * 0.46f;
        q.set(area.right - valueWidth - u * 1.6f, area.top + u * 2 + dy, area.right - u * 1.6f, area.bottom - u * 2 + dy);
        fill.setColor(LEAF);
        c.drawRoundRect(q, q.height() / 2, q.height() / 2, fill);
        float inset = adjustable ? u * 6 : u * 2, vx = q.centerX(), vy = q.centerY(), left = q.left, rightEdge = q.right;
        ink(c, value, vx, vy, u * 4.5f, u * 3, q.width() - inset * 2, 0xFFFFFFFF, BOLD, Paint.Align.CENTER);
        if (adjustable) {
            chevron(c, left + u * 3.4f, vy, -u * 1.4f, u);
            chevron(c, rightEdge - u * 3.4f, vy, u * 1.4f, u);
        }
        ink(c, label, area.left + u * 3.5f, area.centerY() + dy, u * 4.4f, u * 3, left - area.left - u * 6, INK, BOLD, Paint.Align.LEFT);
    }

    private void chevron(Canvas c, float x, float y, float size, float u) {
        stroke.setStrokeWidth(u * 0.8f);
        stroke.setColor(0xFFFFFFFF);
        c.drawLine(x - size * 0.5f, y - Math.abs(size), x + size * 0.5f, y, stroke);
        c.drawLine(x + size * 0.5f, y, x - size * 0.5f, y + Math.abs(size), stroke);
    }

    @Override public void key(Canvas c, RectF area, String label, String glyph, int state, boolean primary, float u) {
        sign(c, area, primary ? LEAF : WOOD, state, u);
        float dy = (state & PRESSED) != 0 ? u * 0.6f : 0;
        TextPaint p = font(BOLD, u * 4.6f, 0xFFFFFBEA);
        String shown = fit(label, p, area.width() - u * 13, u * 3);
        float size = p.getTextSize(), textWidth = p.measureText(shown), icon = u * 5.2f, total = icon + u * 2 + textWidth, left = area.centerX() - total / 2;
        glyph(c, glyph, left + icon / 2, area.centerY() + dy, icon, 0xFFFFFBEA);
        p = font(BOLD, size, 0xFFFFFBEA);
        c.drawText(shown, left + icon + u * 2, baseline(p, area.centerY() + dy), p);
    }

    @Override public float noteHeight(String message, float width, float u) {
        return paragraph(message, font(MEDIUM, u * 4.3f, INK), width - u * 6).getHeight() + u * 5;
    }

    @Override public void note(Canvas c, RectF area, String message, float u) {
        fill.setColor(LEAF);
        c.drawCircle(area.left + u * 1.6f, area.top + u * 4.6f, u * 0.9f, fill);
        StaticLayout lines = paragraph(message, font(MEDIUM, u * 4.3f, INK), area.width() - u * 6);
        c.save();
        c.clipRect(area);
        c.translate(area.left + u * 4.5f, area.top + u * 2.2f);
        lines.draw(c);
        c.restore();
    }

    @Override public void hint(Canvas c, RectF area, String message, float u) {
        TextPaint p = font(BOLD, u * 2.9f, INK_SOFT);
        p.setTextAlign(Paint.Align.CENTER);
        c.drawText(fit(message, p, area.width(), u * 2), area.centerX(), baseline(p, area.centerY()), p);
    }

    @Override public void focus(Canvas c, RectF area, float phase, float u) {
        r.set(area.left - u * 1.2f, area.top - u * 1.2f, area.right + u * 1.2f, area.bottom + u * 1.2f);
        float radius = Math.min(u * 4.4f, r.height() / 2);
        stroke.setStrokeWidth(u * 2.8f);
        stroke.setColor(0xFFFFFFFF);
        c.drawRoundRect(r, radius, radius, stroke);
        stroke.setStrokeWidth(u * 1.5f);
        stroke.setColor(mix(FOCUS, 0xFFFFB648, phase * 0.6f));
        c.drawRoundRect(r, radius, radius, stroke);
    }
}
