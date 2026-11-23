package dev.wiids.cityfolk;

import android.graphics.Bitmap;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Reads original artwork from the player's own extracted Animal Crossing: City Folk disc folder.
 * Nothing is bundled with the app. Every lookup returns null (or an empty list) when the art is
 * missing or malformed.
 *
 * <p>Containers: uncompressed U8 archives ({@code Layout/*.arc}, {@code Banner/banner.arc}) holding
 * TPL textures, BRLYT layouts and BRLAN animations. Texture formats: I4, I8, IA4, IA8, RGB565,
 * RGB5A3, CI4, CI8, CI14X2 (RGB565 or RGB5A3 palettes) and CMPR. These are all the formats the
 * PAL disc's layout archives use; anything else is refused.
 */
public final class CityFolkArt {
    private static final int MAX_ARCHIVE = 16 * 1024 * 1024;
    private static final int MAX_SIDE = 1024;
    private static final int MAX_NODES = 4096;
    private static final Archive MISSING = new Archive(new byte[0], new LinkedHashMap<String, int[]>());

    private final File files;
    private final Map<String, Archive> archives = new HashMap<>();
    private final Map<String, Bitmap> bitmaps = new HashMap<>();

    private CityFolkArt(File files) {
        this.files = files;
    }

    /** {@code discDirectory} is the folder that contains {@code files/} and {@code sys/}. Never throws. */
    public static CityFolkArt open(File discDirectory) {
        try {
            File files = discDirectory == null ? null : new File(discDirectory, "files");
            return new CityFolkArt(files != null && files.isDirectory() ? files : null);
        } catch (RuntimeException unavailable) {
            return new CityFolkArt(null);
        }
    }

    /**
     * Decodes one texture, for example {@code texture("Layout/map.arc", "player_icon_00")}. The
     * name is the TPL's file name with or without {@code .tpl}; {@code name#2} selects the third
     * image of a TPL that holds several (only the banner's icon.tpl does).
     */
    public synchronized Bitmap texture(String archive, String name) {
        if (archive == null || name == null) return null;
        String key = archive + '\n' + name;
        if (bitmaps.containsKey(key)) return bitmaps.get(key);
        Bitmap bitmap = null;
        try {
            Archive loaded = archive(archive);
            int image = 0;
            String file = name;
            int mark = name.lastIndexOf('#');
            if (mark > 0) {
                image = Integer.parseInt(name.substring(mark + 1));
                file = name.substring(0, mark);
            }
            int[] span = loaded.files.get(file.endsWith(".tpl") ? file : file + ".tpl");
            if (span != null && image >= 0) bitmap = decodeTpl(loaded.data, span[0], span[1], image);
        } catch (RuntimeException malformed) {
            bitmap = null;
        }
        bitmaps.put(key, bitmap);
        return bitmap;
    }

    /** The first 16 palette colours of a colour-indexed texture, or null. */
    public synchronized int[] palette(String archive, String name) {
        try {
            Archive loaded = archive(archive);
            int[] span = loaded.files.get(name.endsWith(".tpl") ? name : name + ".tpl");
            if (span == null) return null;
            ByteBuffer tpl = ByteBuffer.wrap(loaded.data, span[0], span[1]).slice();
            if (span[1] < 20 || tpl.getInt(0) != 0x0020af30 || tpl.getInt(4) < 1) return null;
            int table = tpl.getInt(8);
            if (!contains(span[1], table, 8)) return null;
            int header = tpl.getInt(table + 4);
            if (header == 0 || !contains(span[1], header, 12)) return null;
            int entries = tpl.getShort(header) & 0xffff, format = tpl.getInt(header + 4), data = tpl.getInt(header + 8);
            if ((format != 1 && format != 2) || !contains(span[1], data, 2L * entries)) return null;
            int[] colours = new int[16];
            for (int i = 0; i < entries && i < 16; i++) {
                int sample = tpl.getShort(data + 2 * i) & 0xffff;
                colours[i] = format == 1 ? rgb565(sample) : rgb5a3(sample);
            }
            return colours;
        } catch (RuntimeException malformed) {
            return null;
        }
    }

    /** Texture names in an archive, without the {@code .tpl} extension, in archive order. */
    public synchronized List<String> names(String archive) {
        return list(archive, ".tpl", true);
    }

    /** Layout file names in an archive, for example {@code map_win_00.brlyt}. */
    public synchronized List<String> layouts(String archive) {
        return list(archive, ".brlyt", false);
    }

    /** Parses a BRLYT pane tree, for example {@code layout("Layout/item_win.arc", "itm_win_00.brlyt")}. */
    public synchronized Layout layout(String archive, String name) {
        try {
            Archive loaded = archive(archive);
            int[] span = name == null ? null : loaded.files.get(name);
            return span == null ? null : Layout.parse(ByteBuffer.wrap(loaded.data, span[0], span[1]).slice());
        } catch (RuntimeException malformed) {
            return null;
        }
    }

    /**
     * Reads a BRLAN texture-pattern track: element {@code f} is the texture name (without
     * {@code .tpl}) the game shows at animation frame {@code f}. The game picks an icon by setting
     * the frame, e.g. {@code texturePattern("Layout/icon.arc", "item_icon_00_item.brlan", "icn")}
     * (401 item icon frames) or {@code ("Layout/map.arc", "map_icon_00_mapIcon_TxPtrn.brlan",
     * "mapIcn_00")} (9 map marker frames).
     */
    public synchronized List<String> texturePattern(String archive, String animation, String material) {
        try {
            Archive loaded = archive(archive);
            int[] span = animation == null || material == null ? null : loaded.files.get(animation);
            if (span == null) return Collections.emptyList();
            return pattern(ByteBuffer.wrap(loaded.data, span[0], span[1]).slice(), material);
        } catch (RuntimeException malformed) {
            return Collections.emptyList();
        }
    }

    private List<String> list(String archive, String extension, boolean strip) {
        List<String> result = new ArrayList<>();
        try {
            for (String file : archive(archive).files.keySet()) {
                if (file.endsWith(extension))
                    result.add(strip ? file.substring(0, file.length() - extension.length()) : file);
            }
        } catch (RuntimeException malformed) {
            result.clear();
        }
        return result;
    }

    // ---- U8 archive ---------------------------------------------------------------------------

    private static final class Archive {
        final byte[] data;
        /** File name without directories to {offset, size}. */
        final LinkedHashMap<String, int[]> files;

        Archive(byte[] data, LinkedHashMap<String, int[]> files) {
            this.data = data;
            this.files = files;
        }
    }

    private Archive archive(String path) {
        if (path == null) return MISSING;
        Archive cached = archives.get(path);
        if (cached != null) return cached;
        Archive loaded = MISSING;
        try {
            loaded = read(path);
        } catch (IOException | RuntimeException unavailable) {
            loaded = MISSING;
        }
        archives.put(path, loaded);
        return loaded;
    }

    private Archive read(String path) throws IOException {
        if (files == null || path.isEmpty() || path.startsWith("/") || path.contains("..")
                || path.indexOf('\\') >= 0 || path.indexOf(':') >= 0) return MISSING;
        byte[] data;
        try (RandomAccessFile file = new RandomAccessFile(new File(files, path), "r")) {
            long length = file.length();
            if (length < 32 || length > MAX_ARCHIVE) return MISSING;
            data = new byte[(int) length];
            file.readFully(data);
        }
        ByteBuffer u8 = ByteBuffer.wrap(data);
        if (u8.getInt(0) != 0x55aa382d) return MISSING;
        int root = u8.getInt(4);
        if (!contains(data.length, root, 12)) return MISSING;
        int count = u8.getInt(root + 8);
        if (count < 1 || count > MAX_NODES || !contains(data.length, root, 12 * count)) return MISSING;
        int strings = root + 12 * count;
        LinkedHashMap<String, int[]> found = new LinkedHashMap<>();
        for (int entry = 1; entry < count; entry++) {
            int node = root + entry * 12;
            int flags = u8.getInt(node);
            if ((flags >>> 24) != 0) continue;
            int start = strings + (flags & 0xffffff);
            int end = start;
            while (end < data.length && end - start < 128 && data[end] != 0) end++;
            if (end >= data.length || end == start || end - start >= 128) continue;
            int offset = u8.getInt(node + 4);
            int size = u8.getInt(node + 8);
            if (!contains(data.length, offset, size)) continue;
            String name = new String(data, start, end - start, StandardCharsets.ISO_8859_1);
            if (!found.containsKey(name)) found.put(name, new int[] {offset, size});
        }
        return new Archive(data, found);
    }

    private static boolean contains(long size, long start, long length) {
        return start >= 0 && length >= 0 && start + length <= size;
    }

    // ---- TPL textures -------------------------------------------------------------------------

    private static Bitmap decodeTpl(byte[] data, int base, int size, int image) {
        ByteBuffer tpl = ByteBuffer.wrap(data, base, size).slice();
        if (size < 20 || tpl.getInt(0) != 0x0020af30) return null;
        int count = tpl.getInt(4);
        int table = tpl.getInt(8);
        if (image >= count || !contains(size, table + 8L * image, 8)) return null;
        int header = tpl.getInt(table + 8 * image);
        int paletteHeader = tpl.getInt(table + 8 * image + 4);
        if (!contains(size, header, 12)) return null;
        int height = tpl.getShort(header) & 0xffff;
        int width = tpl.getShort(header + 2) & 0xffff;
        int format = tpl.getInt(header + 4);
        int pixels = tpl.getInt(header + 8);
        if (width < 1 || height < 1 || width > MAX_SIDE || height > MAX_SIDE) return null;

        int blockWidth, blockHeight, blockBytes = 32;
        switch (format) {
            case 0: case 8: case 14: blockWidth = 8; blockHeight = 8; break; // I4, CI4, CMPR
            case 1: case 2: case 9: blockWidth = 8; blockHeight = 4; break;  // I8, IA4, CI8
            case 3: case 4: case 5: case 10: blockWidth = 4; blockHeight = 4; break; // IA8, RGB565, RGB5A3, CI14X2
            default: return null;
        }
        int across = (width + blockWidth - 1) / blockWidth;
        int down = (height + blockHeight - 1) / blockHeight;
        if (!contains(size, pixels, (long) across * down * blockBytes)) return null;

        int[] palette = null;
        if (format == 8 || format == 9 || format == 10) {
            if (paletteHeader == 0 || !contains(size, paletteHeader, 12)) return null;
            int entries = tpl.getShort(paletteHeader) & 0xffff;
            int paletteFormat = tpl.getInt(paletteHeader + 4);
            int paletteData = tpl.getInt(paletteHeader + 8);
            if (entries > 16384 || !contains(size, paletteData, 2L * entries)) return null;
            if (paletteFormat != 1 && paletteFormat != 2) return null;
            palette = new int[format == 8 ? 16 : format == 9 ? 256 : 16384];
            for (int i = 0; i < entries && i < palette.length; i++) {
                int sample = tpl.getShort(paletteData + 2 * i) & 0xffff;
                palette[i] = paletteFormat == 1 ? rgb565(sample) : rgb5a3(sample);
            }
        }

        int[] colors = new int[width * height];
        int[] block = new int[blockWidth * blockHeight];
        int offset = base + pixels;
        for (int blockY = 0; blockY < height; blockY += blockHeight) {
            for (int blockX = 0; blockX < width; blockX += blockWidth) {
                decodeBlock(data, offset, format, palette, block);
                offset += blockBytes;
                for (int y = 0; y < blockHeight && blockY + y < height; y++) {
                    for (int x = 0; x < blockWidth && blockX + x < width; x++)
                        colors[(blockY + y) * width + blockX + x] = block[y * blockWidth + x];
                }
            }
        }
        return Bitmap.createBitmap(colors, width, height, Bitmap.Config.ARGB_8888);
    }

    /** Decodes one 32-byte GX texture block into row-major ARGB. */
    private static void decodeBlock(byte[] data, int offset, int format, int[] palette, int[] block) {
        switch (format) {
            case 0: // I4: intensity is also the alpha
            case 8: // CI4
                for (int i = 0; i < 64; i++) {
                    int value = data[offset + i / 2] & 0xff;
                    value = (i & 1) == 0 ? value >> 4 : value & 15;
                    block[i] = format == 8 ? palette[value] : value * 17 * 0x01010101;
                }
                break;
            case 1: // I8
                for (int i = 0; i < 32; i++) block[i] = (data[offset + i] & 0xff) * 0x01010101;
                break;
            case 2: // IA4: alpha in the high nibble
                for (int i = 0; i < 32; i++) {
                    int value = data[offset + i] & 0xff;
                    block[i] = (value >> 4) * 17 << 24 | (value & 15) * 17 * 0x010101;
                }
                break;
            case 9: // CI8
                for (int i = 0; i < 32; i++) block[i] = palette[data[offset + i] & 0xff];
                break;
            case 3: // IA8: alpha byte first
                for (int i = 0; i < 16; i++)
                    block[i] = (data[offset + 2 * i] & 0xff) << 24 | (data[offset + 2 * i + 1] & 0xff) * 0x010101;
                break;
            case 4: // RGB565
                for (int i = 0; i < 16; i++) block[i] = rgb565(word(data, offset + 2 * i));
                break;
            case 5: // RGB5A3
                for (int i = 0; i < 16; i++) block[i] = rgb5a3(word(data, offset + 2 * i));
                break;
            case 10: // CI14X2
                for (int i = 0; i < 16; i++) block[i] = palette[word(data, offset + 2 * i) & 0x3fff];
                break;
            default: // 14, CMPR: four 4x4 DXT1 sub-blocks in reading order, big-endian, MSB-first indices
                int[] choice = new int[4];
                for (int sub = 0; sub < 4; sub++) {
                    int at = offset + 8 * sub;
                    int first = word(data, at);
                    int second = word(data, at + 2);
                    choice[0] = rgb565(first);
                    choice[1] = rgb565(second);
                    if (first > second) {
                        choice[2] = blend(choice[1], choice[0]);
                        choice[3] = blend(choice[0], choice[1]);
                    } else {
                        choice[2] = average(choice[0], choice[1]);
                        choice[3] = choice[2] & 0x00ffffff;
                    }
                    for (int i = 0; i < 16; i++) {
                        int index = (data[at + 4 + i / 4] & 0xff) >> (6 - 2 * (i % 4)) & 3;
                        block[(sub / 2 * 4 + i / 4) * 8 + sub % 2 * 4 + i % 4] = choice[index];
                    }
                }
                break;
        }
    }

    private static int word(byte[] data, int offset) {
        return (data[offset] & 0xff) << 8 | (data[offset + 1] & 0xff);
    }

    private static int five(int value) {
        return value << 3 | value >> 2;
    }

    private static int rgb565(int sample) {
        int green = sample >> 5 & 63;
        return 0xff000000 | five(sample >> 11 & 31) << 16 | (green << 2 | green >> 4) << 8 | five(sample & 31);
    }

    private static int rgb5a3(int sample) {
        if ((sample & 0x8000) != 0)
            return 0xff000000 | five(sample >> 10 & 31) << 16 | five(sample >> 5 & 31) << 8 | five(sample & 31);
        int alpha = sample >> 12 & 7;
        return (alpha << 5 | alpha << 2 | alpha >> 1) << 24 | (sample >> 8 & 15) * 17 << 16
                | (sample >> 4 & 15) * 17 << 8 | (sample & 15) * 17;
    }

    /** Opaque 3/8 of {@code a} plus 5/8 of {@code b} per channel, as the console's decoder blends. */
    private static int blend(int a, int b) {
        int result = 0xff000000;
        for (int shift = 0; shift < 24; shift += 8)
            result |= ((a >> shift & 0xff) * 3 + (b >> shift & 0xff) * 5 >> 3) << shift;
        return result;
    }

    private static int average(int a, int b) {
        int result = 0xff000000;
        for (int shift = 0; shift < 24; shift += 8)
            result |= ((a >> shift & 0xff) + (b >> shift & 0xff) >> 1) << shift;
        return result;
    }

    // ---- BRLAN texture pattern ----------------------------------------------------------------

    private static List<String> pattern(ByteBuffer file, String material) {
        if (file.getInt(0) != 0x524c414e) return Collections.emptyList(); // RLAN
        int section = file.getShort(12) & 0xffff;
        int sections = file.getShort(14) & 0xffff;
        for (int s = 0; s < sections && s < 16; s++) {
            int size = file.getInt(section + 4);
            if (size < 8) break;
            if (file.getInt(section) == 0x70616931) { // pai1
                int frames = file.getShort(section + 8) & 0xffff;
                int textures = file.getShort(section + 12) & 0xffff;
                int entries = file.getShort(section + 14) & 0xffff;
                int entryTable = section + file.getInt(section + 16);
                int textureTable = section + 20;
                for (int e = 0; e < entries && e < 1024; e++) {
                    int entry = section + file.getInt(entryTable + 4 * e);
                    if (!text(file, entry, 20).equals(material)) continue;
                    int tags = file.get(entry + 20) & 0xff;
                    for (int t = 0; t < tags; t++) {
                        int tag = entry + file.getInt(entry + 24 + 4 * t);
                        if (file.getInt(tag) != 0x524c5450 || file.get(tag + 4) == 0) continue; // RLTP
                        int track = tag + file.getInt(tag + 8);
                        if ((file.getShort(track + 2) & 0xffff) != 0x100) continue; // step keys only
                        int keys = file.getShort(track + 4) & 0xffff;
                        int key = track + file.getInt(track + 8);
                        String[] result = new String[Math.min(frames, 4096)];
                        for (int k = 0; k < keys; k++) {
                            int frame = Math.round(file.getFloat(key + 8 * k));
                            int index = file.getShort(key + 8 * k + 4) & 0xffff;
                            if (index >= textures || frame < 0) continue;
                            String name = text(file, textureTable + file.getInt(textureTable + 4 * index), 64);
                            if (name.endsWith(".tpl")) name = name.substring(0, name.length() - 4);
                            for (int f = frame; f < result.length; f++) result[f] = name; // a step key holds
                        }
                        List<String> list = new ArrayList<>(result.length);
                        Collections.addAll(list, result);
                        return list;
                    }
                }
            }
            section += size;
        }
        return Collections.emptyList();
    }

    private static String text(ByteBuffer file, int offset, int limit) {
        StringBuilder text = new StringBuilder();
        for (int i = 0; i < limit && offset + i < file.limit(); i++) {
            int c = file.get(offset + i) & 0xff;
            if (c == 0) break;
            text.append((char) c);
        }
        return text.toString();
    }

    // ---- BRLYT layout -------------------------------------------------------------------------

    /** The pane tree of one BRLYT. Coordinates are layout units, origin at the centre, y up. */
    public static final class Layout {
        public final float width;
        public final float height;
        /** Panes in file (draw) order; parents come before their children. */
        public final List<Pane> panes;
        /** Texture file names in the layout's texture list, with {@code .tpl}. */
        public final List<String> textures;

        private Layout(float width, float height, List<Pane> panes, List<String> textures) {
            this.width = width;
            this.height = height;
            this.panes = Collections.unmodifiableList(panes);
            this.textures = Collections.unmodifiableList(textures);
        }

        public Pane pane(String name) {
            for (Pane pane : panes) if (pane.name.equals(name)) return pane;
            return null;
        }

        static Layout parse(ByteBuffer file) {
            if (file.getInt(0) != 0x524c5954) return null; // RLYT
            int section = file.getShort(12) & 0xffff;
            int sections = file.getShort(14) & 0xffff;
            float width = 0, height = 0;
            List<String> textures = new ArrayList<>();
            List<String> materialNames = new ArrayList<>();
            List<String[]> materialTextures = new ArrayList<>();
            List<int[]> materialColors = new ArrayList<>();
            List<Pane> panes = new ArrayList<>();
            Pane parent = null, last = null;
            for (int s = 0; s < sections && s < 8192; s++) {
                int tag = file.getInt(section);
                int size = file.getInt(section + 4);
                if (size < 8) return null;
                switch (tag) {
                    case 0x6c797431: // lyt1
                        width = file.getFloat(section + 12);
                        height = file.getFloat(section + 16);
                        break;
                    case 0x74786c31: { // txl1
                        int count = file.getShort(section + 8) & 0xffff;
                        for (int i = 0; i < count && i < 1024; i++)
                            textures.add(text(file, section + 12 + file.getInt(section + 12 + 8 * i), 64));
                        break;
                    }
                    case 0x6d617431: { // mat1
                        int count = file.getShort(section + 8) & 0xffff;
                        for (int i = 0; i < count && i < 1024; i++) {
                            int material = section + file.getInt(section + 12 + 4 * i);
                            int[] colors = new int[8]; // fore RGBA then back RGBA (signed 16-bit each)
                            for (int c = 0; c < 8; c++) colors[c] = file.getShort(material + 0x14 + 2 * c);
                            int maps = file.getInt(material + 0x3c) & 15;
                            String[] used = new String[maps];
                            for (int m = 0; m < maps; m++) {
                                int index = file.getShort(material + 0x40 + 4 * m) & 0xffff;
                                used[m] = index < textures.size() ? textures.get(index) : "";
                            }
                            materialNames.add(text(file, material, 20));
                            materialTextures.add(used);
                            materialColors.add(colors);
                        }
                        break;
                    }
                    case 0x70616e31: case 0x70696331: case 0x74787431: case 0x776e6431: case 0x626e6431: {
                        // pan1 pic1 txt1 wnd1 bnd1
                        Pane pane = new Pane();
                        pane.type = text(file, section, 4);
                        pane.parent = parent;
                        int flags = file.get(section + 8) & 0xff;
                        pane.visible = (flags & 1) != 0;
                        pane.origin = file.get(section + 9) & 0xff;
                        pane.alpha = file.get(section + 10) & 0xff;
                        pane.name = text(file, section + 12, 16);
                        pane.x = file.getFloat(section + 0x24);
                        pane.y = file.getFloat(section + 0x28);
                        pane.rotation = file.getFloat(section + 0x38);
                        pane.scaleX = file.getFloat(section + 0x3c);
                        pane.scaleY = file.getFloat(section + 0x40);
                        pane.width = file.getFloat(section + 0x44);
                        pane.height = file.getFloat(section + 0x48);
                        int material = -1;
                        if (tag == 0x70696331) {
                            material = file.getShort(section + 0x5c) & 0xffff;
                            if ((file.get(section + 0x5e) & 0xff) > 0) {
                                pane.uv = new float[8];
                                for (int i = 0; i < 8; i++) pane.uv[i] = file.getFloat(section + 0x60 + 4 * i);
                            }
                        } else if (tag == 0x74787431) {
                            material = file.getShort(section + 0x50) & 0xffff;
                        }
                        if (material >= 0 && material < materialNames.size()) {
                            pane.material = materialNames.get(material);
                            pane.textures = materialTextures.get(material);
                            pane.colors = materialColors.get(material);
                        }
                        panes.add(pane);
                        last = pane;
                        break;
                    }
                    case 0x70617331: // pas1
                        parent = last;
                        break;
                    case 0x70616531: // pae1
                        last = parent;
                        parent = parent == null ? null : parent.parent;
                        break;
                    default:
                        break;
                }
                section += size;
            }
            return new Layout(width, height, panes, textures);
        }
    }

    /** One pane. Position, scale and size are relative to the parent, as stored. */
    public static final class Pane {
        /** pan1 (group), pic1 (picture), txt1 (text), wnd1 (window frame) or bnd1 (hit box). */
        public String type;
        public String name;
        public Pane parent;
        public boolean visible;
        /** 0-8: column {@code origin % 3} is left, centre, right; row {@code origin / 3} is top, middle, bottom. */
        public int origin;
        public int alpha;
        public float x, y, rotation, scaleX, scaleY, width, height;
        /** Material name, or null. */
        public String material;
        /** The material's texture file names (with {@code .tpl}); empty for an untextured material. */
        public String[] textures = new String[0];
        /** Material fore colour RGBA then back colour RGBA: an I4/IA4 texture is tinted from fore (0) to back (1). */
        public int[] colors;
        /** Picture texture coordinates: top-left, top-right, bottom-left, bottom-right (s, t each), or null. */
        public float[] uv;

        /**
         * Rectangle in root layout units {left, top, right, bottom} with y up (top &gt; bottom unless
         * a scale is negative), from the translations and scales of this pane and its ancestors.
         * Rotations are ignored.
         */
        public float[] bounds() {
            float centreX = 0, centreY = 0, scaleX = 1, scaleY = 1;
            List<Pane> chain = new ArrayList<>();
            for (Pane pane = this; pane != null && chain.size() < 64; pane = pane.parent) chain.add(pane);
            for (int i = chain.size() - 1; i >= 0; i--) {
                Pane pane = chain.get(i);
                centreX += pane.x * scaleX;
                centreY += pane.y * scaleY;
                scaleX *= pane.scaleX;
                scaleY *= pane.scaleY;
            }
            float left = centreX - width * scaleX * (origin % 3) / 2f;
            float top = centreY + height * scaleY * (origin / 3) / 2f;
            return new float[] {left, top, left + width * scaleX, top - height * scaleY};
        }

        /** False when this pane or an ancestor is hidden. */
        public boolean shown() {
            int depth = 0;
            for (Pane pane = this; pane != null && depth < 64; pane = pane.parent, depth++)
                if (!pane.visible) return false;
            return true;
        }
    }
}
