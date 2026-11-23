package dev.wiids.colosseum;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.text.StaticLayout;
import android.text.TextPaint;

import com.driftdroid.android.dualscreen.WiiDsSkin;

import java.util.Locale;

/**
 * Orre field instrument: rusted iron chassis, sand-blasted steel plates, a dark instrument well and
 * brass fittings, after the Outskirt Stand locomotive and the game's P★DA. Shadow data is violet.
 */
final class OrreSkin extends WiiDsSkin {
    static final int RUST_HI = 0xFFB96A3C, RUST = 0xFF8A4526, RUST_LOW = 0xFF4A2414, IRON = 0xFF241A15, WELL = 0xFF0E0A08,
            SAND = 0xFFEAD9AA, SAND_MID = 0xFFC9B682, SAND_LOW = 0xFF8F7D55, BRASS = 0xFFD9A24A, BRASS_LOW = 0xFF8C6222,
            INK = 0xFF17100B, FOCUS = 0xFFFFF0B8, SHADOW = 0xFFA765FF, SHADOW_LOW = 0xFF3A1E66, COAT = 0xFF4E8BEA,
            HP_GOOD = 0xFF5BD95A, HP_MID = 0xFFF2C230, HP_LOW = 0xFFEF5338;
    static final Typeface HEAVY = Typeface.create("sans-serif-condensed", Typeface.BOLD),
            REGULAR = Typeface.create("sans-serif-condensed", Typeface.NORMAL);
    private final RectF r = new RectF(), q = new RectF();

    @Override public int scrim() { return 0xC40A0705; }

    @Override public void backdrop(Canvas c, float w, float h, float u) {
        fill.setColor(0xFF000000);
        fill.setShader(new LinearGradient(0, 0, 0, h, 0xFF1D1410, 0xFF0B0806, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, fill);
        fill.setShader(null);
    }

    @Override public void panel(Canvas c, RectF panel, float u) {
        chassis(c, panel, u);
        r.set(panel.left + u * 2.2f, panel.top + u * 2.2f, panel.right - u * 2.2f, panel.bottom - u * 2.2f);
        well(c, r, u * 3, u);
    }

    /** The outer iron frame with a rust face and four bolts. */
    void chassis(Canvas c, RectF outer, float u) {
        Path shape = chamfer(outer, u * 4.5f);
        fill.setColor(0xFF000000);
        fill.setShader(new LinearGradient(0, outer.top, 0, outer.bottom, new int[]{RUST_HI, RUST, RUST_LOW}, new float[]{0, 0.18f, 1}, Shader.TileMode.CLAMP));
        c.drawPath(shape, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(u * 0.7f);
        stroke.setColor(0xFF140C08);
        c.drawPath(shape, stroke);
        q.set(outer.left + u * 0.8f, outer.top + u * 0.8f, outer.right - u * 0.8f, outer.bottom - u * 0.8f);
        stroke.setStrokeWidth(u * 0.35f);
        stroke.setColor(0x55FFD9A8);
        c.drawPath(chamfer(q, u * 4), stroke);
        float k = u * 4.6f;
        bolt(c, outer.left + k, outer.top + u * 1.15f, u * 0.55f);
        bolt(c, outer.right - k, outer.top + u * 1.15f, u * 0.55f);
        bolt(c, outer.left + k, outer.bottom - u * 1.15f, u * 0.55f);
        bolt(c, outer.right - k, outer.bottom - u * 1.15f, u * 0.55f);
    }

    void bolt(Canvas c, float x, float y, float radius) {
        fill.setColor(0xFF2A160C);
        c.drawCircle(x, y + radius * 0.25f, radius * 1.25f, fill);
        fill.setColor(0xFFD7A77A);
        c.drawCircle(x, y, radius, fill);
        fill.setColor(0x80FFFFFF);
        c.drawCircle(x - radius * 0.3f, y - radius * 0.3f, radius * 0.35f, fill);
    }

    /** A dark inset instrument well with a brass keyline. */
    void well(Canvas c, RectF area, float cut, float u) {
        Path shape = chamfer(area, cut);
        fill.setColor(0xFF000000);
        fill.setShader(new LinearGradient(0, area.top, 0, area.bottom, 0xFF17110D, WELL, Shader.TileMode.CLAMP));
        c.drawPath(shape, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(u * 0.9f);
        stroke.setColor(0xD0000000);
        c.drawPath(shape, stroke);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(alpha(BRASS, 0.45f));
        c.drawPath(shape, stroke);
    }

    /** A machined plate. {@code tint} 0 is sand steel. */
    void plate(Canvas c, RectF area, float cut, int tint, int state, float u) {
        boolean down = (state & PRESSED) != 0, hot = (state & FOCUSED) != 0;
        int top = tint == 0 ? 0xFFE9DAAE : mix(tint, 0xFFFFFFFF, 0.22f), mid = tint == 0 ? SAND_MID : tint,
                low = tint == 0 ? SAND_LOW : shade(tint, 0.55f);
        if (hot) { top = mix(top, 0xFFFFFFFF, 0.25f); mid = mix(mid, 0xFFFFFFFF, 0.14f); }
        if (down) { top = shade(top, 0.8f); mid = shade(mid, 0.8f); low = shade(low, 0.8f); }
        q.set(area.left, area.top + u * 0.7f, area.right, area.bottom + u * 0.7f);
        fill.setColor(0xB0000000);
        c.drawPath(chamfer(q, cut), fill);
        Path shape = chamfer(area, cut);
        fill.setColor(0xFF000000);
        fill.setShader(new LinearGradient(0, area.top, 0, area.bottom, new int[]{top, mid, low}, new float[]{0, 0.45f, 1}, Shader.TileMode.CLAMP));
        c.drawPath(shape, fill);
        fill.setShader(null);
        stroke.setStrokeWidth(u * 0.5f);
        stroke.setColor(0xFF120C08);
        c.drawPath(shape, stroke);
        q.set(area.left + u * 0.7f, area.top + u * 0.7f, area.right - u * 0.7f, area.bottom - u * 0.7f);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(0x70FFFFFF);
        c.drawPath(chamfer(q, Math.max(0, cut - u * 0.5f)), stroke);
    }

    /** Stamped capitals with open tracking. Returns the drawn width. */
    float caps(Canvas c, String label, float x, float centerY, float size, float min, float width, int color, Paint.Align align) {
        TextPaint p = font(HEAVY, size, color);
        p.setLetterSpacing(0.07f);
        String shown = fit(label.toUpperCase(Locale.ROOT), p, width, min);
        p.setTextAlign(align);
        c.drawText(shown, x, baseline(p, centerY), p);
        float measured = p.measureText(shown);
        p.setLetterSpacing(0);
        return measured;
    }

    void label(Canvas c, String value, float x, float centerY, float size, float width, int color, Typeface face, Paint.Align align) {
        TextPaint p = font(face, size, color);
        String shown = fit(value, p, width, size * 0.7f);
        p.setTextAlign(align);
        c.drawText(shown, x, baseline(p, centerY), p);
    }

    /** HP track with the game's green, yellow and red thresholds. A negative maximum draws an empty track. */
    void hpBar(Canvas c, RectF bar, int hp, int max, float u) {
        fill.setColor(0xFF050403);
        c.drawRoundRect(bar, bar.height() / 2, bar.height() / 2, fill);
        if (max > 0) {
            float ratio = Math.max(0f, Math.min(1f, hp / (float) max));
            int color = ratio > 0.5f ? HP_GOOD : ratio > 0.2f ? HP_MID : HP_LOW;
            q.set(bar.left + u * 0.35f, bar.top + u * 0.35f, bar.left + u * 0.35f + (bar.width() - u * 0.7f) * ratio, bar.bottom - u * 0.35f);
            if (q.width() > 0) {
                fill.setColor(0xFF000000);
        fill.setShader(new LinearGradient(0, q.top, 0, q.bottom, mix(color, 0xFFFFFFFF, 0.35f), color, Shader.TileMode.CLAMP));
                c.drawRoundRect(q, q.height() / 2, q.height() / 2, fill);
                fill.setShader(null);
            }
        }
        stroke.setStrokeWidth(u * 0.25f);
        stroke.setColor(alpha(SAND, 0.5f));
        c.drawRoundRect(bar, bar.height() / 2, bar.height() / 2, stroke);
    }

    /** Heart gauge: the share of the heart still closed, in Shadow violet. */
    void heartGauge(Canvas c, RectF bar, int percent, float u) {
        fill.setColor(0xFF07040D);
        c.drawRect(bar, fill);
        int segments = 5;
        float gap = u * 0.5f, width = (bar.width() - gap * (segments + 1)) / segments;
        for (int i = 0; i < segments; i++) {
            float left = bar.left + gap + i * (width + gap), share = Math.max(0f, Math.min(1f, (percent - i * 20) / 20f));
            q.set(left, bar.top + gap, left + width, bar.bottom - gap);
            fill.setColor(SHADOW_LOW);
            c.drawRect(q, fill);
            if (share > 0) {
                q.right = left + width * share;
                fill.setColor(SHADOW);
                c.drawRect(q, fill);
            }
        }
        stroke.setStrokeWidth(u * 0.25f);
        stroke.setColor(alpha(SHADOW, 0.8f));
        c.drawRect(bar, stroke);
    }

    @Override public void header(Canvas c, RectF area, String title, String subtitle, Bitmap art, float u) {
        float right = area.right;
        if (art != null) {
            float height = area.height() * 0.78f, width = height * art.getWidth() / art.getHeight();
            r.set(area.right - width - u * 1.6f, area.centerY() - height / 2 - u * 0.8f, area.right, area.centerY() + height / 2 + u * 0.8f);
            fill.setColor(0xFF000000);
            c.drawPath(chamfer(r, u * 1.2f), fill);
            stroke.setStrokeWidth(u * 0.3f);
            stroke.setColor(alpha(BRASS, 0.7f));
            c.drawPath(chamfer(r, u * 1.2f), stroke);
            q.set(r.left + u * 0.8f, r.top + u * 0.8f, r.right - u * 0.8f, r.bottom - u * 0.8f);
            c.drawBitmap(art, null, q, image);
            right = r.left - u * 3;
        }
        caps(c, "WiiDS  ·  " + subtitle, area.left + u * 0.6f, area.top + u * 2.6f, u * 3.1f, u * 2.2f, right - area.left, BRASS, Paint.Align.LEFT);
        caps(c, title, area.left + u * 0.4f, area.top + u * 9.2f, u * 8.4f, u * 4.5f, right - area.left, SAND, Paint.Align.LEFT);
        // Instrument rule with tick marks under the header.
        fill.setColor(alpha(BRASS, 0.75f));
        c.drawRect(area.left, area.bottom + u * 0.9f, area.right, area.bottom + u * 1.2f, fill);
        for (int i = 0; i <= 20; i++) {
            float x = area.left + area.width() * i / 20f;
            c.drawRect(x - u * 0.12f, area.bottom + u * 1.2f, x + u * 0.12f, area.bottom + (i % 5 == 0 ? u * 2.3f : u * 1.7f), fill);
        }
    }

    @Override public void tile(Canvas c, RectF area, String label, String glyph, int state, boolean danger, float u) {
        r.set(area);
        if ((state & PRESSED) != 0) r.offset(0, u * 0.4f);
        plate(c, r, u * 2.6f, danger ? 0xFFB5522E : 0, state, u);
        float k = u * 2.4f;
        bolt(c, r.left + k, r.top + k, u * 0.45f);
        bolt(c, r.right - k, r.top + k, u * 0.45f);
        bolt(c, r.left + k, r.bottom - k, u * 0.45f);
        bolt(c, r.right - k, r.bottom - k, u * 0.45f);
        float d = Math.min(r.height() - u * 7, u * 13), cx = r.left + u * 5 + d / 2, cy = r.centerY();
        fill.setColor(0xFF0B0806);
        c.drawCircle(cx, cy, d / 2, fill);
        stroke.setStrokeWidth(u * 0.7f);
        stroke.setColor(danger ? 0xFFFFC9A8 : RUST);
        c.drawCircle(cx, cy, d / 2, stroke);
        glyph(c, glyph, cx, cy, d * 0.55f, danger ? 0xFFFFD9BF : SAND);
        q.set(cx + d / 2 + u * 2.5f, cy - u * 4.3f, r.right - u * 4.5f, cy + u * 4.3f);
        fill.setColor(0xE6100B08);
        c.drawPath(chamfer(q, u * 1.2f), fill);
        caps(c, label, q.left + u * 2, q.centerY(), u * 4.5f, u * 2.8f, q.width() - u * 4, SAND, Paint.Align.LEFT);
    }

    @Override public void row(Canvas c, RectF area, String label, String value, int state, boolean adjustable, float u) {
        fill.setColor((state & PRESSED) != 0 ? 0xFF2B2018 : 0xFF1C1511);
        c.drawPath(chamfer(area, u * 2), fill);
        stroke.setStrokeWidth(u * 0.3f);
        stroke.setColor(alpha(SAND_LOW, 0.8f));
        c.drawPath(chamfer(area, u * 2), stroke);
        float valueWidth = area.width() * 0.46f;
        r.set(area.right - valueWidth - u * 1.4f, area.top + u * 1.6f, area.right - u * 1.4f, area.bottom - u * 1.6f);
        plate(c, r, u * 1.4f, BRASS, 0, u);
        float inset = adjustable ? u * 6 : u * 2;
        label(c, value, r.centerX(), r.centerY(), u * 4.5f, r.width() - inset * 2, INK, HEAVY, Paint.Align.CENTER);
        if (adjustable) {
            chevron(c, r.left + u * 3.2f, r.centerY(), -u * 1.5f, INK, u);
            chevron(c, r.right - u * 3.2f, r.centerY(), u * 1.5f, INK, u);
        }
        caps(c, label, area.left + u * 3.5f, area.centerY(), u * 4.2f, u * 2.8f, r.left - area.left - u * 6, SAND, Paint.Align.LEFT);
    }

    private void chevron(Canvas c, float x, float y, float size, int color, float u) {
        stroke.setStrokeWidth(u * 0.8f);
        stroke.setColor(color);
        c.drawLine(x - size * 0.5f, y - Math.abs(size), x + size * 0.5f, y, stroke);
        c.drawLine(x + size * 0.5f, y, x - size * 0.5f, y + Math.abs(size), stroke);
    }

    @Override public void key(Canvas c, RectF area, String label, String glyph, int state, boolean primary, float u) {
        r.set(area);
        if ((state & PRESSED) != 0) r.offset(0, u * 0.4f);
        plate(c, r, u * 2.4f, primary ? BRASS : 0, state, u);
        TextPaint p = font(HEAVY, u * 4.6f, INK);
        p.setLetterSpacing(0.07f);
        String shown = fit(label.toUpperCase(Locale.ROOT), p, r.width() - u * 13, u * 2.8f);
        float textWidth = p.measureText(shown), icon = u * 5.2f, total = icon + u * 2 + textWidth, left = r.centerX() - total / 2;
        glyph(c, glyph, left + icon / 2, r.centerY(), icon, INK);
        p = font(HEAVY, p.getTextSize(), INK);
        p.setLetterSpacing(0.07f);
        c.drawText(shown, left + icon + u * 2, baseline(p, r.centerY()), p);
        p.setLetterSpacing(0);
    }

    @Override public float noteHeight(String message, float width, float u) {
        return paragraph(message, font(REGULAR, u * 4.3f, SAND), width - u * 9).getHeight() + u * 7;
    }

    @Override public void note(Canvas c, RectF area, String message, float u) {
        fill.setColor(0xFF19120E);
        c.drawPath(chamfer(area, u * 2), fill);
        fill.setColor(BRASS);
        c.drawRect(area.left + u * 1.6f, area.top + u * 2.4f, area.left + u * 2.3f, area.bottom - u * 2.4f, fill);
        StaticLayout lines = paragraph(message, font(REGULAR, u * 4.3f, SAND), area.width() - u * 9);
        c.save();
        c.clipRect(area);
        c.translate(area.left + u * 5, area.top + u * 3.5f);
        lines.draw(c);
        c.restore();
    }

    @Override public void hint(Canvas c, RectF area, String message, float u) {
        TextPaint p = font(HEAVY, u * 2.9f, alpha(SAND, 0.62f));
        p.setLetterSpacing(0.08f);
        p.setTextAlign(Paint.Align.CENTER);
        c.drawText(fit(message, p, area.width(), u * 2), area.centerX(), baseline(p, area.centerY()), p);
        p.setLetterSpacing(0);
    }

    @Override public void focus(Canvas c, RectF area, float phase, float u) {
        r.set(area.left - u * 1.1f, area.top - u * 1.1f, area.right + u * 1.1f, area.bottom + u * 1.1f);
        Path shape = chamfer(r, u * 3.2f);
        stroke.setStrokeWidth(u * 2.6f);
        stroke.setColor(0xE6000000);
        c.drawPath(shape, stroke);
        stroke.setStrokeWidth(u * 1.35f);
        stroke.setColor(mix(BRASS, FOCUS, 0.55f + 0.45f * phase));
        c.drawPath(shape, stroke);
    }
}
