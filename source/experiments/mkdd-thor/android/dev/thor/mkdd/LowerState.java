package dev.thor.mkdd;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.Collections;
import java.util.List;

/** Immutable renderer contract. No Android dependency, guest pointers or guest writes. */
public final class LowerState {
    public static final int MAGIC = 0x4d4b4444, VERSION = 1, HEADER = 16, STRIDE = 16, WORDS = 144;
    public static final int POSITION = 1, STANDING = 2, CHARACTERS = 4, ITEMS = 8;
    public static final int LAPS = 16, TIME = 32, DRIVER = 64;
    public final int frame, mode, phase, runState, courseId, courseSet, players, screens, totalLaps, targetKart;
    public final boolean available, demo, paused, mirror;
    public final List<Kart> karts;

    private LowerState(int[] p) {
        available = p != null;
        frame = available ? p[3] : 0;
        demo = available && (p[4] & 2) != 0;
        paused = available && (p[4] & 4) != 0;
        mode = available ? p[5] : 0; phase = available ? p[6] : 0;
        runState = available ? p[7] : 0; courseId = available ? p[8] : -1;
        courseSet = available ? p[15] : 0;
        players = available ? p[10] : 0; screens = available ? p[11] : 0;
        totalLaps = available ? p[12] : 0; targetKart = available ? p[13] : -1;
        mirror = available && p[14] != 0;
        ArrayList<Kart> result = new ArrayList<>();
        if (available) for (int i = 0; i < p[9]; ++i) result.add(new Kart(i, p));
        karts = Collections.unmodifiableList(result);
    }

    public static LowerState unavailable() { return new LowerState(null); }
    public static LowerState decode(int[] packet) {
        if (packet == null || packet.length != WORDS) return unavailable();
        int[] p = packet.clone();
        if (p[0] != MAGIC || p[1] != VERSION || p[2] != WORDS || (p[4]&1) == 0 ||
                (p[4] & ~7) != 0 || p[5] < 1 || p[5] > 7 || p[6] < 0 || p[6] > 19 ||
                p[7] < 0 || p[7] > 2 || p[9] < 1 || p[9] > 8 || p[10] < 0 || p[10] > 8 ||
                p[11] < 1 || p[11] > 4 || p[12] < 0 || p[12] > 99 ||
                p[13] < -1 || p[13] >= p[9] || p[14] < 0 || p[14] > 1 || p[15] < 0 || p[15] > 4 ||
                (p[8] != -1 && (p[8] < 0x21 || p[8] > 0x3b))) return unavailable();
        return new LowerState(p);
    }

    public Kart playerKart() { return targetKart >= 0 ? karts.get(targetKart) : null; }
    /** Plus reuses the retail slot IDs on each course set; never reuse another set's map. */
    public int mapKey() { return courseId < 0 ? -1 : courseId + courseSet * 256; }
    /** Stable kart IDs survive overtakes; unavailable ranks follow the game's known ranks. */
    public List<Kart> standings() {
        ArrayList<Kart> result = new ArrayList<>(karts);
        result.sort(Comparator.comparingInt((Kart k) -> k.rank < 1 ? 99 : k.rank)
                .thenComparingInt(k -> k.id));
        return Collections.unmodifiableList(result);
    }

    public static final class Kart {
        public final int id, rank, completedLaps, timeMs, character0, character1, driverSlot;
        /** Raw MKDD held-item kinds; -1 unknown/transition, 16 known empty. */
        public final int item0, item1;
        public final float x, y, z, headingRadians, speedKmh, lapProgress;
        public final boolean positionKnown, finished;
        Kart(int id, int[] p) {  // package-private: d8 34 rejects javac's synthetic access classes
            this.id = id;
            int b = HEADER+id*STRIDE, flags = p[b];
            rank = (flags&STANDING) != 0 && p[b+1] >= 1 && p[b+1] <= p[9] ? p[b+1] : -1;
            completedLaps = (flags&LAPS) != 0 && p[12] > 0 && p[b+2] >= 0 && p[b+2] <= p[12] ? p[b+2] : -1;
            timeMs = (flags&TIME) != 0 && p[b+3] >= 0 && p[b+3] < 5999999 ? p[b+3] : -1;
            boolean chars = (flags&CHARACTERS) != 0 && character(p[b+4]) && character(p[b+5]);
            character0 = chars ? p[b+4] : -1; character1 = chars ? p[b+5] : -1;
            driverSlot = (flags&DRIVER) != 0 && p[b+6] >= 0 && p[b+6] <= 1 ? p[b+6] : -1;
            item0 = (flags&ITEMS) != 0 && item(p[b+7]) ? p[b+7] : -1;
            item1 = (flags&ITEMS) != 0 && item(p[b+8]) ? p[b+8] : -1;
            float px=f(p[b+9]), py=f(p[b+10]), pz=f(p[b+11]);
            float heading=f(p[b+12]), speed=f(p[b+13]);
            positionKnown = (flags&POSITION) != 0 && coordinate(px) && coordinate(py) && coordinate(pz)
                    && Float.isFinite(heading) && Math.abs(heading) <= Math.PI+0.001
                    && Float.isFinite(speed) && Math.abs(speed) <= 1000;
            x = positionKnown ? px : Float.NaN; y = positionKnown ? py : Float.NaN;
            z = positionKnown ? pz : Float.NaN; headingRadians = positionKnown ? heading : Float.NaN;
            speedKmh = positionKnown ? speed : Float.NaN;
            float progress = f(p[b+14]);
            lapProgress = completedLaps >= 0 && Float.isFinite(progress) && progress >= 0 && progress <= 1
                    ? progress : Float.NaN;
            finished = completedLaps >= 0 && p[b+15] == 1;
        }
        public int driverCharacter() { return driverSlot == 0 ? character0 : driverSlot == 1 ? character1 : -1; }
        public int passengerCharacter() { return driverSlot == 0 ? character1 : driverSlot == 1 ? character0 : -1; }
        public int displayLap(int total) { return completedLaps < 0 || total < 1 ? -1 : Math.min(total, completedLaps+1); }
        private static boolean character(int v) { return v >= 1 && v <= 20; }
        private static boolean item(int v) { return v >= 0 && v <= 21; }
        private static boolean coordinate(float v) { return Float.isFinite(v) && Math.abs(v) <= 10000000; }
        private static float f(int bits) { return Float.intBitsToFloat(bits); }
    }

    /** Call with SystemClock.elapsedRealtimeNanos; repeated native packets expire. */
    public static final class Feed {
        public static final long MAX_AGE_NS = 500_000_000L;
        private LowerState current = unavailable();
        private long changedAt;
        public LowerState accept(int[] packet, long nowNs) {
            LowerState next = decode(packet);
            if (!next.available) { clear(); return current; }
            if (!current.available || next.frame != current.frame) { current=next; changedAt=nowNs; }
            if (nowNs < changedAt || nowNs-changedAt >= MAX_AGE_NS) return unavailable();
            return current;
        }
        public void clear() { current=unavailable(); changedAt=0; }
    }
}
