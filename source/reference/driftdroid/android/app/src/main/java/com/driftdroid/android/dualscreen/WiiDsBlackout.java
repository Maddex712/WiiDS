package com.driftdroid.android.dualscreen;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.SystemClock;

/**
 * The lower screen goes dark with the upper one: start-up black, fades, cutscenes and dialogue.
 * Cuts to and from black ramp over 300 ms (as Pokémon XD's lower screen does); with reduced motion it switches at once.
 */
public final class WiiDsBlackout {
    private static final float RAMP_MS = 300f;
    private final Paint paint = new Paint();
    private float level;          // 0 clear .. 1 black
    private float target;
    private long last;

    /** Sets how dark the lower screen should be (0..1). */
    public void target(float value) { target = Math.max(0f, Math.min(1f, value)); }

    public boolean active() { return level > 0.001f || target > 0.001f; }
    public boolean fullyBlack() { return level >= 0.999f; }

    /** Draws the black layer over everything already drawn. Returns true while it is still changing. */
    public boolean draw(Canvas c, float w, float h) {
        long now = SystemClock.uptimeMillis();
        float dt = last == 0 ? 0 : Math.min(100f, now - last);
        last = now;
        if (!WiiDsSkin.motion()) level = target;
        else if (level < target) level = Math.min(target, level + dt / RAMP_MS);
        else if (level > target) level = Math.max(target, level - dt / RAMP_MS);
        if (level > 0.001f) {
            paint.setColor(Math.round(level * 255) << 24);
            c.drawRect(0, 0, w, h, paint);
        }
        return level != target;
    }
}
