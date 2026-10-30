package dev.wiids.brawl;

import android.app.Activity;
import android.content.Context;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

/**
 * Entry point for WiiDS's carousel (ThorGameLaunch's PLAY_GAME protocol, game "brawl"). No native library is
 * loaded in this process. A game opened this way returns to WiiDS from its Main Menu item.
 */
public final class ThorLaunchActivity extends Activity {
    static final String EXTRA_SESSION = "thor_launcher_session";
    private static final String ACTION_PLAY = "dev.thor.launcher.action.PLAY_GAME";
    static final String GAME = "brawl";
    private static final ComponentName HOME = new ComponentName(
            "io.github.maddex712.wiids", "com.driftdroid.android.ModePickerActivity");

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(new BrawlStorage(base)); }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        final Intent request = getIntent();
        if (!isPlayRequest(request)) {
            finish();
            return;
        }
        // Validation includes hashing main.dol. Do file work before handing off to SDL's :game.
        new Thread(() -> {
            final boolean ready = AppFiles.gameReady(this);
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                final Intent next = ready ? new Intent(this,GameActivity.class).putExtra(EXTRA_SESSION,true)
                        : new Intent().setClassName(getPackageName(),"com.driftdroid.android.GamesActivity").putExtra("storage_game","brawl");
                startActivity(next);
                finish();
            });
        }, "ThorBrawlLaunch").start();
    }

    static boolean isPlayRequest(Intent request) {
        return request != null && ACTION_PLAY.equals(request.getAction())
                && request.getIntExtra("thor_protocol", 0) == 1
                && GAME.equals(request.getStringExtra("thor_game"));
    }

    /** The target is fixed: callers cannot request an arbitrary package or component. */
    static boolean returnToLibrary(Activity activity) {
        try {
            activity.startActivity(new Intent(Intent.ACTION_MAIN).setComponent(HOME)
                    .addCategory(Intent.CATEGORY_LAUNCHER)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
            return true;
        } catch (ActivityNotFoundException | SecurityException unavailable) {
            Toast.makeText(activity, "WiiDS is unavailable.", Toast.LENGTH_LONG).show();
            return false;
        }
    }
}
