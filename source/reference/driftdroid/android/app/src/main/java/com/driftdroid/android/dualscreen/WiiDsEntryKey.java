package com.driftdroid.android.dualscreen;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/** The single WiiDS entry on the game display, shown only when there is no lower display. Drawn in the game's skin. */
public final class WiiDsEntryKey extends View {
    private final WiiDsSkin skin;
    private final RectF bounds = new RectF();
    private boolean down;

    public WiiDsEntryKey(Context context, WiiDsSkin skin, Runnable open) {
        super(context);
        this.skin = skin;
        setFocusable(false);
        setContentDescription("WiiDS menu");
        setOnClickListener(view -> open.run());
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        boolean now = event.getActionMasked() == MotionEvent.ACTION_DOWN || (down && event.getActionMasked() == MotionEvent.ACTION_MOVE);
        if (now != down) { down = now; invalidate(); }
        return super.onTouchEvent(event);
    }

    @Override protected void onDraw(Canvas c) {
        float u = getHeight() / 17f;
        bounds.set(u * 1.5f, u * 1.5f, getWidth() - u * 1.5f, getHeight() - u * 2.5f);
        skin.key(c, bounds, "WiiDS", "gear", down ? WiiDsSkin.PRESSED : 0, false, u);
    }
}
