package dev.thor.galaxy;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * One of Super Mario Galaxy's own fonts (NW4R RFNT from the owner's Font.arc, extracted by
 * experiments/galaxy-thor/Extract-LowerFonts.py into glyph sheets plus metrics), drawn glyph by
 * glyph: each code point's cell from its sheet, at pen + left, advancing by its advance.
 * Intensity fonts (messagefont26) are white coverage and take the paint's colour filter;
 * colour fonts (numberfont, picturefont, menufont64's outlined letters) draw as they are.
 */
final class GameFont {
    private static final String TAG = "GalaxyLower";
    final String name;
    final int baseline, lineFeed, cellHeight;
    private final Bitmap[] sheets;
    // code point -> {sheet, x, y, w, h, left, advance}
    private final Map<Integer, int[]> glyphs = new HashMap<>();
    private final Rect src = new Rect();
    private final RectF dst = new RectF();

    private GameFont(String name, int baseline, int lineFeed, int cellHeight, Bitmap[] sheets) {
        this.name = name; this.baseline = baseline; this.lineFeed = lineFeed; this.cellHeight = cellHeight; this.sheets = sheets;
    }

    /** Loads [name].json and its sheets from [folder]; null when they are not staged. */
    static GameFont load(File folder, String name) {
        try {
            File json = new File(folder, name + ".json");
            if (!json.isFile()) return null;
            byte[] bytes = new byte[(int) json.length()];
            InputStream in = new FileInputStream(json);
            try { int n = 0; while (n < bytes.length) { int r = in.read(bytes, n, bytes.length - n); if (r < 0) break; n += r; } }
            finally { in.close(); }
            JSONObject root = new JSONObject(new String(bytes, "UTF-8"));
            JSONArray files = root.getJSONObject("sheet").getJSONArray("files");
            Bitmap[] sheets = new Bitmap[files.length()];
            for (int i = 0; i < sheets.length; i++) {
                sheets[i] = BitmapFactory.decodeFile(new File(folder, files.getString(i)).getPath());
                if (sheets[i] == null) return null;
            }
            GameFont font = new GameFont(name, root.getInt("baseline"), root.getInt("lineFeed"), root.getInt("cellHeight"), sheets);
            JSONObject glyphs = root.getJSONObject("glyphs");
            Iterator<String> keys = glyphs.keys();
            while (keys.hasNext()) {
                String key = keys.next();
                JSONObject g = glyphs.getJSONObject(key);
                font.glyphs.put(Integer.parseInt(key), new int[]{g.getInt("sheet"), g.getInt("x"), g.getInt("y"), g.getInt("w"),
                    g.getInt("h"), g.getInt("left"), g.getInt("advance")});
            }
            return font;
        } catch (Exception failure) {
            Log.w(TAG, "font " + name + " unavailable", failure);
            return null;
        }
    }

    boolean covers(String text) {
        for (int i = 0; i < text.length(); i++) if (!glyphs.containsKey((int) text.charAt(i))) return false;
        return true;
    }

    /** Width of [text] at [scale] with [spacing] (font pixels) between characters. */
    float width(String text, float scale, float spacing) {
        float w = 0;
        for (int i = 0; i < text.length(); i++) {
            int[] g = glyphs.get((int) text.charAt(i));
            w += ((g != null ? g[6] : cellHeight / 3) + (i + 1 < text.length() ? spacing : 0)) * scale;
        }
        return w;
    }

    /** Draws [text] with its baseline at [y], starting at [x]; returns the pen's end x. */
    float draw(Canvas c, String text, float x, float y, float scale, float spacing, Paint paint) {
        float pen = x, top = y - baseline * scale;
        for (int i = 0; i < text.length(); i++) {
            int[] g = glyphs.get((int) text.charAt(i));
            if (g == null) { pen += (cellHeight / 3 + spacing) * scale; continue; }
            if (g[3] > 0 && g[4] > 0 && g[0] < sheets.length) {
                src.set(g[1], g[2], g[1] + g[3], g[2] + g[4]);
                dst.set(pen + g[5] * scale, top, pen + (g[5] + g[3]) * scale, top + g[4] * scale);
                c.drawBitmap(sheets[g[0]], src, dst, paint);
            }
            pen += (g[6] + spacing) * scale;
        }
        return pen;
    }

    /** Draws [text] centred on [cx], shrunk to fit [maxWidth]; returns the scale used. */
    float drawCentered(Canvas c, String text, float cx, float y, float scale, float spacing, float maxWidth, Paint paint) {
        float w = width(text, scale, spacing);
        if (w > maxWidth && w > 0) { scale *= maxWidth / w; w = maxWidth; }
        draw(c, text, cx - w / 2, y, scale, spacing, paint);
        return scale;
    }
}
