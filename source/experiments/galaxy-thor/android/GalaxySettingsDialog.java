package dev.thor.galaxy;

import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;

/**
 * The window that holds Galaxy's settings menu over the game. While it is shown it has the input
 * focus, so every button and stick goes to the menu and none to the game, which the native side
 * freezes (nativeMenuState). Named and top-level: this d8 rejects javac 21's synthetic classes.
 */
final class GalaxySettingsDialog extends Dialog implements GalaxySettingsMenu.Host {
    private final GalaxyActivity activity;
    private GalaxySettingsMenu menu;
    private boolean closed;

    GalaxySettingsDialog(GalaxyActivity activity) {
        super(activity, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        this.activity = activity;
        setCancelable(false);
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
            window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            window.setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT);
            window.setDecorFitsSystemWindows(false);
        }
        menu = new GalaxySettingsMenu(getContext(), new File(activity.getExternalFilesDir(null), "GalaxyData/ThorHud"), this);
        setContentView(menu);
        // The bars can only be hidden once the window has its content.
        if (window != null && window.getInsetsController() != null) {
            window.getInsetsController().setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            window.getInsetsController().hide(android.view.WindowInsets.Type.systemBars());
        }
        menu.requestFocus();
    }

    @Override protected void onStart() {
        super.onStart();
        try { GalaxyActivity.nativeMenuState(true); } catch (UnsatisfiedLinkError ignored) { }
        GalaxyActivity.settingsShowing = true;
        if (menu != null) menu.requestFocus();
    }

    @Override public void setValue(int key, int value) {
        try { GalaxyActivity.nativeMenuSet(key, value); } catch (UnsatisfiedLinkError ignored) { }
    }

    @Override public void readValues(int[] out) {
        try { GalaxyActivity.nativeMenuValues(out); } catch (UnsatisfiedLinkError ignored) { }
    }

    @Override public void close() {
        if (closed) return;
        closed = true;
        GalaxyActivity.settingsShowing = false;
        try { GalaxyActivity.nativeMenuState(false); } catch (UnsatisfiedLinkError ignored) { }
        dismiss();
        activity.settingsClosed(this);
    }

    @Override public void returnToWiiDS() {
        closed = true;
        GalaxyActivity.settingsShowing = false;
        GalaxyActivity.returnToLauncher();
    }
}
