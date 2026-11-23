package dev.wiids.colosseum;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.WindowManager;
import android.widget.FrameLayout;

import com.driftdroid.android.dualscreen.WiiDsPresentation;

/** Lower display: the field instrument, with a layer above it for the WiiDS menu. */
final class ColosseumPresentation extends WiiDsPresentation {
    private final Handler updates = new Handler(Looper.getMainLooper());
    private final ColosseumGameActivity owner;
    private final ColosseumBattle battle = new ColosseumBattle();
    private ColosseumBoard board;
    private FrameLayout layers;
    private boolean started;
    private int mapRevision;

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!started) return;
            ColosseumState snapshot = ColosseumState.read(mapRevision);
            if (snapshot != null) mapRevision = snapshot.mapRevision;
            board.render(snapshot, battle.read() ? battle : null);
            updates.postDelayed(this, snapshot != null && snapshot.scene == ColosseumState.BATTLE ? 50 : 100);
        }
    };

    ColosseumPresentation(ColosseumGameActivity activity, Display display) {
        super(activity, display);
        owner = activity;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (getWindow() != null) getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        board = new ColosseumBoard(getContext(), owner.menuArt(), owner::openSessionMenu, owner::sendTouchButton);
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
        mapRevision = 0;
        updates.post(tick);
    }

    @Override protected void onStop() {
        started = false;
        updates.removeCallbacks(tick);
        super.onStop();
    }
}
