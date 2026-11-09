package dev.thor.mkdd;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Log;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Iterator;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Optional art staged privately from the owner's GM4E01 disc. Every piece is independent:
 * anything missing or malformed stays blank in the renderer, never substituted.
 *
 * duo_hud.json: {"schema":1, "game":"GM4E01",
 *   "faces": {"<ECharID 1..20>": "duo_face_07.png", ...},
 *   "items": {"<item kind 0..21, not 16>": "duo_item_00.png", ...},
 *   "item_variants": {"<kind>@<ECharID>": "duo_item_11_c12.png", ...},
 *   "map_icons": {"<ECharID>": "duo_mapicon_07.png", ...},
 *   "hud":   {"lap"|"time"|"best_lap"|"p1"|"p1_map"|"mirror"|"window"|"logo"|"glyph_<a-z, 0-9, colon, period,
 *             slash, exclamation, question, dash>": "duo_hud_lap.png", ...},
 *   "maps":  {"<ECourseID>": {"png":"duo_map_24.png", "axes":["-x","-z"], "bounds":[minU,minV,maxU,maxV],
 *                             "alignment":"verified"}, ...}}
 * The hud textures are the game's own intensity masks (lettering, portrait window, P1 tags), recoloured
 * at draw time the way the game's J2DPictures do.
 * Axes and bounds come from the retail minimap table (design/stage_art.py). A map is used only
 * once that placement has passed the road check against the course's own line ("verified").
 *
 * Beside the art folder, overview/ may hold design/bake_overview.py's overhead pictures of the
 * courses (overview.json + duo_overview_<id>.png), drawn in the same axes and bounds; one is used
 * only when those match this course's verified map exactly.
 */
final class DuoArt {
    static final String MANIFEST = "duo_hud.json", TAG = "WiiDS-MKDD";
    private static final int PLUS_DS_CHEEP_CHEEP_BEACH = 0x122;
    final Bitmap[] faces = new Bitmap[21], icons = new Bitmap[21], items = new Bitmap[22];
    final HashMap<String, Bitmap> hud = new HashMap<>();
    boolean courseNames;
    /** Holder-specific icons ("kind@ECharID"), e.g. Birdo's egg; indexed kind*21+character. */
    private final Bitmap[] variants = new Bitmap[22*21];

    /** Bounds are in the texture's own axes (see DuoMapAxes), so the art is drawn as the game draws it. */
    static final class CourseMap {
        final Bitmap image;
        /** The course seen from above (bake_overview.py), or null: the minimap line is drawn then. */
        final Bitmap overview;
        /** The mod's own course-name texture, when it provides one. */
        Bitmap title;
        final DuoMapAxes axes;
        final double minU, minV, maxU, maxV;
        /** The drawn (non-transparent) part of the texture, as fractions of its width and height. */
        final double drawnL, drawnT, drawnR, drawnB;
        CourseMap(Bitmap image, Bitmap overview, DuoMapAxes axes, double[] b) {
            this.image=image; this.overview=overview; this.axes=axes; minU=b[0]; minV=b[1]; maxU=b[2]; maxV=b[3];
            int w=image.getWidth(), h=image.getHeight(), l=w, t=h, r=-1, bottom=-1;
            int[] row=new int[w];
            for (int y=0; y < h; y++) {
                image.getPixels(row, 0, w, 0, y, w, 1);
                for (int x=0; x < w; x++) if ((row[x] >>> 24) > 16) {
                    l=Math.min(l, x); r=Math.max(r, x); t=Math.min(t, y); bottom=Math.max(bottom, y);
                }
            }
            boolean any=r >= l && bottom >= t;
            drawnL=any ? l/(double) w : 0; drawnT=any ? t/(double) h : 0;
            drawnR=any ? (r+1)/(double) w : 1; drawnB=any ? (bottom+1)/(double) h : 1;
        }
    }

    private DuoArt() {}

    /** Null when no manifest has been staged yet. Runs off the main thread. */
    static DuoArt load(File folder) {
        JSONObject info = manifest(folder);
        if (info == null) return null;
        DuoArt art = new DuoArt();
        art.courseNames = info.optBoolean("course_names", false);
        decodeAll(folder, info.optJSONObject("faces"), art.faces, 1);
        decodeAll(folder, info.optJSONObject("map_icons"), art.icons, 1);
        decodeAll(folder, info.optJSONObject("items"), art.items, 0);
        JSONObject hud = info.optJSONObject("hud");
        if (hud != null) for (Iterator<String> keys = hud.keys(); keys.hasNext(); ) {
            String key = keys.next();
            Bitmap bitmap = key.matches("[a-z0-9_]+") ? decode(folder, hud.optString(key)) : null;
            if (bitmap != null) art.hud.put(key, bitmap);
        }
        art.items[DuoScene.ITEM_EMPTY] = null;
        JSONObject variants = info.optJSONObject("item_variants");
        if (variants != null) for (Iterator<String> keys = variants.keys(); keys.hasNext(); ) {
            String key = keys.next();
            int at = key.indexOf('@'), kind, character;
            try {
                kind = Integer.parseInt(key.substring(0, Math.max(0, at)));
                character = Integer.parseInt(key.substring(at+1));
            } catch (NumberFormatException e) { continue; }
            if (kind >= 0 && kind < 22 && kind != DuoScene.ITEM_EMPTY && character >= 1 && character <= 20)
                art.variants[kind*21+character] = decode(folder, variants.optString(key));
        }
        return art;
    }

    /** The icon the game shows for this kind in this character's hands, or null. */
    Bitmap item(int kind, int character) {
        if (kind < 0 || kind >= items.length) return null;
        Bitmap variant = character >= 1 && character <= 20 ? variants[kind*21+character] : null;
        return variant != null ? variant : items[kind];
    }

    /** Null unless this course has a verified own-disc map. Runs off the main thread. */
    static CourseMap loadMap(File folder, int courseId) {
        JSONObject info = manifest(folder), maps = info == null ? null : info.optJSONObject("maps");
        JSONObject entry = maps == null ? null : maps.optJSONObject(Integer.toString(courseId));
        if (entry == null || !"verified".equals(entry.optString("alignment"))) return null;
        JSONArray bounds = entry.optJSONArray("bounds");
        if (bounds == null || bounds.length() != 4) return null;
        double[] b = new double[4];
        for (int i = 0; i < 4; i++) {
            b[i] = bounds.optDouble(i);
            if (!Double.isFinite(b[i])) return null;
        }
        if (b[2] <= b[0] || b[3] <= b[1]) return null;
        JSONArray names = entry.optJSONArray("axes");
        DuoMapAxes axes = names == null || names.length() != 2 ? null
                : DuoMapAxes.parse(names.optString(0), names.optString(1));
        if (axes == null) return null;
        Bitmap image = decode(folder, entry.optString("png"));
        if (image == null) return null;
        CourseMap map = new CourseMap(image, overview(folder, courseId, names, b), axes, b);
        if (entry.has("name_png")) map.title = decode(folder, entry.optString("name_png"));
        return map;
    }

    /** The overhead picture baked in exactly this placement, or null. */
    private static Bitmap overview(File folder, int courseId, JSONArray axes, double[] bounds) {
        // The bake cannot reproduce this course's water and terrain materials. Use its authored minimap.
        if (courseId == PLUS_DS_CHEEP_CHEEP_BEACH) return null;
        File dir = new File(folder, "overview");
        if (!new File(dir, "overview.json").isFile()) dir = new File(folder.getParentFile(), "overview");
        File file = new File(dir, "overview.json");
        if (!file.isFile()) return null;
        try {
            JSONObject info = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
            if (info.optInt("schema") != 1 || !"GM4E01".equals(info.optString("game"))) return null;
            JSONObject all = info.optJSONObject("overviews");
            JSONObject e = all == null ? null : all.optJSONObject(Integer.toString(courseId));
            JSONArray a = e == null ? null : e.optJSONArray("axes"), b = e == null ? null : e.optJSONArray("bounds");
            if (a == null || b == null || a.length() != 2 || b.length() != 4
                    || !a.optString(0).equals(axes.optString(0)) || !a.optString(1).equals(axes.optString(1))) return null;
            for (int i = 0; i < 4; i++) if (Math.abs(b.optDouble(i) - bounds[i]) > 1e-3) return null;
            String name = e.optString("png");
            return name.matches("duo_overview_[0-9]+\\.png") ? BitmapFactory.decodeFile(new File(dir, name).getPath()) : null;
        } catch (Exception ex) {
            Log.w(TAG, "Unreadable course overview for " + courseId, ex); return null;
        }
    }

    private static JSONObject manifest(File folder) {
        File file = new File(folder, MANIFEST);
        if (!file.isFile()) return null;
        try {
            JSONObject info = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
            if (info.optInt("schema") != 1 || !"GM4E01".equals(info.optString("game"))) {
                Log.w(TAG, "Ignoring lower-screen art for another schema/game"); return null;
            }
            return info;
        } catch (Exception e) {
            Log.w(TAG, "Unreadable lower-screen art manifest", e); return null;
        }
    }

    private static void decodeAll(File folder, JSONObject names, Bitmap[] into, int first) {
        if (names == null) return;
        for (Iterator<String> keys = names.keys(); keys.hasNext(); ) {
            String key = keys.next();
            int id;
            try { id = Integer.parseInt(key); } catch (NumberFormatException e) { continue; }
            if (id >= first && id < into.length) into[id] = decode(folder, names.optString(key));
        }
    }

    private static Bitmap decode(File folder, String name) {
        if (name == null || !name.matches("duo_[a-z0-9_]+\\.png")) return null;
        Bitmap bitmap = BitmapFactory.decodeFile(new File(folder, name).getPath());
        if (bitmap == null) Log.w(TAG, "Missing lower-screen art " + name);
        return bitmap;
    }
}
