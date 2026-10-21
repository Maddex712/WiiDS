package com.wither.nsmbw;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.Rect;
import android.graphics.RectF;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;

/** Owner-extracted Nintendo pixels and font metrics; never packaged with the application. */
final class ThorHudAssets {
    private final Map<String, Bitmap> images = new HashMap<>();
    private final Map<String, Font> fonts = new HashMap<>();
    private final Rect source = new Rect();
    private final RectF target = new RectF();
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    static final class Font {
        final Bitmap bitmap;
        final JSONObject glyphs;
        final float height;
        Font(Bitmap bitmap, JSONObject description) throws Exception {
            this.bitmap = bitmap;
            glyphs = description.getJSONObject("glyphs");
            height = (float) description.getDouble("lineHeight");
            if (!Float.isFinite(height) || height <= 0 || height > 512 || glyphs.length() > 4096)
                throw new IllegalArgumentException("Invalid private font metrics");
            for (java.util.Iterator<String> it = glyphs.keys(); it.hasNext();) {
                JSONObject g = glyphs.getJSONObject(it.next());
                int x = g.getInt("x"), y = g.getInt("y"), w = g.getInt("w"), h = g.getInt("h");
                double advance = g.getDouble("advance"), left = g.getDouble("left");
                if (x < 0 || y < 0 || w < 0 || h < 0 || x + (long)w > bitmap.getWidth()
                        || y + (long)h > bitmap.getHeight() || !Double.isFinite(advance)
                        || advance < 0 || advance > 512 || !Double.isFinite(left) || Math.abs(left) > 512)
                    throw new IllegalArgumentException("Invalid private glyph");
            }
        }
    }

    ThorHudAssets(File folder) throws Exception {
        File manifest = new File(folder, "manifest.json");
        if (!manifest.isFile() || manifest.length() > 2_000_000) throw new IllegalArgumentException("Missing HUD manifest");
        JSONObject root = new JSONObject(new String(Files.readAllBytes(manifest.toPath()), StandardCharsets.UTF_8));
        if (root.getInt("schema") != 1) throw new IllegalArgumentException("Unsupported HUD manifest");
        try {
            JSONObject list = root.getJSONObject("images");
            if (list.length() > 48) throw new IllegalArgumentException("Too many HUD images");
            for (java.util.Iterator<String> it = list.keys(); it.hasNext();) {
                String name = it.next(); images.put(name, load(folder, list.getJSONObject(name)));
            }
            list = root.getJSONObject("fonts");
            for (String name : new String[]{"main", "number", "message"}) {
                if (name.equals("message") && !list.has(name)) continue;
                JSONObject desc = list.getJSONObject(name);
                Bitmap bitmap = load(folder, desc);
                try { fonts.put(name, new Font(bitmap, desc)); }
                catch (Exception failure) { bitmap.recycle(); throw failure; }
            }
        } catch (Exception failure) { close(); throw failure; }
    }

    private static Bitmap load(File folder, JSONObject entry) throws Exception {
        String name = entry.getString("file");
        if (!name.equals(new File(name).getName()) || name.contains("\\") || name.contains("/") || name.equals(".."))
            throw new IllegalArgumentException("Invalid HUD filename");
        File file = new File(folder, name);
        if (!file.isFile() || file.length() > 32_000_000) throw new IllegalArgumentException("Missing HUD asset");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] bytes = new byte[65536]; int count;
            while ((count = input.read(bytes)) != -1) digest.update(bytes, 0, count);
        }
        StringBuilder hash = new StringBuilder();
        for (byte b : digest.digest()) hash.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        if (!hash.toString().equals(entry.getString("sha256"))) throw new IllegalArgumentException("HUD asset hash mismatch");
        BitmapFactory.Options options = new BitmapFactory.Options(); options.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getPath(), options);
        if (options.outWidth <= 0 || options.outHeight <= 0 || options.outWidth > 4096 || options.outHeight > 4096
                || (long) options.outWidth * options.outHeight > 12_000_000) throw new IllegalArgumentException("Invalid HUD image size");
        Bitmap bitmap = BitmapFactory.decodeFile(file.getPath());
        if (bitmap == null) throw new IllegalArgumentException("Invalid HUD image");
        return bitmap;
    }

    boolean has(String name) { return images.containsKey(name); }

    /** Width over height of a loaded image (1 when missing). */
    float aspect(String name) {
        Bitmap bitmap = images.get(name);
        return bitmap == null ? 1f : (float) bitmap.getWidth() / bitmap.getHeight();
    }

    float textWidth(String text, float height, boolean numbers) {
        return textWidth(text, height, numbers ? "number" : "main");
    }

    float textWidth(String text, float height, String face) {
        Font font = fonts.get(fonts.containsKey(face) ? face : "main");
        float cursor = 0, min = 0, max = 0;
        for (int i = 0; i < text.length(); i++) {
            JSONObject glyph = font.glyphs.optJSONObject(text.substring(i, i + 1));
            if (glyph == null) glyph = font.glyphs.optJSONObject("?");
            if (glyph == null) continue;
            float left = cursor + (float) glyph.optDouble("left");
            min = Math.min(min, left); max = Math.max(max, left + glyph.optInt("w"));
            cursor += (float)glyph.optDouble("advance");
        }
        return (Math.max(cursor, max) - min) * height / font.height;
    }

    void text(Canvas canvas, String text, float centerX, float y, float height, float maxWidth, boolean numbers) {
        text(canvas, text, centerX, y, height, maxWidth, numbers ? "number" : "main", 0xffffffff, 255);
    }

    /**
     * Original glyphs, centred on centerX. The atlases are white with a black outline, so a
     * multiply tint recolours only the fill (as the game's own layouts do) and keeps the outline.
     */
    void text(Canvas canvas, String text, float centerX, float y, float height, float maxWidth,
              String face, int tint, int alpha) {
        Font font = fonts.get(fonts.containsKey(face) ? face : "main");
        paint.setColorFilter(tint == 0xffffffff ? null : new PorterDuffColorFilter(tint, PorterDuff.Mode.MULTIPLY));
        paint.setAlpha(alpha);
        float width = textWidth(text, height, face);
        float scale = height / font.height * (width > maxWidth ? maxWidth / width : 1f);
        float x = centerX - Math.min(width, maxWidth) / 2;
        // Center complete original glyph extents, including negative bearings.
        float cursor = 0, min = 0;
        for (int i = 0; i < text.length(); i++) {
            JSONObject glyph = font.glyphs.optJSONObject(text.substring(i, i + 1));
            if (glyph == null) glyph = font.glyphs.optJSONObject("?");
            if (glyph == null) continue;
            min = Math.min(min, cursor + (float)glyph.optDouble("left"));
            cursor += (float)glyph.optDouble("advance");
        }
        x -= min * scale;
        for (int i = 0; i < text.length(); i++) {
            JSONObject glyph = font.glyphs.optJSONObject(text.substring(i, i + 1));
            if (glyph == null) glyph = font.glyphs.optJSONObject("?");
            if (glyph == null) continue;
            int gx = glyph.optInt("x"), gy = glyph.optInt("y"), w = glyph.optInt("w"), h = glyph.optInt("h");
            source.set(gx, gy, gx + w, gy + h);
            float left = x + (float)glyph.optDouble("left") * scale;
            target.set(left, y, left + w * scale, y + h * scale);
            canvas.drawBitmap(font.bitmap, source, target, paint);
            x += (float)glyph.optDouble("advance") * scale;
        }
        paint.setColorFilter(null); paint.setAlpha(255);
    }

    void image(Canvas canvas, String name, float centerX, float top, float height) {
        image(canvas, name, centerX, top, height, 255);
    }

    /** Native proportions always: only the height is chosen. */
    void image(Canvas canvas, String name, float centerX, float top, float height, int alpha) {
        Bitmap bitmap = images.get(name);
        if (bitmap == null || alpha <= 0) return;
        float width = height * bitmap.getWidth() / bitmap.getHeight();
        target.set(centerX - width / 2, top, centerX + width / 2, top + height);
        paint.setAlpha(alpha);
        canvas.drawBitmap(bitmap, null, target, paint);
        paint.setAlpha(255);
    }

    /**
     * A picture cropped to a circle (NSMB2-style world bubbles): the largest centred square,
     * nudged toward the top where course pictures keep their scenery, scaled into the circle.
     */
    void imageCircle(Canvas canvas, String name, float centerX, float centerY, float radius, int alpha) {
        Bitmap bitmap = images.get(name);
        if (bitmap == null || alpha <= 0) return;
        int side = Math.min(bitmap.getWidth(), bitmap.getHeight());
        int left = (bitmap.getWidth() - side) / 2, top = (bitmap.getHeight() - side) / 4;
        source.set(left, top, left + side, top + side);
        target.set(centerX - radius, centerY - radius, centerX + radius, centerY + radius);
        android.graphics.Path clip = new android.graphics.Path();
        clip.addCircle(centerX, centerY, radius, android.graphics.Path.Direction.CW);
        canvas.save();
        canvas.clipPath(clip);
        paint.setAlpha(alpha);
        canvas.drawBitmap(bitmap, source, target, paint);
        paint.setAlpha(255);
        canvas.restore();
    }

    /**
     * A horizontal strip (the course-intro world landscapes) filling the band at its native
     * aspect: scaled to the band height and cropped, never stretched. pan 0..1 picks the slice.
     */
    void strip(Canvas canvas, String name, RectF band, float pan, int alpha) {
        Bitmap bitmap = images.get(name);
        if (bitmap == null || alpha <= 0) return;
        float scale = band.height() / bitmap.getHeight();
        float visible = Math.min(bitmap.getWidth(), band.width() / scale);
        float left = (bitmap.getWidth() - visible) * Math.max(0f, Math.min(1f, pan));
        source.set(Math.round(left), 0, Math.round(left + visible), bitmap.getHeight());
        float drawn = visible * scale, x = band.centerX() - drawn / 2;
        target.set(x, band.top, x + drawn, band.bottom);
        paint.setAlpha(alpha);
        canvas.drawBitmap(bitmap, source, target, paint);
        paint.setAlpha(255);
    }

    /** Repeats a texture over an area at its native size times scale (subtle backgrounds). */
    void tile(Canvas canvas, String name, RectF area, float scale, int alpha) {
        Bitmap bitmap = images.get(name);
        if (bitmap == null || alpha <= 0) return;
        float w = bitmap.getWidth() * scale, h = bitmap.getHeight() * scale;
        canvas.save(); canvas.clipRect(area);
        paint.setAlpha(alpha);
        for (float y = area.top; y < area.bottom; y += h)
            for (float x = area.left; x < area.right; x += w) {
                target.set(x, y, x + w, y + h); canvas.drawBitmap(bitmap, null, target, paint);
            }
        paint.setAlpha(255);
        canvas.restore();
    }

    /**
     * A window built from the stock-item menu's own bevelled corner: the corner is mirrored to all
     * four sides and its straight run is extended along the edges. The interior is filled first.
     */
    void frame(Canvas canvas, RectF area, int fill, int edge, float corner) {
        Bitmap bitmap = images.get("frameCorner");
        fillPaint.setColor(fill);
        canvas.drawRoundRect(area.left + corner * .25f, area.top + corner * .25f, area.right - corner * .25f,
                area.bottom - corner * .25f, corner * .6f, corner * .6f, fillPaint);
        if (bitmap == null) return;
        int size = bitmap.getWidth(), last = size - 1;
        float w = area.width(), h = area.height();
        paint.setColorFilter(new PorterDuffColorFilter(edge, PorterDuff.Mode.MULTIPLY));
        // corner k: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left (mirrors of the top-left piece)
        for (int k = 0; k < 4; k++) {
            boolean right = k == 1 || k == 2, bottom = k >= 2;
            canvas.save();
            canvas.translate(right ? area.right : area.left, bottom ? area.bottom : area.top);
            canvas.scale(right ? -1 : 1, bottom ? -1 : 1);
            target.set(0, 0, corner, corner);
            canvas.drawBitmap(bitmap, null, target, paint);
            if (k == 0 || k == 2) {
                // Edges: the corner's last column runs along the top (bottom), its last row down the left (right).
                source.set(last, 0, size, size); target.set(corner, 0, w - corner, corner); canvas.drawBitmap(bitmap, source, target, paint);
                source.set(0, last, size, size); target.set(0, corner, corner, h - corner); canvas.drawBitmap(bitmap, source, target, paint);
            }
            canvas.restore();
        }
        paint.setColorFilter(null);
    }

    /** The game's rounded bar (stock-item window), as a three-part pill at any width. */
    void pill(Canvas canvas, RectF area, int tint, int alpha) {
        Bitmap bitmap = images.get("pill");
        if (bitmap == null) { fillPaint.setColor(tint); fillPaint.setAlpha(alpha);
            canvas.drawRoundRect(area, area.height() / 2, area.height() / 2, fillPaint); fillPaint.setAlpha(255); return; }
        int h = bitmap.getHeight(), cap = h / 2, w = bitmap.getWidth();
        float scale = area.height() / h, capW = cap * scale;
        paint.setColorFilter(new PorterDuffColorFilter(tint, PorterDuff.Mode.MULTIPLY)); paint.setAlpha(alpha);
        source.set(0, 0, cap, h); target.set(area.left, area.top, area.left + capW, area.bottom); canvas.drawBitmap(bitmap, source, target, paint);
        source.set(cap, 0, w - cap, h); target.set(area.left + capW, area.top, area.right - capW, area.bottom); canvas.drawBitmap(bitmap, source, target, paint);
        source.set(w - cap, 0, w, h); target.set(area.right - capW, area.top, area.right, area.bottom); canvas.drawBitmap(bitmap, source, target, paint);
        paint.setColorFilter(null); paint.setAlpha(255);
    }

    /** Original authored Wii plate, with its corner pixels kept at their native proportions. */
    void panel(Canvas canvas, float x, float y, float width, float height) {
        Bitmap bitmap = images.get("panel");
        if (bitmap == null) return;
        int edge = 8;
        float[] dx = {x, x + edge, x + width - edge, x + width};
        float[] dy = {y, y + edge, y + height - edge, y + height};
        int[] sx = {0, edge, bitmap.getWidth() - edge, bitmap.getWidth()};
        int[] sy = {0, edge, bitmap.getHeight() - edge, bitmap.getHeight()};
        for (int row = 0; row < 3; row++) for (int col = 0; col < 3; col++) {
            source.set(sx[col], sy[row], sx[col + 1], sy[row + 1]);
            target.set(dx[col], dy[row], dx[col + 1], dy[row + 1]);
            canvas.drawBitmap(bitmap, source, target, paint);
        }
    }

    void close() {
        for (Bitmap bitmap : images.values()) bitmap.recycle();
        for (Font font : fonts.values()) font.bitmap.recycle();
        images.clear(); fonts.clear();
    }
}
