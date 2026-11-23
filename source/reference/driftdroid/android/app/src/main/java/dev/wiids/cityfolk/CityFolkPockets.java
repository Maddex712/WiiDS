package dev.wiids.cityfolk;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;

import com.driftdroid.android.dualscreen.WiiDsSkin;

import java.util.List;

/**
 * The player's Pockets on the lower screen. The game stays in charge of every item operation: this
 * view shows the game's own slots, item icons and the choices it is offering, and a tap puts the
 * game's pointer on that slot or choice and presses A there. It is used only while the runtime
 * reports that it is not drawing the Pockets window on the upper screen.
 */
final class CityFolkPockets {
    static final int SLOTS = 15, MAX_ROWS = 5;

    private final TownSkin skin;
    private final CityFolkArt art;
    private final int[] codes = new int[SLOTS], icons = new int[SLOTS], extra = new int[8];
    private final float[] layout = new float[72];
    private final int[] kinds = new int[SLOTS];
    private List<String> presentNames, present1Names, eggNames;
    private final RectF[] slots = new RectF[SLOTS], rows = new RectF[MAX_ROWS];
    private final RectF box = new RectF(), wear = new RectF(), letterKey = new RectF();
    private final float[] keptX = new float[SLOTS], keptY = new float[SLOTS];
    private boolean kept;
    private final RectF[] letters = new RectF[10];
    private final Paint image = new Paint(Paint.FILTER_BITMAP_FLAG);
    private List<String> iconNames;
    private String[] labels;
    private String dialog;
    private boolean valid, placed, missing;
    private int rowCount, chosen = -1;

    CityFolkPockets(TownSkin skin, CityFolkArt art) {
        this.skin = skin;
        this.art = art;
        for (int i = 0; i < SLOTS; i++) slots[i] = new RectF();
        for (int i = 0; i < MAX_ROWS; i++) rows[i] = new RectF();
        for (int i = 0; i < letters.length; i++) letters[i] = new RectF();
    }

    void refresh() {
        if (missing) return;
        try {
            valid = CityFolkState.readPockets(codes, icons, extra);
            placed = valid && CityFolkState.readPocketsLayout(layout) == 1;
            if (!valid || !CityFolkState.readPocketKinds(kinds)) java.util.Arrays.fill(kinds, 0);
            labels = valid ? CityFolkState.readChoiceRows() : null;
            dialog = valid ? CityFolkState.readDialog() : null;
        } catch (UnsatisfiedLinkError older) {
            missing = true;
            valid = false;
        }
        rowCount = placed ? Math.max(0, Math.min(MAX_ROWS, Math.round(layout[30]))) : 0;
        // Choices are offered only when the game supplied a readable label for every one of them.
        if (labels == null || labels.length < rowCount) rowCount = 0;
        for (int i = 0; i < rowCount; i++) if (labels[i] == null || labels[i].trim().isEmpty()) rowCount = 0;
        if (!valid || extra[3] != 1) chosen = -1;
    }

    /** True while the game shows Pockets and has stopped drawing its window on the upper screen. */
    boolean owned() { return valid && placed && extra[3] == 1 && extra[4] == 2; }

    /** A choice box or message is up but its text could not be read; the pointer sheet takes over for it. */
    boolean unreadableChoice() { return owned() && Math.round(layout[30]) > 0 && rowCount == 0; }

    private Bitmap icon(int slot) {
        int frame = icons[slot];
        if (art == null || codes[slot] < 0) return null;
        if (kinds[slot] >= 1 && kinds[slot] <= 3) {
            // Wrapped presents and eggs use the game's gift animations, first frame.
            if (presentNames == null) {
                presentNames = art.texturePattern("Layout/icon.arc", "gift_icon_00_present.brlan", "present");
                present1Names = art.texturePattern("Layout/icon.arc", "gift_icon_00_present1.brlan", "present");
                eggNames = art.texturePattern("Layout/icon.arc", "gift_icon_00_egg.brlan", "present");
            }
            List<String> names = kinds[slot] == 1 ? presentNames : kinds[slot] == 2 ? present1Names : eggNames;
            return names == null || names.isEmpty() || names.get(0) == null ? null : art.texture("Layout/icon.arc", names.get(0));
        }
        if (frame < 0) return null;
        if (iconNames == null) iconNames = art.texturePattern("Layout/icon.arc", "item_icon_00_item.brlan", "icn");
        if (iconNames == null || frame >= iconNames.size() || iconNames.get(frame) == null) return null;
        return art.texture("Layout/icon.arc", iconNames.get(frame));
    }

    void draw(Canvas c, RectF surface, float u) {
        // The game's own slot arrangement, scaled to the sheet. Rows of choices take the lower part when shown.
        float listHeight = rowCount > 0 ? Math.min(surface.height() * 0.4f, rowCount * u * 11 + (dialog == null ? 0 : u * 9)) : 0;
        // A band above the slots for the wear zone and Bells, and one below for the item's name and Letters,
        // so neither ever covers a slot.
        float band = rowCount == 0 ? u * 10 : 0;
        // The game's letter panel, when open, gets its own two rows of cards between the slots and the footer.
        int open = Math.round(layout[71]);
        float lettersHeight = rowCount == 0 && open > 0 && open <= letters.length ? u * 8 + u * 2.5f : 0;  // one row of cards
        float gridHeight = surface.height() - listHeight - band * 2 - lettersHeight;
        float scale = Math.min(surface.width() / 560f, gridHeight / 230f), cx = surface.centerX(), cy = surface.top + band + gridHeight / 2;
        // Slot centres come from the game's own hit boxes: pointer units times the layout scale give layout units (y up).
        // The diameter comes from the closest pair as drawn, so neighbours never touch whatever the game's own scale.
        // The game packs the slots closer while its letter panel is open, so the slots' own extent is fitted to the
        // space here (the game's arrangement, at whatever size fits).
        // While the letter panel is open the game moves its slots aside; the arrangement from before it opened is kept
        // (taps still go to the game's own slot, by number).
        float[] sx = new float[SLOTS], sy = new float[SLOTS];
        boolean lettersOpen = lettersHeight > 0;
        if (!lettersOpen) for (int i = 0; i < SLOTS; i++) { keptX[i] = layout[i * 2] * layout[41]; keptY[i] = -layout[i * 2 + 1] * layout[42]; kept = true; }
        float minX = Float.MAX_VALUE, maxX = -Float.MAX_VALUE, minY = Float.MAX_VALUE, maxY = -Float.MAX_VALUE;
        float[] lxs = new float[SLOTS], lys = new float[SLOTS];
        for (int i = 0; i < SLOTS; i++) {
            float lx = lettersOpen && kept ? keptX[i] : layout[i * 2] * layout[41], ly = lettersOpen && kept ? keptY[i] : -layout[i * 2 + 1] * layout[42];
            lxs[i] = lx; lys[i] = ly;
            minX = Math.min(minX, lx); maxX = Math.max(maxX, lx); minY = Math.min(minY, ly); maxY = Math.max(maxY, ly);
        }
        float spanX = Math.max(1f, maxX - minX), spanY = Math.max(1f, maxY - minY);
        float fit = Math.min((surface.width() - u * 24) / spanX, (gridHeight - u * 7) / spanY);
        if (rowCount == 0 && fit > 0 && fit < 1e4f) scale = fit;
        for (int i = 0; i < SLOTS; i++) {
            sx[i] = cx + (lxs[i] - (minX + maxX) / 2) * scale;
            sy[i] = cy + (lys[i] - (minY + maxY) / 2) * scale;
        }
        float nearest = Float.MAX_VALUE;
        for (int i = 0; i < SLOTS; i++) for (int j = i + 1; j < SLOTS; j++) {
            float apart = (float) Math.hypot(sx[i] - sx[j], sy[i] - sy[j]);
            if (apart > u * 0.5f) nearest = Math.min(nearest, apart); // slots the game folds onto one place do not count
        }
        float d = nearest == Float.MAX_VALUE ? 78 * scale : Math.min(nearest * 0.88f, u * 14);
        for (int i = 0; i < SLOTS; i++) {
            float x = sx[i], y = sy[i];
            slots[i].set(x - d / 2, y - d / 2, x + d / 2, y + d / 2);
            c.drawOval(slots[i], skin.fillPaint(0xFFE9DDB4));
            c.drawOval(slots[i], skin.linePaint(u * 0.35f, TownSkin.PAPER_EDGE));
            Bitmap bitmap = icon(i);
            if (bitmap != null) {
                box.set(x - d * 0.4f, y - d * 0.4f, x + d * 0.4f, y + d * 0.4f);
                c.drawBitmap(bitmap, null, box, image);
            } else if (codes[i] >= 0) {
                c.drawCircle(x, y, d * 0.16f, skin.fillPaint(TownSkin.INK_SOFT)); // an item whose icon is not known
            }
        }
        // The slot under the game's pointer, as the game hit-tests it, gets the focus ring.
        int hovered = extra[2] >= 0 && extra[2] < SLOTS ? extra[2] : chosen;
        if (hovered >= 0 && hovered < SLOTS) skin.focus(c, slots[hovered], WiiDsSkin.pulse(), u);
        String name = null;
        if (hovered >= 0 && hovered < SLOTS && codes[hovered] >= 0) {
            try { name = CityFolkState.itemName(codes[hovered]); } catch (UnsatisfiedLinkError older) { name = null; }
        }
        float infoY = rowCount == 0 ? surface.bottom - band / 2 : cy + 85 * scale;
        if (name != null && !name.isEmpty() && rowCount == 0)
            skin.ink(c, name, surface.centerX() - (valid(49) ? u * 8 : 0), infoY, u * 4.4f, u * 2.8f, surface.width() * 0.5f, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
        if (extra[0] >= 0)
            skin.ink(c, extra[0] + " Bells", surface.right - u * 3, surface.top + Math.max(band / 2, u * 3), u * 3.8f, u * 2.6f, surface.width() * 0.4f, TownSkin.LEAF_LOW, TownSkin.BOLD, Paint.Align.RIGHT);
        // The window's other drop and press targets, each placed where the game reports its hit box.
        wear.setEmpty(); letterKey.setEmpty();
        if (rowCount == 0 && valid(43)) {
            wear.set(surface.left + u * 2, surface.top + u * 1.5f, surface.left + u * 22, surface.top + band - u * 1.5f);
            skin.card(c, wear, 0, u);
            skin.ink(c, "Wear / use", wear.centerX(), wear.centerY(), u * 3.4f, u * 2.4f, wear.width() - u * 2, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
        }
        if (rowCount == 0 && valid(49)) {
            letterKey.set(surface.right - u * 22, surface.bottom - band + u * 1.5f, surface.right - u * 2, surface.bottom - u * 1.5f);
            skin.card(c, letterKey, 0, u);
            skin.ink(c, "Letters", letterKey.centerX(), letterKey.centerY(), u * 3.6f, u * 2.4f, letterKey.width() - u * 2, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
        }
        int shown = Math.round(layout[71]);
        for (int i = 0; i < letters.length; i++) letters[i].setEmpty();
        if (rowCount == 0 && shown > 0 && shown <= letters.length) {
            // The letter panel is open: one paper card per letter slot the game shows, in its order.
            int perRow = Math.max(1, shown);
            float w = (surface.width() - u * 4 - u * 1.2f * (perRow - 1)) / perRow, h = u * 8, top = surface.top + band + gridHeight + u * 1.5f;
            for (int i = 0; i < shown; i++) {
                if (!valid(51 + i * 2)) continue;
                float left = surface.left + u * 2 + i * (w + u * 1.2f), t = top;
                letters[i].set(left, t, left + w, t + h);
                skin.card(c, letters[i], 0, u);
                skin.glyph(c, "save", letters[i].centerX(), letters[i].centerY(), Math.min(h, w) * 0.5f, TownSkin.INK_SOFT);
            }
        }
        if (rowCount == 0) return;
        float y = surface.bottom - listHeight;
        if (dialog != null && !dialog.trim().isEmpty()) {
            skin.ink(c, dialog.replace('\n', ' '), surface.centerX(), y + u * 4, u * 4, u * 2.6f, surface.width() - u * 4, TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
            y += u * 9;
        }
        float height = (surface.bottom - y - u * 1.2f * (rowCount - 1)) / rowCount;
        for (int i = 0; i < rowCount; i++) {
            rows[i].set(surface.left + surface.width() * 0.18f, y, surface.right - surface.width() * 0.18f, y + height);
            skin.card(c, rows[i], 0, u);
            skin.ink(c, labels[i].trim(), rows[i].centerX(), rows[i].centerY(), Math.min(u * 4.6f, height * 0.5f), u * 2.6f, rows[i].width() - u * 6,
                    TownSkin.INK, TownSkin.BOLD, Paint.Align.CENTER);
            y += height + u * 1.2f;
        }
    }

    /** Where the game's pointer must go for a tap at (x, y): {pointerX, pointerY}, or null if nothing is there. */
    private boolean valid(int index) { return !Float.isNaN(layout[index]) && !Float.isNaN(layout[index + 1]); }

    /** Where the pointer goes while an item is dragged over (x, y): a slot, the wear zone or a letter slot. */
    float[] dropTarget(float x, float y) {
        if (wear.contains(x, y) && valid(43)) return new float[]{layout[43], layout[44]};
        for (int i = 0; i < letters.length; i++) if (letters[i].contains(x, y)) return new float[]{layout[51 + i * 2], layout[52 + i * 2]};
        for (int i = 0; i < SLOTS; i++) if (slots[i].contains(x, y)) return new float[]{layout[i * 2], layout[i * 2 + 1]};
        return null;
    }

    float[] target(float x, float y) {
        if (rowCount == 0 && letterKey.contains(x, y) && valid(49)) return new float[]{layout[49], layout[50]};
        if (rowCount == 0) for (int i = 0; i < letters.length; i++) if (letters[i].contains(x, y)) return new float[]{layout[51 + i * 2], layout[52 + i * 2]};
        for (int i = 0; i < rowCount; i++)
            if (rows[i].contains(x, y)) return new float[]{layout[31 + i * 2], layout[32 + i * 2]};
        if (rowCount > 0) return null; // a choice box is modal in the game too
        for (int i = 0; i < SLOTS; i++) {
            if (slots[i].contains(x, y)) {
                chosen = i;
                return new float[]{layout[i * 2], layout[i * 2 + 1]};
            }
        }
        return null;
    }
}
