package dev.thor.mkdd;

import java.util.Collections;
import java.util.List;
import java.util.Locale;

/** What the duo screen shows for one LowerState. Pure Java so the choices are JVM-tested. */
final class DuoScene {
    // Scene kinds (ints, not an enum: build-tools 34 d8 cannot read javac 21 enums).
    static final int IDLE = 0, RACE = 1, TIME_ATTACK = 2, BATTLE = 3;
    static final int ITEM_EMPTY = 16, ITEM_UNKNOWN = -1;

    final LowerState state;
    final int kind;
    /** The first console's kart, else the known leader (labelled as such), else null. */
    final LowerState.Kart focus;
    final boolean focusIsPlayer;
    /** Bounded display lap of the focus kart, or -1 when laps don't apply or aren't known. */
    final int lap;
    final List<LowerState.Kart> standings;

    private DuoScene(LowerState state, int kind, LowerState.Kart focus, boolean player, int lap,
                     List<LowerState.Kart> standings) {
        this.state=state; this.kind=kind; this.focus=focus; focusIsPlayer=player; this.lap=lap;
        this.standings=standings;
    }

    static DuoScene of(LowerState s) {
        if (s == null || !s.available || s.karts.isEmpty())
            return new DuoScene(LowerState.unavailable(), IDLE, null, false, -1, Collections.emptyList());
        int kind = s.mode == 1 ? TIME_ATTACK : s.mode <= 3 ? RACE : BATTLE;
        List<LowerState.Kart> standings = s.standings();
        // A demo's target kart is the CPU the camera follows, not the owner's.
        LowerState.Kart player = s.demo ? null : s.playerKart(), focus = player;
        if (focus == null) for (LowerState.Kart k : standings) if (k.rank == 1) { focus = k; break; }
        int lap = kind != BATTLE && focus != null ? focus.displayLap(s.totalLaps) : -1;
        return new DuoScene(s, kind, focus, player != null, lap, standings);
    }

    boolean isFocus(LowerState.Kart k) { return focus != null && k.id == focus.id; }

    /** The rider in the back seat holds the usable item; items follow character slots. */
    static int usableItem(LowerState.Kart k) {
        return k.driverSlot == 0 ? k.item1 : k.driverSlot == 1 ? k.item0 : ITEM_UNKNOWN;
    }
    static int driverItem(LowerState.Kart k) {
        return k.driverSlot == 0 ? k.item0 : k.driverSlot == 1 ? k.item1 : ITEM_UNKNOWN;
    }

    static String time(int ms) {
        if (ms < 0) return "-:--.---";
        return String.format(Locale.ROOT, "%d:%02d.%03d", ms/60000, ms/1000%60, ms%1000);
    }

    /** The game's own HUD clock format, "00:17:067"; dashes while unknown. */
    static String hudTime(int ms) {
        if (ms < 0) return "--:--:---";
        return String.format(Locale.ROOT, "%02d:%02d:%03d", Math.min(99, ms/60000), ms/1000%60, ms%1000);
    }

    static String ordinal(int rank) {
        int tens = rank % 100;
        if (tens >= 11 && tens <= 13) return "th";
        switch (rank % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; default: return "th"; }
    }
}
