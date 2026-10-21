package dev.chasem.smslauncher;

/** Wall-clock motion and a display-aligned 60 Hz ceiling, independent of snapshot polling. */
final class ThorSunshineMotion {
    static final long FRAME_NANOS = 16_666_667L;
    private long nextFrame;

    void reset() { nextFrame = 0; }

    boolean frameDue(long frameNanos) {
        if (nextFrame == 0) nextFrame = frameNanos;
        if (frameNanos + 500_000L < nextFrame) return false;
        long steps = Math.max(1L, (frameNanos + 500_000L - nextFrame) / FRAME_NANOS + 1L);
        nextFrame += steps * FRAME_NANOS;
        return true;
    }

    static float follow(float current, float target, float seconds, float timeConstant) {
        return current + (target - current) * (1f - (float) Math.exp(-Math.max(0f, seconds) / timeConstant));
    }

    /** Repeats seamlessly across one wider-than-screen cloud strip. */
    static float cloudOffset(double seconds, float pixelsPerSecond, float period) {
        return (float) ((seconds * pixelsPerSecond) % period);
    }
}
