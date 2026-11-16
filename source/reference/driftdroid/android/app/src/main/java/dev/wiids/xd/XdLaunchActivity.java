package dev.wiids.xd;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.widget.Toast;
import com.driftdroid.android.SaveManagerProfiles;

/** Cold launch only, after the library has resumed any existing native task. */
public final class XdLaunchActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        String problem = SaveManagerProfiles.INSTANCE.launchBlocker(this);
        if (problem == null) {
            try { SaveManagerProfiles.INSTANCE.requireStopped(this); }
            catch (Exception busy) { problem = busy.getMessage(); }
        }
        if (problem == null && !XdStorage.ready(this)) problem = "Import your USA Pokemon XD revision 0 disc in Your games first.";
        if (problem != null) {
            Toast.makeText(this, problem, Toast.LENGTH_LONG).show();
            finish(); return;
        }
        startActivity(new Intent(this, XdGameActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP));
        finish();
    }
}
