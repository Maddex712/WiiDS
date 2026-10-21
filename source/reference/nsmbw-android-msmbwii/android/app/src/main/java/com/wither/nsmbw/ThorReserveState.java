package com.wither.nsmbw;

/** Read-only state of the extra stored-power-up mechanic; activation is a queued request. */
final class ThorReserveState {
    final int sequence, item, context, uses, stores;
    final boolean canUse, queued;
    private ThorReserveState(int[] words) {
        sequence = words[1]; item = words[2]; canUse = words[3] != 0; queued = words[4] != 0;
        context = words[5]; uses = words[6]; stores = words[7];
    }
    static ThorReserveState read() {
        int[] words = GameActivity.nativeReadThorReserve();
        return words != null && words.length == 8 && words[0] == 1 ? new ThorReserveState(words) : null;
    }
}
