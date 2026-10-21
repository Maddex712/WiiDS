package com.wither.nsmbw;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

/** Entry point from the WiiDS launcher. No native library is loaded in this process. */
public class ThorLaunchActivity extends Activity {
    static final String EXTRA_SESSION = "thor_launcher_session";
    private static final String ACTION_PLAY = "dev.thor.launcher.action.PLAY_GAME";
    private static final String HOME = "com.driftdroid.android.ModePickerActivity";

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        final Intent request = getIntent();
        if (!NsmbBuild.THOR_BUILD || request == null
                || !ACTION_PLAY.equals(request.getAction())
                || request.getIntExtra("thor_protocol", 0) != 1
                || !NsmbVariant.current().id.equals(request.getStringExtra("thor_game"))) {
            finish();
            return;
        }
        // Validation includes hashing main.dol. Do file work before handing off to SDL's :game.
        new Thread(() -> {
            final boolean ready = AppFiles.gameReady(this);
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                final Intent next = new Intent(this, ready ? NsmbVariant.current().gameActivity : NsmbVariant.current().launcherActivity)
                        .putExtra(EXTRA_SESSION, true);
                if (!ready) next.putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true);
                startActivity(next);
                finish();
            });
        }, "ThorNsmbwLaunch").start();
    }

    /** The target is fixed: callers cannot request an arbitrary package or component. */
    static boolean returnToLibrary(Activity activity) {
        try {
            activity.startActivity(new Intent(Intent.ACTION_MAIN)
                    .setComponent(new ComponentName(activity.getPackageName(), HOME))
                    .addCategory(Intent.CATEGORY_LAUNCHER)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
            return true;
        } catch (ActivityNotFoundException | SecurityException unavailable) {
            Toast.makeText(activity, "The WiiDS library is unavailable.", Toast.LENGTH_LONG).show();
            return false;
        }
    }
}
