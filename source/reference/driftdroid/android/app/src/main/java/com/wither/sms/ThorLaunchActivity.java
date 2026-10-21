package com.wither.sms;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import dev.chasem.smslauncher.GameActivity;
import dev.chasem.smslauncher.LauncherActivity;
import dev.chasem.smslauncher.ThorStorage;

/** Explicit, versioned transport from the shared Thor library. */
public final class ThorLaunchActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Intent request = getIntent();
        if (!"dev.thor.launcher.action.PLAY_GAME".equals(request.getAction())
            || request.getIntExtra("thor_protocol", -1) != 1
            || !"sunshine".equals(request.getStringExtra("thor_game"))) {
            android.util.Log.w("SMS-Thor", "Rejected unknown launch protocol");
            finish(); return;
        }
        try {
            ThorStorage.validateDisc(this);
            LauncherActivity.prepareGame(this);
            startActivity(new Intent(this, GameActivity.class).putExtra("thor_library", true)
                .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        } catch (java.io.IOException | IllegalStateException e) {
            android.util.Log.e("SMS-Thor", "Sunshine data is not ready", e);
            startActivity(new Intent(this, LauncherActivity.class));
            android.widget.Toast.makeText(this, e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
        finish();
    }
}
