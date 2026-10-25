package dev.thor.galaxy;

/** Five seconds of playable time, once per activity session; lower-view recreation does not reset it. */
final class GalaxyTouchHint {
    private long elapsed, last = -1;
    private boolean active;
    void update(long now, boolean controllable) {
        if (last >= 0 && active) elapsed = Math.min(5000, elapsed + Math.max(0, now - last));
        last = now;
        active = controllable && elapsed < 5000;
    }
    boolean visible() { return active && elapsed < 5000; }
}
