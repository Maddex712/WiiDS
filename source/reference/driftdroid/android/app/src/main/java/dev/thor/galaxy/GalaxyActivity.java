package dev.thor.galaxy;

import android.app.NativeActivity;
import android.app.Presentation;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.view.Display;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;

public final class GalaxyActivity extends NativeActivity implements DisplayManager.DisplayListener {
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(new GalaxyStorage(base)); }
    private static GalaxyActivity instance;
    private DisplayManager displays;
    private Presentation lower;
    private boolean resumed;
    private boolean leaving;

    @Override public void onCreate(Bundle state) {
        instance = this;
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        fullscreen();
        displays = (DisplayManager)getSystemService(Context.DISPLAY_SERVICE);
        if (displays != null) displays.registerDisplayListener(this, null);
    }

    private void fullscreen() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        getWindow().setDecorFitsSystemWindows(false);
        getWindow().getDecorView().setSystemUiVisibility(5894 | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        if (getWindow().getInsetsController() != null) {
            getWindow().getInsetsController().setSystemBarsBehavior(
                    android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            getWindow().getInsetsController().hide(android.view.WindowInsets.Type.systemBars());
        }
    }

    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) fullscreen();
    }

    /**
     * The Thor lower screen's copy of the game's state snapshot (platform/include/port/thor_lower.h).
     * Registered by libgame.so itself (RegisterNatives in platform/mobile/thor/ThorLowerBridge.cpp),
     * since the game is loaded into its reserved window where JNI's own lookup cannot see it.
     * Returns the snapshot's sequence, 0 when none is available.
     */
    static native int nativeLowerState(java.nio.ByteBuffer out);
    /** The game's live space picture (platform/src/gx/vk/vk_thor_capture.cpp), registered the same way. */
    static native int nativeSpace(java.nio.ByteBuffer out, int have, int[] mode);
    /** The stage's live world pieces (platform/include/port/thor_world.h), registered the same way. */
    static native int nativeWorldState(java.nio.ByteBuffer out, int have);
    /** The Galaxy settings menu's link to the game (platform/mobile/thor/ThorSettingsBridge.cpp). */
    static native void nativeMenuState(boolean open);
    static native void nativeMenuSet(int key, int value);
    static native void nativeMenuValues(int[] out);

    /** Whether the settings menu is up (the lower screen shows a note under it). */
    static volatile boolean settingsShowing;
    private GalaxySettingsDialog settings;

    /** Back or the gear (from the game's thread): shows the settings menu. */
    public static boolean openSettingsMenu() {
        GalaxyActivity current = instance;
        if (current == null) return false;
        current.runOnUiThread(current::showSettings);
        return true;
    }

    private void showSettings() {
        if (settings != null || leaving || isFinishing()) return;
        settings = new GalaxySettingsDialog(this);
        try { settings.show(); } catch (WindowManager.BadTokenException failure) { settings = null; }
    }

    void settingsClosed(GalaxySettingsDialog dialog) {
        if (settings == dialog) settings = null;
    }

    private GalaxyLowerScreen lowerView;

    // Named and package-private: this d8 rejects the synthetic classes javac 21 makes at -source 8.
    static final class LowerSource implements GalaxyLowerScreen.Source {
        @Override public int copy(java.nio.ByteBuffer out) { return nativeLowerState(out); }
        @Override public int space(java.nio.ByteBuffer out, int have, int[] mode) { return nativeSpace(out, have, mode); }
        @Override public int world(java.nio.ByteBuffer out, int have) { return nativeWorldState(out, have); }
    }

    @Override public void onResume() { super.onResume(); resumed = true; showLower(); }
    @Override public void onPause() { resumed = false; closeLower(); super.onPause(); }
    private void closeLower() {
        if (lowerView != null) { lowerView.stop(); lowerView = null; }
        if (lower != null) { lower.dismiss(); lower = null; }
    }
    private void showLower() {
        if (!resumed || displays == null || leaving) return;
        Display target = null;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.isValid() && display.getDisplayId() != getWindowManager().getDefaultDisplay().getDisplayId()) {
                target = display;
                if ("Screen-2".equals(display.getName())) break;
            }
        }
        if (target == null) { closeLower(); return; }
        if (lower != null && lower.getDisplay().getDisplayId() == target.getDisplayId()) return;
        closeLower();
        lower = new Presentation(this, target);
        // The observatory field guide: the galaxy's star chart / the Observatory's ledger, from the
        // game's own state; the owner's art is staged privately in files/ThorHud.
        java.io.File art = new java.io.File(getExternalFilesDir(null), "GalaxyData/ThorHud");
        lowerView = new GalaxyLowerScreen(lower.getContext(), art, new LowerSource());
        lowerView.setBackgroundColor(0xff090b2a);
        lower.setContentView(lowerView.layered());
        // Touchable (the planet turns under a finger) but never focusable: the controller stays the game's.
        if (lower.getWindow() != null) lower.getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
        try {
            lower.show();
            lowerView.start();
            if (lower.getWindow() != null) {
                lower.getWindow().getDecorView().setSystemUiVisibility(5894);
                if (lower.getWindow().getInsetsController() != null)
                    lower.getWindow().getInsetsController().hide(android.view.WindowInsets.Type.systemBars());
            }
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException failure) { closeLower(); }
    }
    @Override public void onDisplayAdded(int id) { showLower(); }
    @Override public void onDisplayRemoved(int id) { showLower(); }
    @Override public void onDisplayChanged(int id) { showLower(); }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            // Back opens Galaxy's settings menu; its "Return to WiiDS" row leaves.
            if (event.getAction() == KeyEvent.ACTION_UP && event.getRepeatCount() == 0) showSettings();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    public static void returnToLauncher() {
        GalaxyActivity current = instance;
        if (current == null) return;
        current.runOnUiThread(() -> {
            if (current.leaving) return;
            current.leaving = true;
            try {
                current.startActivity(new Intent().setComponent(new ComponentName("io.github.maddex712.wiids",
                        "com.driftdroid.android.ModePickerActivity"))
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            } catch (android.content.ActivityNotFoundException ignored) { }
            current.finish();
        });
    }

    public static void triggerHaptic(int ms) { }
    public static void setPreferredRefreshRate(float rate) {
        GalaxyActivity current = instance;
        if (current == null) return;
        current.runOnUiThread(() -> {
            WindowManager.LayoutParams params = current.getWindow().getAttributes();
            params.preferredRefreshRate = 60f;
            current.getWindow().setAttributes(params);
        });
    }

    @Override public void onDestroy() {
        if (settings != null) { settings.dismiss(); settings = null; }
        settingsShowing = false;
        closeLower();
        if (displays != null) displays.unregisterDisplayListener(this);
        if (instance == this) instance = null;
        Thread exit = new Thread(() -> {
            try { Thread.sleep(2500); } catch (InterruptedException ignored) { }
            android.os.Process.killProcess(android.os.Process.myPid());
        }, "GalaxyExit");
        exit.setDaemon(true);
        exit.start();
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
