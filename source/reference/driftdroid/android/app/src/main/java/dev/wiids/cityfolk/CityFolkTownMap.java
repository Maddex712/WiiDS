package dev.wiids.cityfolk;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.RectF;

import com.driftdroid.android.dualscreen.WiiDsSkin;

/**
 * The town map. The terrain is the raster the game itself generates for its Map screen, copied out
 * of the running game; building markers come from the save's own table and the player marker from
 * the live position. Until the game has generated its raster this session, the sheet shows only the
 * acre grid with those live markers and says how to chart the town.
 */
final class CityFolkTownMap {
    private static final int SIZE = 240;
    // Marker inks by kind follow the game map: yellow shops and facilities, a pink town hall, blue houses.
    private static final int[] TINT = {0xFFD9A400, 0xFFD9A400, 0xFFE35AA8, 0xFFD9A400, 0xFFD9A400, 0xFFD9A400, 0xFF2F86E0, 0xFFE0513F, 0xFF4FA4E8};
    private final int[] markers = new int[60];
    private int markerCount;
    private String[] names;
    private final float[] placedX = new float[20], placedY = new float[20];
    private int picked = -1;
    private long pickedAt;
    private int[] discPalette;

    private final TownSkin skin;
    private final CityFolkArt art;
    private final byte[] indices = new byte[SIZE * SIZE];
    private final int[] palette = new int[16], pixels = new int[SIZE * SIZE];
    private final Bitmap[] icons = new Bitmap[8];
    private final RectF area = new RectF(), box = new RectF();
    private final Paint crisp = new Paint(), tinted = new Paint(Paint.FILTER_BITMAP_FLAG);
    private Bitmap raster, placeholder, playerIcon;
    private int revision;
    private boolean artLoaded, feedMissing;

    CityFolkTownMap(TownSkin skin, CityFolkArt art) {
        this.skin = skin;
        this.art = art;
        crisp.setFilterBitmap(false); // the game's raster is 3 px per tile; keep its edges
    }

    boolean charted() { return raster != null; }

    /** True once the snapshot carries the town grid, so the live markers can be placed. */
    boolean ready(CityFolkState state) {
        return state.acresWide() == 7 && state.acresHigh() == 7;
    }

    private void loadArt() {
        artLoaded = true;
        if (art == null) return;
        for (int i = 0; i < icons.length; i++) icons[i] = art.texture("Layout/map.arc", "map_iconFc_0" + i);
        playerIcon = art.texture("Layout/map.arc", "player_icon_00");
        placeholder = art.texture("Layout/map.arc", "map_240_CI4_00");
        discPalette = art.palette("Layout/map.arc", "map_240_CI4_00");
    }

    /** Picks up a newly generated raster. The disc's stand-in picture is never shown as the town. */
    void refresh() {
        if (feedMissing) return;
        if (!artLoaded) loadArt();
        int next;
        try {
            markerCount = Math.max(0, Math.min(20, CityFolkState.readMarkers(markers)));
            try { names = CityFolkState.readMapNames(); } catch (UnsatisfiedLinkError older) { names = null; }
            next = CityFolkState.readMap(revision, indices, palette);
        } catch (UnsatisfiedLinkError missing) {
            feedMissing = true;
            return;
        }
        if (next == 0 || next == revision) return;
        revision = next;
        boolean colours = false;
        for (int colour : palette) colours |= (colour & 0xffffff) != 0;
        // A raster painted at town load has no palette of its own yet; the disc map palette is the one the game loads.
        if (!colours && discPalette != null) System.arraycopy(discPalette, 0, palette, 0, 16);
        else if (!colours) return;
        for (int i = 0; i < pixels.length; i++) pixels[i] = palette[indices[i] & 15] | 0xff000000;
        if (isPlaceholder()) return;
        raster = Bitmap.createBitmap(pixels, SIZE, SIZE, Bitmap.Config.ARGB_8888);
    }

    private boolean isPlaceholder() {
        if (placeholder == null || placeholder.getWidth() < SIZE || placeholder.getHeight() < SIZE) return false;
        int different = 0;
        for (int y = 0; y < SIZE; y += 3) {
            for (int x = 0; x < SIZE; x += 3) {
                int a = pixels[y * SIZE + x], b = placeholder.getPixel(x, y);
                if (Math.abs(((a >> 16) & 255) - ((b >> 16) & 255)) > 10 || Math.abs(((a >> 8) & 255) - ((b >> 8) & 255)) > 10
                        || Math.abs((a & 255) - (b & 255)) > 10) different++;
            }
        }
        return different < 64; // of 6400 samples
    }

    void draw(Canvas c, RectF surface, CityFolkState state, float u) {
        float caption = raster == null ? u * 9 : 0, size = Math.min(surface.width(), surface.height() - caption);
        // On a wide sheet the map sits left and a key to its markers takes the right side.
        float spare = surface.width() - size, legendWidth = spare > u * 30 ? Math.min(spare - u * 4, u * 34) : 0;
        float mapLeft = legendWidth > 0 ? surface.left + (spare - legendWidth - u * 4) / 2 : surface.centerX() - size / 2;
        area.set(mapLeft, surface.top + (surface.height() - caption - size) / 2,
                mapLeft + size, surface.top + (surface.height() - caption - size) / 2 + size);
        float px = size / SIZE;
        if (raster != null) {
            c.drawBitmap(raster, null, area, crisp);
        } else {
            c.drawRect(area, skin.fillPaint(0xFFF3EACB));
            Paint dash = skin.linePaint(u * 0.3f, WiiDsSkin.alpha(TownSkin.INK_SOFT, 0.55f));
            dash.setPathEffect(new DashPathEffect(new float[]{u * 1.2f, u * 1.2f}, 0));
            for (int i = 1; i < 5; i++) {
                c.drawLine(area.left + size * i / 5, area.top, area.left + size * i / 5, area.bottom, dash);
                c.drawLine(area.left, area.top + size * i / 5, area.right, area.top + size * i / 5, dash);
            }
            dash.setPathEffect(null);
            skin.ink(c, "Open the Map once and the town is charted here.", surface.centerX(), area.bottom + u * 5, u * 3.8f, u * 2.6f,
                    surface.width() - u * 4, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
        }
        c.drawRect(area, skin.linePaint(u * 0.5f, TownSkin.INK));
        // Buildings: tile positions from the save, three raster pixels per tile, the game marker glyphs.
        for (int i = 0; i < markerCount; i++) {
            int kind = markers[i * 3], tileX = markers[i * 3 + 1], tileZ = markers[i * 3 + 2];
            placedX[i] = Float.NaN;
            if (kind < 0 || kind > 8 || tileX < 0 || tileZ < 0) continue;
            float x = area.left + ((tileX - 16) * 3 + 1.5f) * px, y = area.top + ((tileZ - 16) * 3 + 1.5f) * px;
            placedX[i] = x; placedY[i] = y;
            if (!area.contains(x, y)) continue;
            // Villager houses use the house glyph at a smaller size, as the game draws them.
            stamp(c, icons[kind == 8 ? 6 : kind], x, y, kind == 8 ? u * 3.8f : u * 5.2f, TINT[kind], u);
        }
        if (legendWidth > 0) legend(c, area.right + u * 4, area.top, legendWidth, u);
        // A tapped building is named for three seconds: residents by the save's own names, facilities by the game list wording.
        if (picked >= 0 && picked < markerCount && android.os.SystemClock.uptimeMillis() - pickedAt < 3000 && !Float.isNaN(placedX[picked])) {
            String label = label(picked);
            if (label != null) {
                android.text.TextPaint p = new android.text.TextPaint(Paint.ANTI_ALIAS_FLAG);
                p.setTextSize(u * 3.8f); p.setTypeface(TownSkin.BOLD);
                float w = p.measureText(label) + u * 4, x = Math.max(area.left, Math.min(area.right - w, placedX[picked] - w / 2)), y = placedY[picked] - u * 9;
                android.graphics.RectF tag = new android.graphics.RectF(x, y, x + w, y + u * 6);
                skin.card(c, tag, 0, u);
                skin.ink(c, label, tag.centerX(), tag.centerY(), u * 3.8f, u * 2.6f, w, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
            }
        }
        if (state.hasPlayer()) {
            float x = area.left + (state.playerColumn() - 1) * 48 * px, y = area.top + (state.playerRow() - 1) * 48 * px;
            if (area.contains(x, y)) {
                c.drawCircle(x, y, u * (2.8f + 1.3f * WiiDsSkin.pulse()), skin.fillPaint(WiiDsSkin.alpha(TownSkin.RED, 0.28f)));
                if (playerIcon != null) stamp(c, playerIcon, x, y, u * 5.6f, TownSkin.RED, u);
                else {
                    c.drawCircle(x, y, u * 1.9f, skin.fillPaint(0xFFFFFFFF));
                    c.drawCircle(x, y, u * 1.3f, skin.fillPaint(TownSkin.RED));
                }
            }
        }
    }

    // Names as the game's own Map list gives them. Kind 5, the round sign, has no proven name and is left out.
    private static final String[] NAMES = {"Shop", "Tailor", "Town Hall", "Museum", "Gate", null, "House", "Your house", "Neighbour"};

    private void legend(Canvas c, float x, float y, float width, float u) {
        boolean[] present = new boolean[9];
        for (int i = 0; i < markerCount; i++) if (markers[i * 3] >= 0 && markers[i * 3] <= 8) present[markers[i * 3]] = true;
        int lines = playerIcon != null ? 1 : 0;
        for (int kind = 0; kind < 9; kind++) if (present[kind] && NAMES[kind] != null) lines++;
        float row = Math.min(u * 6.4f, area.height() / Math.max(1, lines)), cy = y + row / 2;
        if (playerIcon != null) {
            stamp(c, playerIcon, x + u * 3, cy, u * 5, TownSkin.RED, u);
            skin.ink(c, "You", x + u * 7.5f, cy, u * 3.8f, u * 2.6f, width - u * 8, TownSkin.INK, TownSkin.BOLD, Paint.Align.LEFT);
            cy += row;
        }
        for (int kind = 0; kind < 9; kind++) {
            if (!present[kind] || NAMES[kind] == null) continue;
            stamp(c, icons[kind == 8 ? 6 : kind], x + u * 3, cy, kind == 8 ? u * 3.8f : u * 5, TINT[kind], u);
            skin.ink(c, NAMES[kind], x + u * 7.5f, cy, u * 3.8f, u * 2.6f, width - u * 8, TownSkin.INK, TownSkin.BOLD, Paint.Align.LEFT);
            cy += row;
        }
    }

    private String label(int i) {
        int kind = markers[i * 3];
        if (names != null && i < names.length && names[i] != null && !names[i].trim().isEmpty()) return names[i].trim();
        return kind >= 0 && kind < NAMES.length ? NAMES[kind] : null;
    }

    /** Selects the building nearest a tap on the map, if one is within reach. */
    boolean pick(float x, float y, float u) {
        int best = -1;
        float bestDistance = u * 6;
        for (int i = 0; i < markerCount; i++) {
            if (Float.isNaN(placedX[i])) continue;
            float d = (float) Math.hypot(placedX[i] - x, placedY[i] - y);
            if (d < bestDistance) { bestDistance = d; best = i; }
        }
        picked = best;
        pickedAt = android.os.SystemClock.uptimeMillis();
        return best >= 0;
    }

    /** The game's marker art is a white mask; it is inked in one colour over a paper disc so it reads on any terrain. */
    private void stamp(Canvas c, Bitmap mask, float x, float y, float size, int colour, float u) {
        c.drawCircle(x, y, size * 0.56f, skin.fillPaint(0xFFFFFDF2));
        c.drawCircle(x, y, size * 0.56f, skin.linePaint(u * 0.3f, WiiDsSkin.shade(colour, 0.7f)));
        if (mask == null) { c.drawCircle(x, y, size * 0.3f, skin.fillPaint(colour)); return; }
        tinted.setColorFilter(new PorterDuffColorFilter(colour, PorterDuff.Mode.SRC_IN));
        box.set(x - size * 0.42f, y - size * 0.42f, x + size * 0.42f, y + size * 0.42f);
        c.drawBitmap(mask, null, box, tinted);
    }
}
