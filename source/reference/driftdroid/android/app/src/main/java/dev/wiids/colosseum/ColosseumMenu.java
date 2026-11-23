package dev.wiids.colosseum;

/**
 * The battle input window the game is waiting on (commands, moves, targets, party), read from the
 * game's own window system. Layout is documented in companion_state.h (ColosseumState.readMenu, v2).
 */
final class ColosseumMenu {
    static final int NONE = 0, COMMANDS = 1, MOVES = 2, TARGETS = 3, PARTY = 4, BAG = 5, CHOICE = 6, ACTIONS = 7;
    static final int MAX_ROWS = 8;

    boolean open, withheld;
    int kind, rows, cursor, swapFrom, menuId, place, cursorItem, revision = -1;
    final int[] item = new int[MAX_ROWS], enabled = new int[MAX_ROWS], code = new int[MAX_ROWS], type = new int[MAX_ROWS],
            pp = new int[MAX_ROWS], maxPp = new int[MAX_ROWS], special = new int[MAX_ROWS], x = new int[MAX_ROWS], y = new int[MAX_ROWS],
            extra = new int[MAX_ROWS];
    final String[] label = new String[MAX_ROWS], detail = new String[MAX_ROWS];
    String title;

    private final int[] ints = new int[92];
    private final String[] labels = new String[17];
    private boolean unavailable;

    /** Refreshes from the host. False when the host has no menu feed or no input window is open. */
    boolean read() {
        open = false;
        withheld = false;
        if (unavailable) return false;
        try {
            if (!ColosseumState.readMenu(ints, labels)) return false;
        } catch (UnsatisfiedLinkError older) {
            unavailable = true;
            return false;
        }
        if (ints[0] != 2) return false;
        withheld = (ints[3] & 2) != 0;
        kind = ints[2];
        rows = Math.max(0, Math.min(MAX_ROWS, ints[6]));
        cursor = ints[7];
        menuId = ints[8];
        swapFrom = ints[11];
        place = ints[9];
        cursorItem = ints[10];
        revision = ints[4];
        title = labels[0];
        for (int r = 0; r < rows; r++) {
            int b = 12 + 10 * r;
            item[r] = ints[b];
            enabled[r] = ints[b + 1];
            code[r] = ints[b + 2];
            type[r] = ints[b + 3];
            pp[r] = ints[b + 4];
            maxPp[r] = ints[b + 5];
            special[r] = ints[b + 6];
            x[r] = ints[b + 7];
            y[r] = ints[b + 8];
            extra[r] = ints[b + 9];
            label[r] = labels[1 + 2 * r];
            detail[r] = labels[2 + 2 * r];
        }
        open = ints[1] == 1 && kind != NONE && rows > 0;
        return open;
    }

    /**
     * Commands, moves and targets are always drawn below while open. The party screen, bag, choices and action
     * list are drawn below only while the host is withholding their upper copy (it does so only when it read
     * them completely), so they are never shown twice and never lost.
     */
    boolean drawnBelow() {
        if (!open) return false;
        // The game's alternative layouts (0xF6 buttons, 0xF7/0xF8) are read as rows only and never withheld above, so
        // the lower screen leaves them to the upper one rather than show them twice.
        if (menuId == 0xF6 || menuId == 0xF7 || menuId == 0xF8) return false;
        if (kind == COMMANDS || kind == MOVES || kind == TARGETS) return true;
        return (kind == PARTY || kind == BAG || kind == CHOICE || kind == ACTIONS) && withheld;
    }

    static final String[] TYPES = {"Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "???",
            "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark"};
    static final int[] TYPE_COLORS = {0xFFA8A878, 0xFFC03028, 0xFFA890F0, 0xFFA040A0, 0xFFE0C068, 0xFFB8A038, 0xFFA8B820,
            0xFF705898, 0xFFB8B8D0, 0xFF68A090, 0xFFF08030, 0xFF6890F0, 0xFF78C850, 0xFFF8D030, 0xFFF85888, 0xFF98D8D8,
            0xFF7038F8, 0xFF705848};
}
