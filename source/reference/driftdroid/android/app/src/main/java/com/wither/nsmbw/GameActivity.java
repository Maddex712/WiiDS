package com.wither.nsmbw;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.os.Process;
import android.system.Os;
import android.util.Log;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity running libmain.so (the translated game, its runtime, SDL and Dawn,
 * linked into one library), with the on-screen remote and the phone's motion on top.
 */
public class GameActivity extends SDLActivity
        implements TouchControlsView.Listener, MotionInput.Listener {
    private static final String TAG = "NSMBW";

    /** Player 1's on-screen remote and motion (runtime/src/android/android_bridge.cpp). */
    static native void nativeSetTouchState(int buttons, boolean shake, float tilt);

    /** Immutable guest-thread snapshot for the lower display; no direct reads of guest RAM. */
    static native int[] nativeReadThorState();
    static native int[] nativeReadAchievementEvents(int cursor);
    private com.driftdroid.android.achievements.NsmbAchievementSession achievements;
    private com.driftdroid.android.achievements.AchievementPopup achievementPopup;
    static native int[] nativeReadThorWorldMap();
    static native int[] nativeReadThorReserve();
    static native void nativeUseThorReserve();

    /** The Display menu: aspect, render scale (480 lines per 1x), FPS counter (DisplaySettings). */
    static native void nativeApplyDisplaySettings(int aspect, float renderScale, boolean showFps);

    /** "GPU name\n1|0": the GPU the game draws with and whether its compatibility fixes are on. */
    static native String nativeGraphicsInfo();

    /** What this phone is called in LAN room lists (netplay). */
    static native void nativeSetDeviceName(String name);

    /** Whether this run is a LAN session, and leaving one (the host keeps the session's save). */
    static native boolean nativeSessionActive();
    static native void nativePrepareToLeave();
    /** A tap on the game's menus, as fractions of the screen. */
    static native void nativeTap(float x, float y);
    /** Whether a menu that taps drive is up (the remote is hidden then). */
    static native boolean nativeTapScreen();
    /** {render scale, show FPS, aspect} after the game's own settings window changed them, else null. */
    static native float[] nativeTakeSettings();

    private WifiManager.MulticastLock multicastLock;

    private AppSettings settings;
    private boolean bootWidescreen;  // the game reads 4:3 versus widescreen once, when it starts
    private TouchControlsView touch;
    private MotionInput motion;
    private boolean motionResumed;
    private boolean nativeReady;
    private ThorLowerScreen lowerScreen;
    private int touchButtons;
    private boolean touchShake, motionShake;
    private float motionTilt;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        settings = new AppSettings(this);
        bootWidescreen = settings.aspectMode() != AppSettings.ASPECT_4_3;
        try {
            AppFiles.prepareRuntime(this);
            AppFiles.writeConfig(this, settings);
            // Read by runtime/src/platform/host_platform.cpp before anything else starts.
            Os.setenv("WIICOMPILED_RUNTIME_DIR", AppFiles.runtimeDir(this).getAbsolutePath(), true);
            Os.setenv("WIICOMPILED_DATA_DIR", AppFiles.dataDir(this).getAbsolutePath(), true);
            Os.setenv("NSMBW_THOR", NsmbBuild.THOR_BUILD ? "1" : "0", true);
        } catch (Exception e) {
            Log.e(TAG, "could not prepare the game's files", e);
            Toast.makeText(this, "Could not prepare the game's files: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        super.onCreate(savedInstanceState);
        nativeReady = !SDLActivity.mBrokenLibraries;
        // WiiDS shows each game's RetroAchievements sets (imported progress); the native observer's
        // local rules are no longer recorded or shown.
        if (nativeReady) {
            nativeSetDeviceName(deviceName());
        }
        // LAN rooms are found with broadcasts, which many Wi-Fi drivers drop unless an app holds this.
        final WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("nsmbw-lan");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        touch = new TouchControlsView(this);
        touch.setListener(this);
        applyControlSettings();
        addContentView(touch, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        motion = new MotionInput(this, this);
        if (NsmbBuild.THOR_BUILD && nativeReady) lowerScreen = new ThorLowerScreen(this);
    }

    /** SDL is linked into libmain.so, so that one library is all there is to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {NsmbVariant.current().library};
    }

    // The game's menus take taps and the remote hides while they are up; it comes back a moment
    // after they go, so it doesn't blink while one menu screen hands over to the next.
    private final android.os.Handler menuPoll = new android.os.Handler(android.os.Looper.getMainLooper());
    private long lastTapScreen;
    private final Runnable pollMenu = new Runnable() {
        @Override
        public void run() {
            final long now = android.os.SystemClock.uptimeMillis();
            if (nativeReady) {
                if (achievements != null) achievements.accept(nativeReadAchievementEvents(achievements.cursor()));
                // The game's settings window: keep its choices, the config is rewritten at each start.
                final float[] changed = nativeTakeSettings();
                if (changed != null) {
                    settings.setResolutionScale(changed[0]);
                    settings.setShowFps(changed[1] > 0.5f);
                    if (changed.length > 2) settings.setAspectMode(Math.round(changed[2]));
                    if (changed.length > 3) settings.setSpinningCoins(changed[3] > 0.5f);
                    if (changed.length > 4) settings.setFrameGraph(changed[4] > 0.5f);
                    if (changed.length > 5 && Math.round(changed[5]) != settings.frameRate()) {
                        settings.setFrameRate(Math.round(changed[5]));
                        applyFrameRate();
                    }
                }
            }
            if (nativeReady && nativeTapScreen()) {
                lastTapScreen = now;
                touch.setMenuMode(true);
            } else if (now - lastTapScreen > 600) {
                touch.setMenuMode(false);
            }
            menuPoll.postDelayed(this, 100);
        }
    };

    @Override
    public void onTap(float x, float y) {
        if (nativeReady) nativeTap(x, y);
    }

    @Override
    protected void onResume() {
        super.onResume();
        motionResumed = true;
        if (achievementPopup != null) achievementPopup.resume();
        menuPoll.removeCallbacks(pollMenu);
        menuPoll.post(pollMenu);
        hideSystemBars();
        if (motion != null) {
            motion.start(settings.motionTilt(), settings.motionShake());
        }
        if (lowerScreen != null) lowerScreen.start();
        applyFrameRate();
        menuPoll.postDelayed(this::applyFrameRate, 1500);  // the game surface may not exist yet
    }

    /**
     * 120 FPS needs the panel in its 120 Hz mode: the window asks for the slowest mode that covers
     * the target (60 Hz for the original rate), and the game surface votes the same frame rate,
     * which some displays honour where the window request alone is ignored.
     */
    private void applyFrameRate() {
        final int target = settings.frameRate();
        final android.view.Display display = getWindowManager().getDefaultDisplay();
        final android.view.Display.Mode current = display.getMode();
        android.view.Display.Mode best = null, fastest = null;
        for (android.view.Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() != current.getPhysicalWidth() || mode.getPhysicalHeight() != current.getPhysicalHeight()) continue;
            if (fastest == null || mode.getRefreshRate() > fastest.getRefreshRate()) fastest = mode;
            if (mode.getRefreshRate() >= target - 1f && (best == null || mode.getRefreshRate() < best.getRefreshRate())) best = mode;
        }
        if (best == null) best = fastest;
        if (best != null) {
            final WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.preferredDisplayModeId = best.getModeId();
            attributes.preferredRefreshRate = best.getRefreshRate();
            getWindow().setAttributes(attributes);
        }
        final android.view.Surface surface = mSurface != null && mSurface.getHolder() != null ? mSurface.getHolder().getSurface() : null;
        if (surface != null && surface.isValid()) {
            try {
                if (Build.VERSION.SDK_INT >= 31) surface.setFrameRate(target, android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                        android.view.Surface.CHANGE_FRAME_RATE_ALWAYS);
                else surface.setFrameRate(target, android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
            } catch (RuntimeException unavailable) { Log.w(TAG, "Frame-rate vote unavailable", unavailable); }
        }
        Log.i(TAG, "Upper output target " + target + " Hz" + (best == null ? "" : " (mode " + best.getRefreshRate() + " Hz)"));
    }

    @Override
    protected void onPause() {
        motionResumed = false;
        if (achievementPopup != null) achievementPopup.pause();
        if (lowerScreen != null) lowerScreen.stop();
        menuPoll.removeCallbacks(pollMenu);
        if (motion != null) motion.stop();
        if (touch != null) touch.release();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
            if (motion != null && motionResumed) motion.start(settings.motionTilt(), settings.motionShake());
        } else if (touch != null) {
            touch.release();  // a dialog or the shade took the screen: let go of every button
            if (motion != null) motion.stop();
        }
    }

    @Override
    public void onBackPressed() {
        onMenu();
    }

    /** SDL takes the Back key as keyboard input before onBackPressed: the Back menu gets it first. */
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (NsmbBuild.THOR_BUILD && nativeReady
                && event.getKeyCode() == android.view.KeyEvent.KEYCODE_BUTTON_THUMBR) {
            if (event.getAction() == android.view.KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0)
                nativeUseThorReserve();
            return true;
        }
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP && !event.isCanceled()) {
                onMenu();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    /** "Start Menu": back to the launcher (its settings, Skip Start Menu); the game's process ends. */
    private void openStartMenu(boolean inSession) {
        if (inSession) {
            nativePrepareToLeave();  // the host keeps the session's progress
        }
        if (NsmbBuild.THOR_BUILD && getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false)
                && ThorLaunchActivity.returnToLibrary(this)) {
            finish();
            return;
        }
        startActivity(new Intent(this, NsmbVariant.current().launcherActivity)
                .putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true)
                .putExtra(ThorLaunchActivity.EXTRA_SESSION,
                        getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
        finish();
    }

    /** "Leave LAN Game": keep the host's progress, then start the game again on its own. */
    private void leaveSession() {
        nativePrepareToLeave();
        restartForSession();  // with the session file gone, the next start is an ordinary one
    }

    /** The name the user gave the phone, else its model. */
    private String deviceName() {
        String name = Settings.Global.getString(getContentResolver(), Settings.Global.DEVICE_NAME);
        if (name == null || name.trim().isEmpty()) {
            name = Build.MODEL;
        }
        return name == null ? "Phone" : name.trim();
    }

    /**
     * Network play: the runtime has written the session (netplay_start.cpp); start the game again in a
     * fresh process. The launcher waits for this one to end, then opens the game, which finds the
     * session and boots into it. Called from the game thread.
     */
    void restartForSession() {
        runOnUiThread(() -> {
            final Intent intent = new Intent(this, NsmbVariant.current().launcherActivity)
                    .putExtra(LauncherActivity.EXTRA_RESTART_GAME, true)
                    .putExtra(ThorLaunchActivity.EXTRA_SESSION,
                            getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
            finish();
        });
    }

    @Override
    protected void onDestroy() {
        if (achievements != null) { achievements.setListener(null); achievements.close(); }
        if (achievementPopup != null) { achievementPopup.close(); achievementPopup = null; }
        if (lowerScreen != null) lowerScreen.close();
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onDestroy();
        if (isFinishing()) {
            // The runtime starts once per process; the next start must get a fresh one.
            Process.killProcess(Process.myPid());
        }
    }

    // --- input ---------------------------------------------------------------------------------

    @Override
    public void onTouchInput(int buttons, boolean shake) {
        touchButtons = buttons;
        touchShake = shake;
        pushInput();
    }

    @Override
    public void onMotion(float tilt, boolean shake) {
        motionTilt = tilt;
        motionShake = shake;
        pushInput();
    }

    private void pushInput() {
        if (!nativeReady) return;
        try {
            nativeSetTouchState(touchButtons, touchShake || motionShake, motionTilt);
        } catch (UnsatisfiedLinkError e) {
            nativeReady = false;
            Log.e(TAG, "touch input bridge missing from libmain.so", e);
        }
    }

    private void applyControlSettings() {
        touch.setControlsVisible(settings.touchControls());
        touch.setOpacity(settings.touchOpacity());
        touch.setHaptics(settings.haptics());
    }

    private void hideSystemBars() {
        getWindow().setDecorFitsSystemWindows(false);
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    // --- menu ----------------------------------------------------------------------------------

    /**
     * The phone's Back gesture: back to the start menu (where the settings are), leave a LAN game,
     * or quit. There is no on-screen menu button; the game's own settings window (the gear on the
     * save file screen and the pause menus) covers resolution and the FPS counter while playing.
     */
    @Override
    public void onMenu() {
        touch.release();
        final boolean inSession = nativeReady && nativeSessionActive();
        final java.util.List<String> items = new java.util.ArrayList<>();
        final String menuLabel = NsmbBuild.THOR_BUILD
                && getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false)
                ? "Main Menu" : "Start Menu";
        items.add(menuLabel);
        if (NsmbBuild.THOR_BUILD) items.add("Achievements");
        if (inSession) items.add("Leave LAN Game");
        items.add("Quit Game");
        new AlertDialog.Builder(this)
                .setTitle("New Super Mario Bros. Wii")
                .setItems(items.toArray(new String[0]), (dialog, which) -> {
                    final String item = items.get(which);
                    if (item.equals(menuLabel)) openStartMenu(inSession);
                    else if (item.equals("Achievements")) startActivity(new Intent(this,
                        com.driftdroid.android.achievements.AchievementsActivity.class)
                        .putExtra(com.driftdroid.android.achievements.AchievementsActivity.EXTRA_GAME, NsmbVariant.current().id));
                    else if (item.equals("Leave LAN Game")) leaveSession();
                    else finish();
                })
                .setNegativeButton("Back to the Game", null)
                .show();
    }
}
