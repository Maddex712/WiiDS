package com.driftdroid.android.dualscreen;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Original Wii layout art and bitmap-font metrics extracted from the owner's disc. */
final class ThorWiiHudAssets {
    final Bitmap normalRow, selectedRow, timeLabel, colon, dot, searchlight;
    final Bitmap[] ranks = new Bitmap[12], portraits = new Bitmap[24], digits = new Bitmap[10];

    ThorWiiHudAssets(File folder) throws Exception {
        JSONObject info = new JSONObject(new String(Files.readAllBytes(new File(folder, "hud.json").toPath()), StandardCharsets.UTF_8));
        if (info.getInt("schema") != 1) throw new IllegalArgumentException("Unsupported Wii HUD manifest");
        JSONObject rows = info.getJSONObject("rows"), timer = info.getJSONObject("timer");
        normalRow = load(folder, rows.getJSONObject("normal").getString("texture"));
        selectedRow = load(folder, rows.getJSONObject("selected").getString("texture"));
        timeLabel = load(folder, timer.getString("label"));
        colon = load(folder, timer.getString("colon"));
        dot = load(folder, timer.getString("dot"));
        searchlight = load(folder, info.getJSONObject("images").getJSONObject("searchlight").getString("texture"));
        JSONArray numbers = timer.getJSONArray("digits"), faces = info.getJSONArray("faces");
        for (int i = 0; i < digits.length; i++) digits[i] = load(folder, numbers.getString(i));
        for (int i = 0; i < faces.length(); i++) {
            JSONObject face = faces.getJSONObject(i);
            portraits[face.getInt("id")] = load(folder, face.getString("texture"));
        }
        for (Bitmap face : portraits) if (face == null) throw new IllegalArgumentException("Incomplete Wii portraits");
        JSONObject font = info.getJSONObject("fonts").getJSONObject("main");
        Bitmap atlas = load(folder, font.getString("texture"));
        JSONObject glyphs = font.getJSONObject("glyphs");
        int height = font.getInt("lineHeight");
        JSONArray labels = info.getJSONArray("ranks");
        Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
        Rect source = new Rect(); RectF target = new RectF();
        for (int i = 0; i < labels.length(); i++) {
            JSONObject label = labels.getJSONObject(i);
            String text = label.getString("text");
            float advance = 0, left = 0, right = 0;
            for (int j = 0; j < text.length(); j++) {
                JSONObject glyph = glyphs.getJSONObject(text.substring(j, j + 1));
                left = Math.min(left, advance + (float) glyph.getDouble("left"));
                right = Math.max(right, advance + (float) glyph.getDouble("left") + glyph.getInt("w"));
                advance += (float) glyph.getDouble("advance");
            }
            Bitmap rank = Bitmap.createBitmap((int) Math.ceil(Math.max(right, advance) - left), height, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(rank); advance = -left;
            for (int j = 0; j < text.length(); j++) {
                JSONObject glyph = glyphs.getJSONObject(text.substring(j, j + 1));
                int x = glyph.getInt("x"), y = glyph.getInt("y"), w = glyph.getInt("w"), h = glyph.getInt("h");
                source.set(x, y, x + w, y + h);
                float gx = advance + (float) glyph.getDouble("left");
                target.set(gx, 0, gx + w, h);
                canvas.drawBitmap(atlas, source, target, paint);
                advance += (float) glyph.getDouble("advance");
            }
            ranks[label.getInt("rank") - 1] = rank;
        }
        for (Bitmap rank : ranks) if (rank == null) throw new IllegalArgumentException("Incomplete Wii ranks");
        atlas.recycle();
    }

    private static Bitmap load(File folder, String name) {
        if (!name.matches("hud_[a-zA-Z0-9_]+\\.png")) throw new IllegalArgumentException("Unexpected HUD filename");
        Bitmap bitmap = BitmapFactory.decodeFile(new File(folder, name).getPath());
        if (bitmap == null) throw new IllegalArgumentException("Missing Wii HUD asset: " + name);
        return bitmap;
    }
}
