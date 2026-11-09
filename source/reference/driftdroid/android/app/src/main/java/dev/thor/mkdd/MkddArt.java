package dev.thor.mkdd;

import android.graphics.Bitmap;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Builds the duo screen's art (DuoArt's duo_hud.json + PNGs) on the device from the player's own
 * imported GM4E01 image: the same output as design/stage_art.py. Nothing is shipped.
 *
 * Map placement is the retail minimap table read from the disc's executable. The importer accepts
 * only the exact disc design/stage_art.py checked course by course against each course's own
 * line, so those verdicts (VERIFIED) are applied here unchanged.
 */
public final class MkddArt {
    private MkddArt() {}
    static final String MARKER = ".duo-art-4";
    public interface Progress { void update(int done, int total, String course) throws IOException; }

    private static final String[] FACES = {"baby_mario", "baby_luigi", "pata", "noko", "peach", "daisy", "mario",
        "luigi", "wario", "waluigi", "yoshi", "catherine", "donky", "diddy", "koopa", "koopa_jr", "kinopio",
        "kinopico", "teresa", "pakkun"};
    private static final String[] MAP_ICONS = {"babymario", "babyluigi", "patapata", "nokonoko", "peach", "daisy",
        "mario", "luigi", "wario", "waluigi", "yoshi", "catherine", "donkeykong", "diddykong", "koopa", "koopajr",
        "kinopio", "kinopico", "teresa", "pakkun"};
    private static final int[] ITEM_KINDS = {0, 2, 3, 4, 5, 7, 8, 11, 12, 13, 15, 17, 18, 19, 21};
    private static final String[] ITEM_ART = {"item_kame_green", "item_kame_red", "item_banana", "item_big_banana",
        "item_kinoko", "item_wanwan", "item_bomb_hei", "item_egg", "item_goldenkinoko", "item_kame_wing",
        "item_dummybox", "item_kame_green_3", "item_kinoko_3", "item_kame_red_3", "item_fire_ball"};
    /** The game's own HUD lettering and frames: manifest key, archive ("race2d" or English "loc"), texture. */
    static final String[][] HUD = hud();
    private static String[][] hud() {
        String[][] fixed = {{"lap", "loc", "im_lap1_1"}, {"time", "loc", "im_time1_3"}, {"best_lap", "loc", "m_best_lap"},
            {"p1", "loc", "playernumbersimple_1p"}, {"p1_map", "loc", "map_icon_player1"}, {"mirror", "loc", "power_mozi.4"},
            {"window", "race2d", "chara_window2"}, {"logo", "race2d", "e3_mariokart_copy3"},
            {"glyph_colon", "race2d", "mariofontcolon"}, {"glyph_period", "race2d", "mariofontperiod"},
            {"glyph_slash", "race2d", "mariofontslash"}, {"glyph_exclamation", "race2d", "mariofontexclamation"},
            {"glyph_question", "race2d", "mariofontquestion"}, {"glyph_dash", "race2d", "mariofont_number_xline"}};
        String[][] out = new String[fixed.length + 36][];
        System.arraycopy(fixed, 0, out, 0, fixed.length);
        for (int i = 0; i < 26; i++) {
            String c = String.valueOf((char) ('a' + i));
            out[fixed.length + i] = new String[]{"glyph_" + c, "race2d", "mariofont_" + c};
        }
        for (int i = 0; i < 10; i++)
            out[fixed.length + 26 + i] = new String[]{"glyph_" + i, "race2d", "mariofont_number_" + i};
        return out;
    }
    // ECourseID, course archive, minimap corner table, orientation `li r0` (retail US; stage_art.py).
    private static final Object[][] COURSES = {
        {0x21, "BabyLuigi", 0x803CDDECL, 0x80141E14L}, {0x22, "Peach", 0x803CDE0CL, 0x80141EE0L},
        {0x23, "Daisy", 0x803CDE34L, 0x8014200CL}, {0x24, "Luigi", 0x803CDE5CL, 0x801420D8L},
        {0x25, "Mario", 0x803CDE74L, 0x801421A4L}, {0x26, "Yoshi", 0x803CDE90L, 0x80142270L},
        {0x28, "Nokonoko", 0x803CDEB0L, 0x8014233CL}, {0x29, "Patapata", 0x803CDEC8L, 0x80142408L},
        {0x2a, "Waluigi", 0x803CDEE8L, 0x801424D4L}, {0x2b, "Wario", 0x803CDF0CL, 0x801425A0L},
        {0x2c, "Diddy", 0x803CDF28L, 0x8014266CL}, {0x2d, "Donkey", 0x803CDF40L, 0x80142738L},
        {0x2f, "Koopa", 0x803CDF5CL, 0x80142804L}, {0x31, "Rainbow", 0x803CDF70L, 0x801428D0L},
        {0x32, "Desert", 0x803CDF84L, 0x8014299CL}, {0x33, "Snow", 0x803CDFA4L, 0x80142A68L}};
    // All sixteen COURSES passed the automatic check or a recorded course-model review in
    // stage_art.py. Peach's shoreline art is not the racing centre line across its wide beach.
    private static final String[][] AXES = {{"x", "z"}, {"-z", "x"}, {"-x", "-z"}, {"z", "-x"}};

    public static boolean ready(File art) {
        if (!new File(art, MARKER).isFile()) return false;
        try {
            JSONObject info = new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(art, DuoArt.MANIFEST).toPath()), StandardCharsets.UTF_8));
            for (String group : new String[]{"faces", "map_icons", "items", "item_variants", "hud"}) {
                JSONObject entries = info.getJSONObject(group);
                for (java.util.Iterator<String> keys = entries.keys(); keys.hasNext(); )
                    if (!present(art, entries.getString(keys.next()))) return false;
            }
            JSONObject maps = info.getJSONObject("maps");
            JSONObject overviews = new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(art, "overview/overview.json").toPath()), StandardCharsets.UTF_8)).getJSONObject("overviews");
            int expected = info.optBoolean("course_names", false) ? 80 : 16;
            if (maps.length() != expected || overviews.length() != expected) return false;
            for (java.util.Iterator<String> keys = maps.keys(); keys.hasNext(); ) {
                String key = keys.next(); JSONObject e = maps.getJSONObject(key);
                if (!present(art, e.getString("png")) || (e.has("name_png") && !present(art, e.getString("name_png")))
                        || !present(new File(art, "overview"), overviews.getJSONObject(key).getString("png"))) return false;
            }
            return true;
        } catch (Exception e) { return false; }
    }

    private static boolean present(File folder, String name) {
        return name.matches("[a-zA-Z0-9_.-]+\\.png") && new File(folder, name).isFile() && new File(folder, name).length() > 0;
    }

    /** Blocking. Builds into a sibling folder and swaps it in; returns null or the reason. */
    public static String build(File iso, File art) {
        return build(iso, art, false, null);
    }

    /** All generated maps live inside the art transaction; original art is retained on any failure. */
    public static synchronized String build(File iso, File art, boolean plus, Progress progress) {
        File temp = new File(art.getParentFile(), art.getName() + ".building");
        File previous = new File(art.getParentFile(), art.getName() + ".previous");
        if (previous.exists()) {
            if (ready(art)) deleteTree(previous);
            else {
                deleteTree(art);
                if (!previous.renameTo(art)) return "The previous lower-screen art couldn't be recovered.";
            }
        }
        deleteTree(temp);
        if (!temp.mkdirs()) return "The lower-screen art folder couldn't be created.";
        try (RandomAccessFile disc = new RandomAccessFile(iso, "r")) {
            Map<String, long[]> files = fileTable(disc);
            JSONObject manifest = new JSONObject().put("schema", 1).put("game", "GM4E01");
            Map<String, byte[]> race2d = rarc(yaz0(rarc(yaz0(read(disc, files, "/MRAM.arc"))).get("race2d.arc")));
            JSONObject faces = new JSONObject(), icons = new JSONObject(), items = new JSONObject(), variants = new JSONObject();
            for (int i = 0; i < FACES.length; i++) {
                faces.put(Integer.toString(i+1), save(temp, String.format(Locale.ROOT, "duo_face_%02d.png", i+1), need(race2d, FACES[i]+".bti")));
                icons.put(Integer.toString(i+1), save(temp, String.format(Locale.ROOT, "duo_mapicon_%02d.png", i+1), need(race2d, MAP_ICONS[i]+"_mapicon32.bti")));
            }
            for (int i = 0; i < ITEM_KINDS.length; i++)
                items.put(Integer.toString(ITEM_KINDS[i]), save(temp, String.format(Locale.ROOT, "duo_item_%02d.png", ITEM_KINDS[i]), need(race2d, ITEM_ART[i]+".bti")));
            variants.put("11@12", save(temp, "duo_item_11_c12.png", need(race2d, "item_egg_catherine.bti")));
            variants.put("21@8", save(temp, "duo_item_21_c08.png", need(race2d, "item_fire_ball_luigi.bti")));
            manifest.put("faces", faces).put("map_icons", icons).put("items", items).put("item_variants", variants);
            Map<String, byte[]> loc = rarc(yaz0(read(disc, files, "/MRAM_Locale/English/MRAMLoc.arc")));
            JSONObject hud = new JSONObject();
            for (String[] h : HUD)
                hud.put(h[0], save(temp, "duo_hud_" + h[0] + ".png", need("loc".equals(h[1]) ? loc : race2d, h[2] + ".bti")));
            manifest.put("hud", hud);
            byte[] dol = dol(disc);
            JSONObject maps = new JSONObject(), overviews = new JSONObject();
            File overview = new File(temp, "overview");
            if (!overview.mkdir()) throw new IOException("The overview folder couldn't be created");
            int total = plus ? 80 : 16, done = 0;
            if (plus) manifest.put("course_names", true);
            for (int page = 0; page < (plus ? 5 : 1); page++) for (Object[] c : COURSES) {
                int id = (Integer) c[0];
                int key = page * 256 + id;
                String arc = (String) c[1];
                if (progress != null) progress.update(done, total, arc);
                Map<String, byte[]> course = rarc(yaz0(read(disc, files, (plus ? "/Cours" + page : "/Course") + "/" + arc + ".arc")));
                byte[] bol = need(course, arc.toLowerCase(Locale.ROOT) + "_course.bol");
                if ((bol[0x19] & 255) != id) throw new IOException(arc + " is not course " + id);
                JSONObject entry = new JSONObject().put("png",
                        save(temp, String.format(Locale.ROOT, "duo_map_%02d.png", key), need(course, arc.toLowerCase(Locale.ROOT) + "_map.bti")));
                int at = dolOffset(dol, (Long) c[2]), code = dolOffset(dol, (Long) c[3]);
                float x1 = Float.intBitsToFloat(u32(dol, at)), z1 = Float.intBitsToFloat(u32(dol, at+4));
                float x2 = Float.intBitsToFloat(u32(dol, at+8)), z2 = Float.intBitsToFloat(u32(dol, at+12));
                int op = u32(dol, code);
                int orientation = op & 0xffff;
                if (plus) {
                    x1 = plusFloat(dol, page, (Long) c[2]); z1 = plusFloat(dol, page, (Long) c[2] + 4);
                    x2 = plusFloat(dol, page, (Long) c[2] + 8); z2 = plusFloat(dol, page, (Long) c[2] + 12);
                    orientation = plusOrientation(dol, page, (Long) c[3] + 3);
                }
                if (orientation > 3 || (!plus && (op >>> 16) != 0x3800)
                        || !Float.isFinite(x1) || !Float.isFinite(z1) || !Float.isFinite(x2) || !Float.isFinite(z2)
                        || x1 == x2 || z1 == z2 || (!plus && !(x1 < x2 && z1 < z2)))
                    throw new IOException(arc + ": unexpected minimap data");
                String[] axes = AXES[orientation].clone();
                double[] bounds = {low(axes[0], x1, z1, x2, z2), low(axes[1], x1, z1, x2, z2),
                        high(axes[0], x1, z1, x2, z2), high(axes[1], x1, z1, x2, z2)};
                for (int i = 0; i < 2; i++) if (bounds[i+2] < bounds[i]) {
                    axes[i] = axes[i].startsWith("-") ? axes[i].substring(1) : "-" + axes[i];
                    bounds[i] = -bounds[i]; bounds[i+2] = -bounds[i+2];
                }
                entry.put("axes", new JSONArray().put(axes[0]).put(axes[1]));
                JSONArray placement = new JSONArray(); for (double bound : bounds) placement.put(bound);
                entry.put("bounds", placement);
                entry.put("alignment", "verified");
                if (plus) entry.put("name_png", save(temp, "duo_name_" + key + ".png", read(disc, files, "/CourseNam" + page + "/English/" + arc + "_name.bti")));
                maps.put(Integer.toString(key), entry);
                String name = "duo_overview_" + key + ".png";
                Bitmap picture = MkddOverview.bake(need(course, arc.toLowerCase(Locale.ROOT) + "_course.bmd"), bol, axes, bounds);
                int width = picture.getWidth(), height = picture.getHeight();
                try (FileOutputStream output = new FileOutputStream(new File(overview, name))) {
                    if (!picture.compress(Bitmap.CompressFormat.PNG, 100, output)) throw new IOException("Overview write failed");
                    output.getFD().sync();
                } finally { picture.recycle(); }
                overviews.put(Integer.toString(key), new JSONObject().put("png", name).put("axes", entry.getJSONArray("axes"))
                        .put("bounds", placement).put("size", new JSONArray().put(width).put(height)));
                done++;
                if (progress != null) progress.update(done, total, arc);
            }
            manifest.put("maps", maps);
            write(new File(overview, "overview.json"), new JSONObject().put("schema", 1).put("game", "GM4E01").put("overviews", overviews).toString(1).getBytes(StandardCharsets.UTF_8));
            write(new File(temp, DuoArt.MANIFEST), manifest.toString(1).getBytes(StandardCharsets.UTF_8));
            write(new File(temp, MARKER), new byte[]{'1'});
        } catch (Exception e) {
            deleteTree(temp);
            return "The Double Dash lower-screen art couldn't be built: " + e.getMessage();
        }
        if (art.exists() && !art.renameTo(previous)) { deleteTree(temp); return "The previous lower-screen art couldn't be retained."; }
        if (!temp.renameTo(art)) {
            boolean recovered = !previous.exists() || previous.renameTo(art);
            deleteTree(temp);
            return recovered ? "The lower-screen art couldn't be moved into place." : "The previous art is retained for recovery. Open setup again.";
        }
        deleteTree(previous);
        return null;
    }

    private static float plusFloat(byte[] dol, int page, long address) throws IOException {
        for (int i = 0; i < 88; i++) if ((u32(dol, dolOffset(dol, 0x803e294cL + i*4)) & 0xffffffffL) == address)
            return Float.intBitsToFloat(u32(dol, dolOffset(dol, 0x803e2b04L + page*0x160 + i*4)));
        throw new IOException("Missing Plus projection address");
    }
    private static int plusOrientation(byte[] dol, int page, long address) throws IOException {
        for (int i = 0; i < 22; i++) if ((u32(dol, dolOffset(dol, 0x803e2aacL + i*4)) & 0xffffffffL) == address)
            return dol[dolOffset(dol, 0x803e31e4L + page*22 + i)] & 255;
        throw new IOException("Missing Plus orientation address");
    }

    private static double low(String axis, float x1, float z1, float x2, float z2) {
        switch (axis) { case "x": return x1; case "z": return z1; case "-x": return -x2; default: return -z2; }
    }
    private static double high(String axis, float x1, float z1, float x2, float z2) {
        switch (axis) { case "x": return x2; case "z": return z2; case "-x": return -x1; default: return -z1; }
    }

    // ---- disc ----

    private static Map<String, long[]> fileTable(RandomAccessFile disc) throws IOException {
        disc.seek(0x424);
        long off = disc.readInt() & 0xffffffffL, size = disc.readInt() & 0xffffffffL;
        if (size > 1 << 22) throw new IOException("Unexpected file table");
        byte[] t = new byte[(int) size];
        disc.seek(off); disc.readFully(t);
        int count = u32(t, 8);
        int names = count * 12;
        Map<String, long[]> out = new HashMap<>();
        int[] ends = new int[64];
        String[] dirs = new String[64];
        int depth = 0;
        ends[0] = count; dirs[0] = "";
        for (int i = 1; i < count; i++) {
            while (depth > 0 && i >= ends[depth]) depth--;
            int word = u32(t, i*12), a = u32(t, i*12+4), b = u32(t, i*12+8);
            int start = names + (word & 0xffffff), end = start;
            while (t[end] != 0) end++;
            String name = dirs[depth] + "/" + new String(t, start, end - start, StandardCharsets.ISO_8859_1);
            if ((word >>> 24) != 0) { depth++; ends[depth] = b; dirs[depth] = name; }
            else out.put(name, new long[]{a & 0xffffffffL, b & 0xffffffffL});
        }
        return out;
    }

    private static byte[] read(RandomAccessFile disc, Map<String, long[]> files, String path) throws IOException {
        long[] f = files.get(path);
        if (f == null || f[1] > 64L << 20) throw new IOException("Missing " + path);
        byte[] data = new byte[(int) f[1]];
        disc.seek(f[0]); disc.readFully(data);
        return data;
    }

    private static byte[] dol(RandomAccessFile disc) throws IOException {
        disc.seek(0x420);
        long at = disc.readInt() & 0xffffffffL;
        byte[] header = new byte[0x100];
        disc.seek(at); disc.readFully(header);
        int size = 0;
        for (int i = 0; i < 18; i++) {
            int o = u32(header, i*4), s = u32(header, 0x90 + i*4);
            if (s != 0) size = Math.max(size, o + s);
        }
        if (size <= 0 || size > 16 << 20) throw new IOException("Unexpected executable");
        byte[] dol = new byte[size];
        disc.seek(at); disc.readFully(dol);
        return dol;
    }

    private static int dolOffset(byte[] dol, long va) throws IOException {
        for (int i = 0; i < 18; i++) {
            long o = u32(dol, i*4) & 0xffffffffL, a = u32(dol, 0x48 + i*4) & 0xffffffffL, s = u32(dol, 0x90 + i*4) & 0xffffffffL;
            if (s != 0 && va >= a && va < a + s) return (int) (o + va - a);
        }
        throw new IOException(String.format(Locale.ROOT, "%08x is outside the executable", va));
    }

    static byte[] yaz0(byte[] d) throws IOException {
        if (d.length < 16 || d[0] != 'Y' || d[1] != 'a' || d[2] != 'z' || d[3] != '0') return d;
        int size = u32(d, 4);
        if (size < 0 || size > 64 << 20) throw new IOException("Unexpected archive size");
        byte[] out = new byte[size];
        int i = 16, o = 0;
        while (o < size) {
            int code = d[i++] & 255;
            for (int bit = 0; bit < 8 && o < size; bit++) {
                if ((code & (0x80 >> bit)) != 0) { out[o++] = d[i++]; continue; }
                int b1 = d[i++] & 255, b2 = d[i++] & 255;
                int dist = ((b1 & 15) << 8 | b2) + 1, n = b1 >> 4;
                n = n == 0 ? (d[i++] & 255) + 0x12 : n + 2;
                for (int k = 0; k < n && o < size; k++) { out[o] = out[o - dist]; o++; }
            }
        }
        return out;
    }

    /** Lower-case file name to contents, for every file in a RARC archive. */
    static Map<String, byte[]> rarc(byte[] d) throws IOException {
        if (d == null || d.length < 0x40 || d[0] != 'R' || d[1] != 'A' || d[2] != 'R' || d[3] != 'C')
            throw new IOException("Not an archive");
        int data = u32(d, 0xc) + 0x20, count = u32(d, 0x28), entries = u32(d, 0x2c) + 0x20, strings = u32(d, 0x34) + 0x20;
        Map<String, byte[]> out = new HashMap<>();
        for (int e = 0; e < count; e++) {
            int at = entries + e*20;
            int kind = u16(d, at + 4), name = u16(d, at + 6), a = u32(d, at + 8), b = u32(d, at + 12);
            if ((kind & 0x0200) != 0) continue;
            int start = strings + name, end = start;
            while (d[end] != 0) end++;
            byte[] blob = new byte[b];
            System.arraycopy(d, data + a, blob, 0, b);
            out.put(new String(d, start, end - start, StandardCharsets.ISO_8859_1).toLowerCase(Locale.ROOT), blob);
        }
        return out;
    }

    // ---- textures ----

    private static String save(File folder, String name, byte[] bti) throws IOException {
        Bitmap bitmap = bti(bti);
        try (FileOutputStream out = new FileOutputStream(new File(folder, name))) {
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)) throw new IOException("PNG write failed");
            out.getFD().sync();
        } finally { bitmap.recycle(); }
        return name;
    }

    /** Decodes a BTI texture (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CMPR). */
    static Bitmap bti(byte[] b) throws IOException {
        return Bitmap.createBitmap(btiPixels(b), u16(b, 2), u16(b, 4), Bitmap.Config.ARGB_8888);
    }

    /** Raw unassociated channels: Android Bitmap discards RGB under zero alpha. */
    static int[] btiPixels(byte[] b) throws IOException {
        if (b.length < 32) throw new IOException("Truncated texture header");
        int format = b[0] & 255, w = u16(b, 2), h = u16(b, 4), base = u32(b, 0x1c);
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) throw new IOException("Unexpected texture size");
        int[] px = new int[w * h];
        int bw, bh;
        switch (format) {
            case 0: case 14: bw = 8; bh = 8; break;
            case 1: case 2: bw = 8; bh = 4; break;
            case 3: case 4: case 5: case 6: bw = 4; bh = 4; break;
            default: throw new IOException("Unsupported texture format " + format);
        }
        int p = base;
        for (int by = 0; by < h; by += bh) for (int bx = 0; bx < w; bx += bw) {
            if (format == 14) {
                for (int sub = 0; sub < 4; sub++) {
                    int c0 = u16(b, p), c1 = u16(b, p+2), bits = u32(b, p+4);
                    p += 8;
                    int[] pal = new int[4];
                    pal[0] = rgb565(c0); pal[1] = rgb565(c1);
                    if (c0 > c1) { pal[2] = mix(pal[0], pal[1], 2, 1, 3); pal[3] = mix(pal[0], pal[1], 1, 2, 3); }
                    else { pal[2] = mix(pal[0], pal[1], 1, 1, 2); pal[3] = 0; }
                    int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
                    for (int i = 0; i < 16; i++) put(px, w, h, sx + i % 4, sy + i / 4, pal[(bits >>> (30 - 2*i)) & 3]);
                }
            } else if (format == 6) {
                for (int i = 0; i < 16; i++) {
                    int a = b[p + i*2] & 255, r = b[p + i*2 + 1] & 255, g = b[p + 32 + i*2] & 255, bl = b[p + 33 + i*2] & 255;
                    put(px, w, h, bx + i % 4, by + i / 4, a << 24 | r << 16 | g << 8 | bl);
                }
                p += 64;
            } else {
                for (int i = 0; i < bw * bh; i++) {
                    int x = bx + i % bw, y = by + i / bw, c;
                    switch (format) {
                        case 0: { int v = ((i & 1) == 0 ? (b[p + i/2] >> 4) : b[p + i/2]) & 15; v *= 17; c = v << 24 | v * 0x10101; break; }
                        case 1: { int v = b[p + i] & 255; c = v << 24 | v * 0x10101; break; }
                        case 2: { int v = b[p + i] & 255, g = (v & 15) * 17; c = (v >> 4) * 17 << 24 | g * 0x10101; break; }
                        case 3: { int a = b[p + i*2] & 255, g = b[p + i*2 + 1] & 255; c = a << 24 | g * 0x10101; break; }
                        case 4: c = rgb565(u16(b, p + i*2)); break;
                        default: c = rgb5a3(u16(b, p + i*2)); break;
                    }
                    put(px, w, h, x, y, c);
                }
                p += 32;
            }
        }
        return px;
    }

    private static void put(int[] px, int w, int h, int x, int y, int c) { if (x < w && y < h) px[y*w + x] = c; }
    private static int rgb565(int v) {
        return 0xff000000 | ((v >> 11) & 31) * 255 / 31 << 16 | ((v >> 5) & 63) * 255 / 63 << 8 | (v & 31) * 255 / 31;
    }
    private static int rgb5a3(int v) {
        if ((v & 0x8000) != 0)
            return 0xff000000 | ((v >> 10) & 31) * 255 / 31 << 16 | ((v >> 5) & 31) * 255 / 31 << 8 | (v & 31) * 255 / 31;
        return ((v >> 12) & 7) * 255 / 7 << 24 | ((v >> 8) & 15) * 17 << 16 | ((v >> 4) & 15) * 17 << 8 | (v & 15) * 17;
    }
    private static int mix(int a, int b, int wa, int wb, int div) {
        int r = (((a >> 16) & 255) * wa + ((b >> 16) & 255) * wb) / div;
        int g = (((a >> 8) & 255) * wa + ((b >> 8) & 255) * wb) / div;
        int bl = ((a & 255) * wa + (b & 255) * wb) / div;
        return 0xff000000 | r << 16 | g << 8 | bl;
    }

    // ---- helpers ----

    private static byte[] need(Map<String, byte[]> files, String name) throws IOException {
        byte[] data = files.get(name);
        if (data == null) throw new IOException("Missing " + name);
        return data;
    }
    static int u16(byte[] d, int at) { return (d[at] & 255) << 8 | d[at+1] & 255; }
    static int u32(byte[] d, int at) { return (d[at] & 255) << 24 | (d[at+1] & 255) << 16 | (d[at+2] & 255) << 8 | d[at+3] & 255; }
    private static void write(File file, byte[] bytes) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) { out.write(bytes); out.getFD().sync(); }
    }
    static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) for (File c : children) deleteTree(c);
        f.delete();
    }
}
