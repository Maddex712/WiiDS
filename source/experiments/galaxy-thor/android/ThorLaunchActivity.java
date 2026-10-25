package dev.thor.galaxy;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;

public class ThorLaunchActivity extends Activity {
    protected boolean kaizoProfile() { return false; }
    @Override protected void attachBaseContext(android.content.Context base) { super.attachBaseContext(new GalaxyStorage(base, kaizoProfile())); }
    static boolean ready(android.content.Context context) {
        File external = context.getExternalFilesDir(null);
        if (external == null) return false;
        File root = new File(external, "GalaxyData");
        if (new File(external, ".galaxy-install.properties").exists()) return false;
        for (String name : new String[]{"sys/fst.bin", "sys/ErrorMessageArchive.arc",
                "sys/StoryEvent.bcsv", "sys/GalaxyID.bcsv", ".ready", "ThorHud/SETUP-MANIFEST.json",
                "ThorHud/planets/HeavensDoorGalaxy.planets"}) {
            File file = new File(root, name);
            if (!file.isFile() || file.length() == 0) return false;
        }
        try (FileInputStream stream = new FileInputStream(new File(root, "files/ObjectData/Coin.arc"))) {
            byte[] magic = new byte[4];
            return stream.read(magic) == 4 && magic[0] == 'C' && magic[1] == 'R'
                    && magic[2] == 'A' && magic[3] == 'R';
        } catch (Exception failure) { return false; }
    }

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent source = getIntent();
        boolean shared = "dev.thor.launcher.action.PLAY_GAME".equals(source.getAction());
        if (shared && (source.getIntExtra("thor_protocol", -1) != 1
                || !(kaizoProfile() ? "kaizo" : "galaxy").equals(source.getStringExtra("thor_game")))) {
            showMessage("This Galaxy launch request is not supported.");
            return;
        }
        if (!ready(this)) {
            showMessage("Super Mario Galaxy\n\nYour game files are not ready yet.");
            return;
        }
        startActivity(new Intent(this, kaizoProfile() ? KaizoActivities.Game.class : GalaxyActivity.class));
        finish();
    }

    private void showMessage(String value) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(24);
        text.setPadding(40, 40, 40, 40);
        setContentView(text);
    }
}
