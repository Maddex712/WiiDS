package dev.thor.mkdd;

import java.util.Arrays;

/** Standings rows slide between slots by stable kart id (smoothstep, 160 ms). */
final class DuoRowMotion {
    static final long DURATION_NS = 160_000_000L;
    private final float[] from = new float[8], to = new float[8], now = new float[8];
    private final long[] started = new long[8];
    private final boolean[] placed = new boolean[8];

    void reset() { Arrays.fill(placed, false); }

    /** Moves kart {@code id} toward {@code slot}; returns true while it is still moving. */
    boolean update(int id, int slot, long nowNs) {
        if (id < 0 || id >= 8) return false;
        if (!placed[id]) { placed[id]=true; from[id]=to[id]=now[id]=slot; started[id]=nowNs; return false; }
        if (to[id] != slot) { from[id]=now[id]; to[id]=slot; started[id]=nowNs; }
        float t = Math.min(1, Math.max(0, (nowNs-started[id]) / (float) DURATION_NS));
        now[id] = from[id]+(to[id]-from[id])*t*t*(3-2*t);
        return t < 1;
    }

    float slot(int id) { return id >= 0 && id < 8 ? now[id] : 0; }
}
