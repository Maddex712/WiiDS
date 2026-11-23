package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;
import android.view.MotionEvent;
import android.view.KeyEvent;

/** A normal application window on the primary screen, in the owning game's process. */
public class CompanionHostActivity extends Activity {
    private WiiDsPresentation content;

    static Class<? extends CompanionHostActivity> component(String process, String packageName) {
        String suffix = process.startsWith(packageName + ":") ? process.substring(packageName.length() + 1) : "";
        switch (suffix) {
            case "nsmbw_game": return Nsmb.class;
            case "msmbwii_game": return Msmb.class;
            case "sms_game": return Sunshine.class;
            case "eclipse_game": return Eclipse.class;
            case "galaxy_game": return Galaxy.class;
            case "galaxy_kaizo_game": return Kaizo.class;
            case "brawl_game": return Brawl.class;
            case "brawl_plus_game": return ProjectPlus.class;
            case "mkdd_game": return Mkdd.class;
            case "mkdd_plus_game": return MkddPlus.class;
            case "xd_game": return Xd.class;
            case "xg_game": return Xg.class;
            case "colosseum_game": return Colosseum.class;
            case "city_folk_game": return CityFolk.class;
            default:
                if (process.equals(packageName)) return CompanionHostActivity.class;
                throw new IllegalStateException("No companion host for " + suffix);
        }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        // Some Android versions focus this Activity even with two resumed displays. Forward its
        // controller events to the existing owner instead of starting a second game instance.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        content = WiiDsPresentation.claim(getIntent().getStringExtra("companion_token"));
        if (content == null) { finish(); return; } // stale task/process recreation cannot start a guest.
        content.attach(this);
    }
    @Override protected void onStop() { if (content != null && !isChangingConfigurations()) content.dismiss(); super.onStop(); }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (content != null) content.syncOwnerFocus();
    }
    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        boolean handled = super.dispatchTouchEvent(event);
        if (content != null) content.syncOwnerFocus();
        return handled;
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        return content != null && content.key(event) || super.dispatchKeyEvent(event);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        return content != null && content.motion(event) || super.dispatchGenericMotionEvent(event);
    }
    @Override protected void onDestroy() {
        if (content != null) { if (!isChangingConfigurations()) content.dismiss(); content = null; }
        super.onDestroy();
    }
    @Override public void onBackPressed() { if (content != null) content.back(); else finish(); }

    public static final class Nsmb extends CompanionHostActivity {}
    public static final class Msmb extends CompanionHostActivity {}
    public static final class Sunshine extends CompanionHostActivity {}
    public static final class Eclipse extends CompanionHostActivity {}
    public static final class Galaxy extends CompanionHostActivity {}
    public static final class Kaizo extends CompanionHostActivity {}
    public static final class Brawl extends CompanionHostActivity {}
    public static final class ProjectPlus extends CompanionHostActivity {}
    public static final class Mkdd extends CompanionHostActivity {}
    public static final class MkddPlus extends CompanionHostActivity {}
    public static final class Xd extends CompanionHostActivity {}
    public static final class Xg extends CompanionHostActivity {}
    public static final class Colosseum extends CompanionHostActivity {}
    public static final class CityFolk extends CompanionHostActivity {}
}
