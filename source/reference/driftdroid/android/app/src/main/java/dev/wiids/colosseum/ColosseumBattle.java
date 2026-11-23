package dev.wiids.colosseum;

/**
 * The player's Pokémon on the field, read from the guest's battle state. {@link #ownsStatus} is true
 * only while the host has stopped drawing the upper screen's player status plates; until then the
 * lower screen leaves that status to the upper HUD.
 */
final class ColosseumBattle {
    static final int MAX = 2;
    boolean valid, ownsStatus, partyScreenUp;
    int count;
    final int[] slot = new int[MAX], level = new int[MAX], hp = new int[MAX], maxHp = new int[MAX], status = new int[MAX], exp = new int[MAX];
    final boolean[] shadow = new boolean[MAX];
    final String[] names = new String[MAX];

    private final int[] ints = new int[32];
    final int[] heart = new int[MAX];
    private boolean unavailable, latched;

    /** Refreshes this snapshot. False when the host has no battle feed or no battle is running. */
    boolean read() {
        valid = false;
        ownsStatus = false;
        count = 0;
        java.util.Arrays.fill(ints, 22, 29, -1); // older hosts do not write these
        if (unavailable) return false;
        try {
            if (!ColosseumState.readBattle(ints, names) || ints[0] < 1 || ints[0] > 2 || ints[1] != 1) { latched = false; return false; }
        } catch (UnsatisfiedLinkError missing) {
            unavailable = true;
            return false;
        }
        // Bit 1: the host is dropping the upper player plates. The plates are not drawn at all during attack
        // animations, so once seen in this battle the lower screen keeps the status while ownership is still
        // requested (bit 0) and the feed stays valid; it is released when either goes away.
        boolean requested = (ints[3] & 1) != 0;
        if ((ints[3] & 2) != 0) latched = true;
        if (!requested) latched = false;
        ownsStatus = latched;
        partyScreenUp = ints[28] == 1; // the game's own party screen is drawn on the upper screen
        for (int position = 0; position < MAX; position++) {
            int base = 6 + position * 8;
            if (ints[base] != 1) continue;
            slot[count] = ints[base + 1];
            level[count] = ints[base + 3];
            hp[count] = ints[base + 4];
            maxHp[count] = ints[base + 5];
            shadow[count] = ints[base + 7] == 1;
            heart[count] = ints[26 + position]; // the player's own Shadow battler; -1 otherwise
            status[count] = ints[base + 6];
            // EXP gained within the level over the EXP the level spans; -1 from the host means unknown.
            int gained = ints[22 + position * 2], span = ints[23 + position * 2];
            exp[count] = gained >= 0 && span > 0 ? Math.min(1000, (int) (gained * 1000L / span)) : -1;
            names[count] = names[position];
            count++;
        }
        valid = true;
        return true;
    }

    boolean active(int partySlot) {
        for (int i = 0; i < count; i++) if (slot[i] == partySlot) return true;
        return false;
    }
}
