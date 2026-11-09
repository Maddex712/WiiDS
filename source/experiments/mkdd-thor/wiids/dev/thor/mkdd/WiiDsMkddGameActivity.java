package dev.thor.mkdd;

import android.content.Context;
import android.os.Process;

/**
 * The game inside WiiDS: MkddGameActivity on the Double Dash storage profile, so the native
 * frontend's files dir is <external>/mkdd. The menu's Return to WiiDS goes back to the library and
 * ends this process at once (see MkddGameActivity.leave), so a quick relaunch always starts a
 * fresh :mkdd_game, never one still tearing down.
 */
public class WiiDsMkddGameActivity extends MkddGameActivity {
    protected boolean isPlus() { return false; }
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(new MkddStorage(base, isPlus())); }
    @Override protected String gameLibrary() { return isPlus() ? "wiids_mkdd_plus_game" : "wiids_mkdd_game"; }

    @Override public boolean canRestart() { return true; }
    @Override public String leaveLabel() { return "Return to WiiDS"; }

    @Override protected void leave() {
        MkddLaunchActivity.returnToLibrary(this);
        super.leave();
    }

    /** MkddLaunchActivity (another process) starts the game again once this process is gone. */
    @Override protected void restart() {
        MkddLaunchActivity.relaunch(this, Process.myPid(), isPlus());
        super.leave();
    }
}
