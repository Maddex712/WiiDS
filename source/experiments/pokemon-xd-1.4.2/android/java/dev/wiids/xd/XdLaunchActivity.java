package dev.wiids.xd;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.widget.*;

/** Gated engineering entry. It must never pretend that the companion library runs the game. */
public final class XdLaunchActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        if(XdStorage.ready(this)) {
            startActivity(new Intent(this,XdGameActivity.class)); finish(); return;
        }
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);
        TextView status=new TextView(this);status.setText("Pokemon XD engineering preview\n"+
            (XdStorage.imported(this)?"USA revision 0 disc imported.\n":"Import your USA revision 0 disc through Your games.\n")+
            (XdStorage.NATIVE_GAME_AVAILABLE ? "Import the supported disc before starting this private runtime preview." :
            "The Android game runtime is not built yet. Gameplay is unavailable."));
        box.addView(status);Button back=new Button(this);back.setText("Return to WiiDS");back.setOnClickListener(v->finish());box.addView(back);setContentView(box);
    }
}
