package dev.wiids.cityfolk;

/** One read-only snapshot of the running town, published by the game thread once per frame. */
final class CityFolkState {
    static final int STARTING = 0, FRONTEND = 1, TOWN = 2, INDOORS = 3, CITY = 4, OTHER = 5;
    static final int NO_POINTER_SCREEN = 0, POCKETS = 1, TOWN_MAP = 2, OTHER_POINTER_SCREEN = 3;
    static final int ACRES = 4, POCKET_ITEMS = 53, POCKET_COUNT = 15;

    final int[] ints = new int[128];
    final float[] floats = new float[8];
    boolean valid;
    private boolean unavailable;

    private static native boolean read(int[] ints, float[] floats);
    /** The game's generated 240 x 240 map raster, north row first. Returns its revision; 0 until one exists. */
    static native int readMap(int knownRevision, byte[] indices, int[] argbPalette);
    /** Every building on the game map as (kind, tileX, tileZ). Returns the number of triples. */
    static native int readMarkers(int[] out);
    /** Pockets item codes and their icon frames. extra[3] = Pockets shown, extra[4] = 2 while the upper copy is not drawn, extra[5] = choice rows shown. */
    static native boolean readPockets(int[] codes15, int[] icons15, int[] extra);
    /** Slot centres and choice-row centres in pointer units. Returns 1 only while Pockets is drawn. */
    static native int readPocketsLayout(float[] out);
    /** Labels of the choices the game is offering, top to bottom, or null. */
    static native String[] readChoiceRows();
    /** Text of a message dialog that is up, or null. */
    static native String readDialog();
    /** The game's own name for an item code, once that item has been in the pockets this session; else null. */
    static native String itemName(int code);
    /** 0 item icon, 1 wrapped present, 2 wrapped present (second style), 3 egg. */
    static native boolean readPocketKinds(int[] kinds15);
    /** Names aligned with readMarkers: residents for houses, null where the game supplies none here. */
    static native String[] readMapNames();

    /** False until the game has published a frame, or when this build's runtime has no feed. */
    boolean refresh() {
        if (unavailable) return valid = false;
        try {
            java.util.Arrays.fill(ints, 96, 128, -1);
            valid = read(ints, floats);
        } catch (UnsatisfiedLinkError missing) {
            unavailable = true;
            valid = false;
        }
        return valid;
    }

    /** 2 while the circle wipe is fully closed (black), 1 while it is opening or closing. */
    int wipe() { return valid ? ints[96] : 0; }
    /** 1 text box up with no pointer screen, 2 text box up with a pointer screen (a choice or keyboard). */
    int textBox() { return valid ? ints[97] : 0; }
    boolean demoStage() { return valid && ints[98] == 1; }
    /** Average brightness of the upper picture, 0..255, or -1 until measured. */
    int topLevel() { return valid ? ints[99] : -1; }
    boolean stageChanging() { return valid && ints[100] == 1; }

    int scene() { return valid ? ints[0] : STARTING; }
    int pointerScreen() { return valid ? ints[1] : NO_POINTER_SCREEN; }
    /** 2 while the runtime is skipping the game's own Map layouts because the lower screen owns the map. */
    boolean guestMapHidden() { return valid && ints[70] == 2; }
    int acresWide() { return valid ? ints[2] : 0; }
    int acresHigh() { return valid ? ints[3] : 0; }
    int acre(int column, int row) { return ints[ACRES + row * acresWide() + column]; }
    boolean hasPlayer() { return valid && !Float.isNaN(floats[0]) && !Float.isNaN(floats[1]) && floats[3] > 0; }
    /** Player position in acres from the grid's north-west corner. */
    float playerColumn() { return (floats[0] - floats[4]) / floats[3]; }
    float playerRow() { return (floats[1] - floats[5]) / floats[3]; }
}
