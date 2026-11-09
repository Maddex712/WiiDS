package dev.wiids.brawl;

/** Brawl's completed result, rather than a placing guessed from the last live damage reading. */
final class BrawlResultSnapshot {
    static final int BASE = 68, PLAYERS = BASE + 4, EXTRA = PLAYERS + 4 * 4, LENGTH = EXTRA + 2;

    private BrawlResultSnapshot() { }

    static int[] retainPlayers(int[] previous, int[] live) {
        int[] merged = live.clone();
        if (previous == null) return merged;
        int count = 0;
        for (int p = 0; p < 4; p++) {
            int o = 12 + p * 14;
            // Brawl tears down the human's entry before the CPU entries at the end. Keep each slot's
            // last readable snapshot so teardown cannot turn a human match into a CPU-only demo.
            if (merged[o] == 0 && previous[o] == 1) System.arraycopy(previous, o, merged, o, 14);
            if (merged[o] == 1) count++;
        }
        merged[6] = count;
        return merged;
    }

    static int[] complete(int[] battle, int[] result) {
        if (battle == null || result == null || battle.length < LENGTH || result.length < LENGTH) return null;
        int decision = result[BASE];
        // Time-up, individual win and team win. No Contest (9), demos and other game modes are not results.
        if (decision < 1 || decision > 3 || result[BASE + 1] <= 0) return null;
        int[] finalState = battle.clone();
        System.arraycopy(result, BASE, finalState, BASE, LENGTH - BASE);
        finalState[4] = result[BASE + 1];
        finalState[7] = result[BASE + 3];
        for (int p = 0; p < 4; p++) {
            int o = 12 + p * 14, r = PLAYERS + p * 4;
            if (battle[o] != 1) continue;
            if (result[r] < 0 || result[r] > 3 || result[r + 1] < 0 || result[r + 1] > 999
                    || result[r + 2] < 0 || result[r + 2] > 999 || result[r + 3] < 0 || result[r + 3] > 999)
                return null;
            finalState[o + 10] = result[r + 1];  // KOs include the last frame of the match
            finalState[o + 6] = result[r + 2];
            finalState[o + 11] = result[r + 3];
        }
        if (finalState[7] != 0) {
            int winner = result[EXTRA];
            if (winner < 0 || winner > 3 || battle[12 + winner * 14] != 1) return null;
            int winningTeam = battle[12 + winner * 14 + 7];
            boolean[] presentTeams = new boolean[3];
            int teamCount = 0;
            for (int p = 0; p < 4; p++) {
                int o = 12 + p * 14;
                if (battle[o] != 1) continue;
                int team = battle[o + 7];
                if (team < 0 || team >= presentTeams.length) return null;
                if (!presentTeams[team]) { presentTeams[team] = true; teamCount++; }
            }
            for (int p = 0; p < 4; p++) {
                int o = 12 + p * 14;
                if (battle[o] != 1) continue;
                // gmResultInfo's winning player is the winning team's representative. Its raw m_rank
                // still orders individual scores. Two-team matches have one shared place per side.
                // In a three-team match, omit unverified second/third badges rather than invent them.
                finalState[PLAYERS + p * 4] = battle[o + 7] == winningTeam ? 0 : teamCount == 2 ? 1 : -1;
            }
        }
        return finalState;
    }

    static int place(int[] state, int slot) { return state[PLAYERS + slot * 4] + 1; }
}
