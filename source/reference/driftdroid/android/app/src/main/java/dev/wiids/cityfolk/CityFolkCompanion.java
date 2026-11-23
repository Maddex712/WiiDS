package dev.wiids.cityfolk;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.WindowManager;
import android.widget.FrameLayout;

import com.driftdroid.android.dualscreen.WiiDsPresentation;

/** Lower display: the town noticeboard, with a layer above it for the WiiDS menu. */
final class CityFolkCompanion extends WiiDsPresentation {
    private final Handler updates = new Handler(Looper.getMainLooper());
    private final CityFolkGameActivity owner;
    private CityFolkBoard board;
    private FrameLayout layers;
    private boolean started;
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!started) return;
            board.tick();
            updates.postDelayed(this, 50);
        }
    };

    CityFolkCompanion(CityFolkGameActivity owner, Display display) {
        super(owner, display);
        this.owner = owner;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (getWindow() != null) getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        board = new CityFolkBoard(getContext(), owner, owner.art());
        layers = new FrameLayout(getContext());
        layers.addView(board, new FrameLayout.LayoutParams(-1, -1));
        setContentView(layers);
    }

    /** Where the WiiDS menu is hosted while this display owns it. Null until the window exists. */
    FrameLayout menuLayer() { return layers; }

    void releaseTouches() { if (board != null) board.releaseTouches(); }

    @Override protected void onStart() {
        super.onStart();
        started = true;
        updates.post(tick);
    }

    @Override protected void onStop() {
        started = false;
        updates.removeCallbacks(tick);
        if (board != null) board.releaseTouches();
        owner.aim(0, 0, false);
        super.onStop();
    }
}
