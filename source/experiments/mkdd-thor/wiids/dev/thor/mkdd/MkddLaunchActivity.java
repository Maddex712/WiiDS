package dev.thor.mkdd;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.widget.Toast;
import java.io.File;

/**
 * Entry point for WiiDS's carousel (ThorGameLaunch's PLAY_GAME protocol, game "mkdd"). No native
 * library is loaded in this process. The game itself runs in :mkdd_game (WiiDsMkddGameActivity).
 */
public final class MkddLaunchActivity extends Activity {
    private static final String ACTION_PLAY = "dev.thor.launcher.action.PLAY_GAME";
    static final String GAME = "mkdd";
    static final String PLUS = "mkdd-plus";
    private static final String HOME = "com.driftdroid.android.ModePickerActivity";
    /** Restart from the WiiDS menu: the game process to wait out before starting a new one. */
    private static final String AFTER_PID = "mkdd_after_pid";
    private static final long WAIT_MS = 3000;

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(new MkddStorage(base)); }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        Intent request = getIntent();
        String game = request == null ? null : request.getStringExtra("thor_game");
        if (request == null || !ACTION_PLAY.equals(request.getAction()) || request.getIntExtra("thor_protocol", 0) != 1
                || (!GAME.equals(game) && !PLUS.equals(game))) {
            finish();
            return;
        }
        int after = request.getIntExtra(AFTER_PID, 0);
        long until = SystemClock.uptimeMillis() + WAIT_MS;
        Handler main = new Handler(Looper.getMainLooper());
        Runnable[] start = new Runnable[1];
        start[0] = () -> {
            // A new :mkdd_game must not be handed to the old one while it is still ending.
            if (after > 0 && new File("/proc/" + after).exists()) {
                if (SystemClock.uptimeMillis() < until) { main.postDelayed(start[0], 50); return; }
                // Started now, it could land in the old process, which refuses a second game.
                Toast.makeText(this, "Double Dash is still closing. Start it again from the library.", Toast.LENGTH_LONG).show();
                returnToLibrary(this);
                finish();
                return;
            }
            if (PLUS.equals(game)) {
                if (!MkddProfiles.plusReady(this)) {
                    Toast.makeText(this, "Set up Double Dash Plus first.", Toast.LENGTH_LONG).show();
                    returnToLibrary(this); finish(); return;
                }
                new Thread(() -> {
                    boolean valid = MkddProfiles.verifyPlus(this);
                    main.post(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        if (valid) {
                            MkddProfiles.seedSettings(this);
                            startActivity(new Intent(this, WiiDsMkddPlusGameActivity.class));
                        } else {
                            Toast.makeText(this, "MKDD Plus 3.3 could not be verified. Install the supported patch again.", Toast.LENGTH_LONG).show();
                            returnToLibrary(this);
                        }
                        finish();
                    });
                }, "MkddPlusCheck").start();
                return;
            }
            Intent next = MkddStorage.ready(this) ? new Intent(this, WiiDsMkddGameActivity.class)
                    : new Intent().setClassName(getPackageName(), "com.driftdroid.android.GamesActivity").putExtra("storage_game", GAME);
            startActivity(next);
            finish();
        };
        start[0].run();
    }

    /** Starts Double Dash again once process [pid] (the game's, ending now) is gone. */
    static void relaunch(Activity activity, int pid, boolean plus) {
        activity.startActivity(new Intent(activity, MkddLaunchActivity.class).setAction(ACTION_PLAY)
                .putExtra("thor_protocol", 1).putExtra("thor_game", plus ? PLUS : GAME).putExtra(AFTER_PID, pid));
    }

    /** The target is fixed (this package's library): callers cannot request another component. */
    static void returnToLibrary(Activity activity) {
        try {
            activity.startActivity(new Intent(Intent.ACTION_MAIN).setComponent(new ComponentName(activity.getPackageName(), HOME))
                    .addCategory(Intent.CATEGORY_LAUNCHER)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        } catch (ActivityNotFoundException | SecurityException unavailable) {
            Toast.makeText(activity, "WiiDS is unavailable.", Toast.LENGTH_LONG).show();
        }
    }
}
