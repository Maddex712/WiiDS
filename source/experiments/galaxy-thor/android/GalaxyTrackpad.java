package dev.thor.galaxy;

/** Relative finger motion only. The native shared cursor owns its position across input devices. */
final class GalaxyTrackpad {
    static final int BEGIN = 0, MOVE = 1, END = 2, CANCEL = 3;
    interface Sink { void send(float dx, float dy, int phase); }
    private final Sink sink;
    private boolean held;
    private float x, y;
    GalaxyTrackpad(Sink sink) { this.sink = sink; }
    void begin(float px, float py) {
        if (held) cancel();
        if (!Float.isFinite(px) || !Float.isFinite(py)) return;
        x = px; y = py; held = true;
        sink.send(0, 0, BEGIN);
    }
    void move(float px, float py, float width, float height) {
        if (!held) return;
        if (!Float.isFinite(px) || !Float.isFinite(py) || !Float.isFinite(width)
                || !Float.isFinite(height) || width <= 0 || height <= 0) { cancel(); return; }
        float dx = 2 * (px - x) / width, dy = 2 * (py - y) / height;
        x = px; y = py;
        if (!Float.isFinite(dx) || !Float.isFinite(dy)) { cancel(); return; }
        if (dx != 0 || dy != 0) sink.send(dx, dy, MOVE);
    }
    void end(float px, float py, float width, float height) {
        move(px, py, width, height);
        if (held) { held = false; sink.send(0, 0, END); }
    }
    void cancel() { held = false; sink.send(0, 0, CANCEL); }
}
