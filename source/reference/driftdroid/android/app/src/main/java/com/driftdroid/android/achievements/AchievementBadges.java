package com.driftdroid.android.achievements;

import android.content.Context;
import android.graphics.*;
import android.util.LruCache;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;

/**
 * Badge pictures for every achievement surface. WiiDS medallions frame art the device already
 * extracted from the player's own NSMB disc (nsmbw/ThorHud, hash-checked against its manifest);
 * imported RA badges use RA's own pictures in a rounded-square frame. Missing art falls back to
 * drawn WiiDS glyphs. Earned badges are full colour; locked ones are desaturated, dimmed and
 * marked with a padlock. Call from a worker: composition decodes files.
 */
public final class AchievementBadges {
    private final File hud;
    private final RaBadges ra;
    private final LruCache<String, Bitmap> cache = new LruCache<String, Bitmap>(24 * 1024 * 1024) {
        @Override protected int sizeOf(String key, Bitmap value) { return value.getByteCount(); }
    };
    private Map<String, Bitmap> hudArt;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);

    public AchievementBadges(Context context) {
        File external = context.getExternalFilesDir(null);
        hud = external == null ? null : new File(external, "nsmbw/ThorHud");
        ra = new RaBadges(new File(context.getNoBackupFilesDir(), "achievements/ra-badges"));
    }

    public synchronized Bitmap badge(AchievementCatalog.Item item, int size) {
        String key = item.badge + "|" + item.earned() + "|" + size;
        Bitmap cached = cache.get(key);
        if (cached != null) return cached;
        Bitmap made = compose(item, size);
        cache.put(key, made);
        if (!pictureReady(item)) waiting.add(key);
        return made;
    }

    /** Keys of RA badges composed before their picture arrived (drawn as the fallback trophy). */
    private final Set<String> waiting = new HashSet<>();

    /** RA pictures arrive after an import: recompose only badges still showing the fallback. */
    public synchronized void forgetImported() {
        for (String key : waiting) cache.remove(key);
        waiting.clear();
    }

    /** True once [item]'s own RA picture is on the device (or it never has one). */
    public boolean pictureReady(AchievementCatalog.Item item) {
        if (item.source != AchievementCatalog.Source.RETROACHIEVEMENTS || !item.badge.startsWith("ra:")) return true;
        try { return ra.file(item.badge.substring(3)).isFile(); } catch (java.io.IOException invalid) { return true; }
    }

    // ------------------------------------------------------------------ source art
    private static final String[] HUD_FILES = {"starCoin", "heroMario", "course1", "world1"};

    /** The disc-derived pictures, each verified against the manifest written when they were built. */
    private Map<String, Bitmap> hudArt() {
        if (hudArt != null) return hudArt;
        Map<String, Bitmap> art = new HashMap<>();
        try {
            File manifest = new File(hud, "manifest.json");
            if (hud != null && manifest.isFile() && manifest.length() <= 512 * 1024) {
                JSONObject root = new JSONObject(new String(Files.readAllBytes(manifest.toPath()), StandardCharsets.UTF_8));
                if (root.optInt("schema") == 1 && "private-nsmbw-retail-hud".equals(root.optString("kind"))) {
                    JSONObject images = root.getJSONObject("images");
                    for (String name : HUD_FILES) {
                        JSONObject entry = images.optJSONObject(name);
                        if (entry == null || !(name + ".png").equals(entry.optString("file"))) continue;
                        File file = new File(hud, name + ".png");
                        if (!file.isFile() || file.length() > 2 * 1024 * 1024) continue;
                        byte[] bytes = Files.readAllBytes(file.toPath());
                        if (!hex(MessageDigest.getInstance("SHA-256").digest(bytes)).equals(entry.optString("sha256"))) continue;
                        Bitmap bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
                        if (bitmap != null && bitmap.getWidth() <= 1024 && bitmap.getHeight() <= 1024) art.put(name, bitmap);
                    }
                }
            }
        } catch (Exception unavailable) { art.clear(); }
        hudArt = art;
        return art;
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder();
        for (byte b : bytes) out.append(String.format(Locale.ROOT, "%02x", b & 255));
        return out.toString();
    }

    // ------------------------------------------------------------------ composition
    private Bitmap compose(AchievementCatalog.Item item, int size) {
        boolean imported = item.source == AchievementCatalog.Source.RETROACHIEVEMENTS;
        boolean earned = item.earned();
        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        float s = size, cx = s / 2f, cy = s / 2f;
        Path outer = shape(imported, cx, cy, s * 0.47f), inner = shape(imported, cx, cy, s * 0.385f);
        // Soft contact shadow.
        paint.setShader(new RadialGradient(cx, cy + s * 0.025f, s * 0.5f, new int[]{0x66000000, 0x00000000}, new float[]{0.86f, 1f}, Shader.TileMode.CLAMP));
        c.drawCircle(cx, cy + s * 0.025f, s * 0.5f, paint);
        // Metal rim: gold when earned, brushed steel while locked.
        int[] rim = earned ? new int[]{0xfffff3b8, 0xfff2c14e, 0xffa86f0e, 0xfff7d774}
                           : new int[]{0xffd3d9de, 0xff858e97, 0xff4c545c, 0xffaab2b9};
        paint.setShader(new LinearGradient(0, s * 0.03f, 0, s * 0.97f, rim, new float[]{0f, 0.38f, 0.72f, 1f}, Shader.TileMode.CLAMP));
        c.drawPath(outer, paint);
        paint.setShader(null);
        paint.setColor(0xff141c28);
        c.drawPath(shape(imported, cx, cy, s * 0.40f), paint);
        // Field: the launcher's restrained blue when earned, slate while locked.
        paint.setShader(new RadialGradient(cx, cy - s * 0.12f, s * 0.48f,
            earned ? new int[]{0xff5fb2f2, 0xff1d5aa3, 0xff0f2b57} : new int[]{0xff48515c, 0xff262c34, 0xff161a20},
            new float[]{0f, 0.6f, 1f}, Shader.TileMode.CLAMP));
        c.drawPath(inner, paint);
        paint.setShader(null);

        int layer = c.save();
        c.clipPath(inner);
        if (!earned) {
            // Desaturated art under a dark veil keeps locked badges recognisable but clearly unearned.
            ColorMatrix grey = new ColorMatrix(); grey.setSaturation(0f);
            paint.setColorFilter(new ColorMatrixColorFilter(grey));
        }
        boolean drew = imported ? drawImported(c, item, s) : drawLocal(c, item.badge, s);
        if (!drew) drawGlyph(c, item.badge, s, earned);
        paint.setColorFilter(null);
        if (!earned) { paint.setColor(0x990c1016); c.drawPath(inner, paint); }
        // Gloss across the top of the field.
        paint.setShader(new LinearGradient(0, s * 0.1f, 0, s * 0.52f, 0x40ffffff, 0x00ffffff, Shader.TileMode.CLAMP));
        c.drawOval(new RectF(s * 0.16f, s * 0.08f, s * 0.84f, s * 0.56f), paint);
        paint.setShader(null);
        c.restoreToCount(layer);
        if (!earned) drawLock(c, s);
        return out;
    }

    private static Path shape(boolean square, float cx, float cy, float r) {
        Path path = new Path();
        if (square) path.addRoundRect(new RectF(cx - r, cy - r, cx + r, cy + r), r * 0.36f, r * 0.36f, Path.Direction.CW);
        else path.addCircle(cx, cy, r, Path.Direction.CW);
        return path;
    }

    private boolean drawImported(Canvas c, AchievementCatalog.Item item, float s) {
        if (!item.badge.startsWith("ra:")) return false;
        Bitmap picture = ra.read(item.badge.substring(3));
        if (picture == null) return false;
        c.drawBitmap(picture, null, new RectF(s * 0.115f, s * 0.115f, s * 0.885f, s * 0.885f), paint);
        return true;
    }

    private boolean drawLocal(Canvas c, String id, float s) {
        Map<String, Bitmap> art = hudArt();
        switch (id) {
            case "nsmb.star_coin.v1": {
                Bitmap coin = art.get("starCoin");
                if (coin == null) return false;
                c.drawBitmap(coin, null, new RectF(s * 0.22f, s * 0.22f, s * 0.78f, s * 0.78f), paint);
                return true;
            }
            case "nsmb.three_stars.v1": {
                Bitmap coin = art.get("starCoin");
                if (coin == null) return false;
                float d = s * 0.34f;
                c.drawBitmap(coin, null, new RectF(s * 0.14f, s * 0.42f, s * 0.14f + d, s * 0.42f + d), paint);
                c.drawBitmap(coin, null, new RectF(s * 0.86f - d, s * 0.42f, s * 0.86f, s * 0.42f + d), paint);
                c.drawBitmap(coin, null, new RectF(s * 0.5f - d / 2f, s * 0.17f, s * 0.5f + d / 2f, s * 0.17f + d), paint);
                return true;
            }
            case "nsmb.first_steps.v1": {
                // World 1-1's own course picture from the course-select screen.
                Bitmap course = art.get("course1");
                if (course == null) return false;
                float h = s * 0.84f, w = h * course.getWidth() / course.getHeight();
                c.drawBitmap(course, null, new RectF(s / 2f - w / 2f, s * 0.08f, s / 2f + w / 2f, s * 0.08f + h), paint);
                return true;
            }
            case "nsmb.first_exit.v1": {
                // Mario from the course-results screen over the World 1 panorama.
                Bitmap strip = art.get("world1"), mario = art.get("heroMario");
                if (mario == null) return false;
                if (strip != null) {
                    float h = s * 0.42f, w = h * strip.getWidth() / strip.getHeight();
                    int cut = Math.max(1, Math.round(strip.getHeight() * 1.6f));
                    Rect source = new Rect(strip.getWidth() / 2 - cut, 0, strip.getWidth() / 2 + cut, strip.getHeight());
                    c.drawBitmap(strip, source, new RectF(0, s * 0.56f, s, s * 0.56f + h), paint);
                }
                c.drawBitmap(mario, null, new RectF(s * 0.2f, s * 0.12f, s * 0.8f, s * 0.72f), paint);
                return true;
            }
        }
        return false;
    }

    /** Drawn stand-ins when the disc art is missing (or for RA badges not yet fetched). */
    private void drawGlyph(Canvas c, String id, float s, boolean earned) {
        Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        fill.setColor(earned ? 0xfff7d774 : 0xffb9c1c8);
        Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
        ink.setStyle(Paint.Style.STROKE); ink.setStrokeWidth(s * 0.03f); ink.setColor(0xff10223e); ink.setStrokeJoin(Paint.Join.ROUND);
        switch (id) {
            case "nsmb.star_coin.v1": star(c, s / 2f, s / 2f, s * 0.24f, fill, ink); break;
            case "nsmb.three_stars.v1":
                star(c, s * 0.31f, s * 0.6f, s * 0.13f, fill, ink); star(c, s * 0.69f, s * 0.6f, s * 0.13f, fill, ink);
                star(c, s * 0.5f, s * 0.34f, s * 0.14f, fill, ink); break;
            case "nsmb.first_steps.v1": {
                Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
                text.setTypeface(Typeface.DEFAULT_BOLD); text.setTextSize(s * 0.26f); text.setTextAlign(Paint.Align.CENTER);
                text.setStyle(Paint.Style.FILL_AND_STROKE); text.setStrokeWidth(s * 0.035f); text.setColor(0xff10223e);
                c.drawText("1-1", s / 2f, s * 0.6f, text);
                text.setStyle(Paint.Style.FILL); text.setColor(fill.getColor());
                c.drawText("1-1", s / 2f, s * 0.6f, text);
                break;
            }
            case "nsmb.first_exit.v1": {
                // Goal pole and pennant.
                c.drawRect(s * 0.44f, s * 0.22f, s * 0.5f, s * 0.78f, fill);
                c.drawRect(s * 0.44f, s * 0.22f, s * 0.5f, s * 0.78f, ink);
                Path flag = new Path(); flag.moveTo(s * 0.5f, s * 0.25f); flag.lineTo(s * 0.74f, s * 0.34f); flag.lineTo(s * 0.5f, s * 0.43f); flag.close();
                c.drawPath(flag, fill); c.drawPath(flag, ink);
                c.drawCircle(s * 0.47f, s * 0.21f, s * 0.045f, fill);
                break;
            }
            default: {
                // A trophy for imported RA entries without a fetched picture.
                Path cup = new Path();
                cup.moveTo(s * 0.32f, s * 0.26f); cup.lineTo(s * 0.68f, s * 0.26f);
                cup.cubicTo(s * 0.68f, s * 0.52f, s * 0.58f, s * 0.58f, s * 0.5f, s * 0.58f);
                cup.cubicTo(s * 0.42f, s * 0.58f, s * 0.32f, s * 0.52f, s * 0.32f, s * 0.26f); cup.close();
                c.drawPath(cup, fill); c.drawPath(cup, ink);
                c.drawRect(s * 0.47f, s * 0.58f, s * 0.53f, s * 0.68f, fill);
                c.drawRoundRect(new RectF(s * 0.36f, s * 0.68f, s * 0.64f, s * 0.75f), s * 0.02f, s * 0.02f, fill);
                c.drawRoundRect(new RectF(s * 0.36f, s * 0.68f, s * 0.64f, s * 0.75f), s * 0.02f, s * 0.02f, ink);
            }
        }
    }

    private static void star(Canvas c, float cx, float cy, float r, Paint fill, Paint ink) {
        Path path = new Path();
        for (int k = 0; k < 10; k++) {
            double a = Math.PI / 5 * k - Math.PI / 2;
            float radius = k % 2 == 0 ? r : r * 0.45f;
            float x = cx + (float) Math.cos(a) * radius, y = cy + (float) Math.sin(a) * radius;
            if (k == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        path.close();
        c.drawPath(path, fill); c.drawPath(path, ink);
    }

    private static void drawLock(Canvas c, float s) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        float x = s * 0.76f, y = s * 0.76f, r = s * 0.15f;
        p.setColor(0xf0141a22); c.drawCircle(x, y, r, p);
        p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(s * 0.018f); p.setColor(0xffaab2b9); c.drawCircle(x, y, r, p);
        // Padlock: shackle and body.
        p.setColor(0xffffffff); p.setStrokeWidth(s * 0.022f);
        c.drawArc(new RectF(x - r * 0.36f, y - r * 0.62f, x + r * 0.36f, y + r * 0.08f), 180, 180, false, p);
        p.setStyle(Paint.Style.FILL);
        c.drawRoundRect(new RectF(x - r * 0.5f, y - r * 0.18f, x + r * 0.5f, y + r * 0.5f), r * 0.12f, r * 0.12f, p);
    }
}
