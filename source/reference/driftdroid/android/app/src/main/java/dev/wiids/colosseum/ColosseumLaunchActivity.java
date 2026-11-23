package dev.wiids.colosseum;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;
import com.driftdroid.android.SaveManagerProfiles;
import com.driftdroid.android.dualscreen.WiiDsScreens;

/** Starts the isolated runtime once, after WiiDS has checked for a retained session. */
public final class ColosseumLaunchActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            String blocker = SaveManagerProfiles.INSTANCE.launchBlocker(this);
            if (blocker != null) throw new IllegalStateException(blocker);
            SaveManagerProfiles.INSTANCE.requireStopped(this);
            if (!ColosseumStorage.ready(this))
                throw new IllegalStateException("Import your USA Pokémon Colosseum disc in Your games first.");
            prepare(ColosseumStorage.config(this));
            prepare(ColosseumStorage.card(this).getParentFile());
            Intent game = new Intent(this, ColosseumGameActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            startActivity(game, WiiDsScreens.launchOptions(this, game, null));
        } catch (Exception error) {
            Toast.makeText(this, error.getMessage(), Toast.LENGTH_LONG).show();
        }
        finish();
    }

    private static void prepare(java.io.File directory) throws java.io.IOException {
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new java.io.IOException("Could not prepare Pokémon Colosseum's storage.");
    }
}
