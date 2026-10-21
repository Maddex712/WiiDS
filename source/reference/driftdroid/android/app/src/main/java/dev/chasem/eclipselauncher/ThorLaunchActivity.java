package dev.chasem.eclipselauncher;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import dev.chasem.eclipselauncher.GameActivity;
import dev.chasem.eclipselauncher.LauncherActivity;
import dev.chasem.eclipselauncher.ThorStorage;

/** Explicit, versioned transport from the shared Thor library. */
public final class ThorLaunchActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Intent request = getIntent();
        if (!"dev.thor.launcher.action.PLAY_GAME".equals(request.getAction())
            || request.getIntExtra("thor_protocol", -1) != 1
            || !"eclipse".equals(request.getStringExtra("thor_game"))) {
            android.util.Log.w("SMS-Thor", "Rejected unknown launch protocol");
            finish(); return;
        }
        ThorStorage.setLibrary(this, request.getStringExtra("thor_launcher_package"));
        try {
            ThorStorage.validateDisc(this);
            LauncherActivity.prepareGame(this);
            startActivity(new Intent(this, GameActivity.class).putExtra("thor_library", true)
                .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        } catch (java.io.IOException | IllegalStateException e) {
            android.util.Log.e("SMS-Thor", "Eclipse data is not ready", e);
            startActivity(new Intent(this, LauncherActivity.class));
            android.widget.Toast.makeText(this, e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
        finish();
    }
}
