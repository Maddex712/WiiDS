package dev.wiids.brawl;

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
import java.io.File;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity running libmain.so (the translated game, its runtime, SDL and Dawn,
 * linked into one library), with the on-screen remote and the phone's motion on top.
 */
public final class GameActivity extends SDLActivity
        implements TouchControlsView.Listener, MotionInput.Listener {
    private static final String TAG = "NSMBW";

    /** Player 1's on-screen remote and motion (runtime/src/android/android_bridge.cpp). */
    static native void nativeSetTouchState(int buttons, boolean shake, float tilt);

    /** Immutable guest-thread snapshot for the lower display; no direct reads of guest RAM. */


    /** Brawl's match state for the Thor's lower screen (runtime/src/android/brawl_hud_state.cpp). */
    static native int[] nativeReadBrawlState();

    /** Waits (up to 5 s) for background save writes before the process ends (android_bridge.cpp). */
    static native boolean nativeFlushSaves();
    static native boolean nativeCommitGameSave();
    static native void nativeSetLowerPanels(boolean ready);

    // The WiiDS menu and its helpers (runtime/src/android/brawl_hud_state.cpp).
    /** The game's current scene ("scStrap", "scMelee", ...). */
    static native String nativeSceneName();
    /** Freezes the game (silent) at the end of its frame while the menu is open. */
    static native void nativeSetPaused(boolean paused);
    /** GameCube buttons (PAD_BUTTON_*) held on player 1's pad by the app; 0 lets go. */
    static native void nativeSetPadButtons(int buttons);
    /** Standard face buttons (else GameCube), L1 as a shield, rumble. */
    static native void nativeSetControls(boolean standardLayout, boolean l1Shield, boolean rumble);

    private static final int PAD_A = 0x0100, PAD_START = 0x1000;
    private BrawlMenu menu;
    private String savesNote;
    private final boolean runningProjectPlus[] = {false};

    private BrawlLowerScreen lowerScreen;

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
    private boolean nativeReady;
    private int touchButtons;
    private boolean touchShake, motionShake;
    private float motionTilt;

    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(new BrawlStorage(base)); }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        settings = new AppSettings(this);
        bootWidescreen = settings.aspectMode() != AppSettings.ASPECT_4_3;
        runningProjectPlus[0] = settings.projectPlusActive(this);
        // Before the game opens its save: a restore asked for in the menu, then this start's backup.
        try {
            if (!SaveBackups.applyPending(this)) SaveBackups.backUp(this, false);
        } catch (java.io.IOException failedRestore) {
            Log.e(TAG, "Save restore could not finish; game will not start", failedRestore);
            super.onCreate(savedInstanceState);
            Toast.makeText(this, "Could not restore the save. Your original save is preserved. "
                    + failedRestore.getMessage(), Toast.LENGTH_LONG).show();
            startActivity(new Intent(this, LauncherActivity.class)
                    .putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
            finish();
            return;
        }
        try {
            AppFiles.prepareRuntime(this);
            AppFiles.writeConfig(this, settings);
            // Read by runtime/src/platform/host_platform.cpp before anything else starts.
            Os.setenv("WIICOMPILED_RUNTIME_DIR", AppFiles.runtimeDir(this).getAbsolutePath(), true);
            Os.setenv("WIICOMPILED_DATA_DIR", AppFiles.dataDir(this).getAbsolutePath(), true);
            Os.setenv("NSMBW_THOR", BrawlBuild.THOR_BUILD ? "1" : "0", true);
        } catch (Exception e) {
            Log.e(TAG, "could not prepare the game's files", e);
            Toast.makeText(this, "Could not prepare the game's files: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        super.onCreate(savedInstanceState);
        nativeReady = !SDLActivity.mBrokenLibraries;
        if (nativeReady) {
            nativeSetDeviceName(deviceName());
            applyControls();
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
        if (nativeReady) lowerScreen = new BrawlLowerScreen(this);
    }

    /** SDL is linked into libmain.so, so that one library is all there is to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {"wiids_brawl_png16", "wiids_brawl", "wiids_brawl_jni"};
    }

    @Override protected String getMainSharedObject() { return getApplicationInfo().nativeLibraryDir + "/libwiids_brawl.so"; }

    // The game's menus take taps and the remote hides while they are up; it comes back a moment
    // after they go, so it doesn't blink while one menu screen hands over to the next.
    private final android.os.Handler menuPoll = new android.os.Handler(android.os.Looper.getMainLooper());
    private long lastTapScreen;
    private final Runnable pollMenu = new Runnable() {
        @Override
        public void run() {
            final long now = android.os.SystemClock.uptimeMillis();
            if (nativeReady) {
                // The game's settings window: keep its choices, the config is rewritten at each start.
                final float[] changed = nativeTakeSettings();
                if (changed != null) {
                    settings.setResolutionScale(changed[0]);
                    settings.setShowFps(changed[1] > 0.5f);
                    if (changed.length > 2) settings.setAspectMode(Math.round(changed[2]));
                    if (changed.length > 3) settings.setSpinningCoins(changed[3] > 0.5f);
                    if (changed.length > 4) settings.setFrameGraph(changed[4] > 0.5f);
                }
            }
            if (nativeReady) helpers(now);
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
        menuPoll.removeCallbacks(pollMenu);
        menuPoll.post(pollMenu);
        hideSystemBars();
        if (motion != null) {
            motion.start(settings.motionTilt(), settings.motionShake());
        }
        if (lowerScreen != null) lowerScreen.start();
    }

    @Override
    protected void onPause() {
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
        } else {
            if (touch != null) touch.release();  // a dialog or the shade took the screen: let go of every button
            autoPause();
        }
    }

    // --- helpers: the strap-screen skip and auto-pause -------------------------------------------

    private final android.os.Handler pad = new android.os.Handler(android.os.Looper.getMainLooper());
    private long strapSince, nextStrapPress;
    private int strapPresses;

    /** Holds `buttons` on player 1's pad for 120 ms (the game reads it like a real press). */
    private void press(int buttons) {
        try { nativeSetPadButtons(buttons); } catch (UnsatisfiedLinkError e) { return; }
        pad.removeCallbacksAndMessages(null);
        pad.postDelayed(() -> { try { nativeSetPadButtons(0); } catch (UnsatisfiedLinkError ignored) { } }, 120);
    }

    /**
     * Every 100 ms: on the wrist-strap screen (scStrap) and the title screen (scTitle), press A for the player a
     * moment after each appears, up to three times each, straight to the main menu.
     */
    private String introScene = "";

    private void helpers(long now) {
        if (menu != null || !settings.skipIntro()) return;
        String scene;
        try { scene = nativeSceneName(); } catch (UnsatisfiedLinkError e) { return; }
        if (!"scStrap".equals(scene) && !"scTitle".equals(scene)) { strapSince = 0; strapPresses = 0; introScene = ""; return; }
        if (!scene.equals(introScene)) { introScene = scene; strapSince = 0; strapPresses = 0; }
        if (strapSince == 0) { strapSince = now; nextStrapPress = now + 1500; }
        if (strapPresses < 3 && now >= nextStrapPress) {
            press(PAD_A);
            strapPresses++;
            nextStrapPress = now + 1500;
        }
    }

    /** Something else took the screen during a battle: pause it with Start, as the player would. */
    private void autoPause() {
        if (!nativeReady || menu != null || !settings.autoPause()) return;
        int[] state;
        try { state = nativeReadBrawlState(); } catch (UnsatisfiedLinkError e) { return; }
        if (state == null || state.length < BrawlLowerScreen.HEADER + 4 * BrawlLowerScreen.PER_PLAYER
                || state[1] != 1 || state[2] != 0) return;
        // Only a battle someone is playing: Start would end an attract-mode demo (all CPUs).
        for (int p = 0; p < 4; p++) {
            int o = BrawlLowerScreen.HEADER + p * BrawlLowerScreen.PER_PLAYER;
            if (state[o] == 1 && state[o + 8] == 0) { press(PAD_START); return; }
        }
    }

    private void applyControls() {
        try {
            nativeSetControls(settings.standardLayout(), settings.l1Shield(), settings.rumble());
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "controls bridge missing from libmain.so", e);
        }
    }

    @Override
    public void onBackPressed() {
        onMenu();
    }

    /** SDL takes the Back key as keyboard input before onBackPressed: the Back menu gets it first. */
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (menu != null) {
            // The open menu gets every key (the game is frozen under it).
            if (event.getAction() == android.view.KeyEvent.ACTION_DOWN) menu.onKeyDown(event.getKeyCode(), event);
            else if (event.getAction() == android.view.KeyEvent.ACTION_UP && !event.isCanceled()) menu.onKeyUp(event.getKeyCode(), event);
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

    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (menu != null) return menu.onGenericMotionEvent(event);
        return super.dispatchGenericMotionEvent(event);
    }

    /** "Start Menu": back to the launcher (its settings, Skip Start Menu); the game's process ends. */
    private void openStartMenu(boolean inSession) {
        if (inSession) {
            nativePrepareToLeave();  // the host keeps the session's progress
        }
        if (getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false)
                && ThorLaunchActivity.returnToLibrary(this)) {
            finish();
            return;
        }
        startActivity(new Intent(this, LauncherActivity.class)
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
            final Intent intent = new Intent(this, LauncherActivity.class)
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
        if (lowerScreen != null) lowerScreen.close();
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onDestroy();
        if (isFinishing()) {
            // Saves still being written in the background (hle/storage/nand_memfile.cpp) reach the disk first.
            if (nativeReady && !nativeFlushSaves()) {
                Log.e(TAG, "Keeping the save writer alive: storage still has pending writes");
                return;
            }
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

    /** Back: the WiiDS menu over the frozen game (BrawlMenu). */
    @Override
    public void onMenu() {
        if (touch != null) touch.release();
        if (menu != null || !nativeReady) return;
        try { nativeSetPaused(true); } catch (UnsatisfiedLinkError e) { return; }
        savesNote = null;
        menu = new BrawlMenu(this, new File(new File(AppFiles.dataDir(this), "ThorHud"), "brawl"), menuHost);
        addContentView(menu, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        menu.requestFocus();
        if (lowerScreen != null) lowerScreen.setMenuOpen(true);
    }

    private void closeMenu() {
        if (menu == null) return;
        ((ViewGroup) menu.getParent()).removeView(menu);
        menu = null;
        try { nativeSetPaused(false); } catch (UnsatisfiedLinkError ignored) { }
        if (lowerScreen != null) lowerScreen.setMenuOpen(false);
    }

    private static final float[] MENU_SCALES = {0f, 1f, 1.5f, 2f, 2.25f, 3f, 4f};
    private static final int[] MENU_ASPECTS = {AppSettings.ASPECT_16_9, AppSettings.ASPECT_4_3, AppSettings.ASPECT_FILL};

    private boolean saving;

    /** A full disk must not become a successful backup, restart or quit. Wait off the UI thread. */
    private void withFlushedSaves(Runnable after) {
        if (saving) return;
        saving = true;
        savesNote = "Finishing save...";
        if (menu != null) menu.setSavePending(true);
        new Thread(() -> {
            boolean ok;
            try { ok = nativeCommitGameSave() && nativeFlushSaves(); }
            catch (UnsatisfiedLinkError e) { ok = false; }
            final boolean complete = ok;
            runOnUiThread(() -> {
                saving = false;
                if (isFinishing() || isDestroyed()) return;
                if (menu != null) menu.setSavePending(false);
                if (complete) { savesNote = null; after.run(); }
                else {
                    savesNote = "The game could not finish saving. Check storage and try again.";
                    Toast.makeText(this, savesNote, Toast.LENGTH_LONG).show();
                }
                if (menu != null) menu.invalidate();
            });
        }, "Brawl-save-flush").start();
    }

    private final BrawlMenu.Host menuHost = new BrawlMenu.Host() {
        @Override public int get(int key) {
            switch (key) {
                case BrawlMenu.RESOLUTION: {
                    float scale = settings.resolutionScale();
                    int best = 0;
                    for (int i = 1; i < MENU_SCALES.length; i++)
                        if (Math.abs(MENU_SCALES[i] - scale) < Math.abs(MENU_SCALES[best] - scale)) best = i;
                    return best;
                }
                case BrawlMenu.ASPECT: {
                    for (int i = 0; i < MENU_ASPECTS.length; i++) if (MENU_ASPECTS[i] == settings.aspectMode()) return i;
                    return 0;
                }
                case BrawlMenu.SHOW_FPS: return settings.showFps() ? 1 : 0;
                case BrawlMenu.LAYOUT: return settings.standardLayout() ? 1 : 0;
                case BrawlMenu.L1_SHIELD: return settings.l1Shield() ? 1 : 0;
                case BrawlMenu.RUMBLE: return settings.rumble() ? 1 : 0;
                case BrawlMenu.MODE: return settings.projectPlus() ? 1 : 0;
                case BrawlMenu.SKIP_INTRO: return settings.skipIntro() ? 1 : 0;
                case BrawlMenu.AUTO_PAUSE: return settings.autoPause() ? 1 : 0;
                default: return 0;
            }
        }

        @Override public void set(int key, int value) {
            switch (key) {
                case BrawlMenu.RESOLUTION: settings.setResolutionScale(MENU_SCALES[value]); applyDisplay(); break;
                case BrawlMenu.ASPECT: settings.setAspectMode(MENU_ASPECTS[value]); applyDisplay(); break;
                case BrawlMenu.SHOW_FPS: settings.setShowFps(value != 0); applyDisplay(); break;
                case BrawlMenu.LAYOUT: settings.setStandardLayout(value != 0); applyControls(); break;
                case BrawlMenu.L1_SHIELD: settings.setL1Shield(value != 0); applyControls(); break;
                case BrawlMenu.RUMBLE: settings.setRumble(value != 0); applyControls(); break;
                case BrawlMenu.MODE: settings.setProjectPlus(value != 0); break;
                case BrawlMenu.SKIP_INTRO: settings.setSkipIntro(value != 0); break;
                case BrawlMenu.AUTO_PAUSE: settings.setAutoPause(value != 0); break;
                default: break;
            }
            try { AppFiles.writeConfig(GameActivity.this, settings); } catch (Exception ignored) { }
        }

        @Override public void act(int action) {
            if (action == BrawlMenu.RESUME) { closeMenu(); return; }
            if (action == BrawlMenu.LEAVE) {
                boolean session = getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false);
                withFlushedSaves(() -> {
                    closeMenu();
                    if (session) openStartMenu(nativeSessionActive()); else finish();
                });
                return;
            }
            if (action == BrawlMenu.RESTART) {
                withFlushedSaves(() -> { closeMenu(); restartForSession(); });
                return;
            }
            if (action == BrawlMenu.BACK_UP) {
                // The game is frozen at the end of a frame: its background save writes have finished or will land
                // whole (nand_memfile.cpp writes a scratch file and renames it).
                withFlushedSaves(() -> {
                    File made = SaveBackups.backUp(GameActivity.this, true);
                    savesNote = made != null ? "Backed up just now." : "Could not back up the save.";
                });
                return;
            }
            if (action >= BrawlMenu.RESTORE) {
                java.util.List<File> all = SaveBackups.list(GameActivity.this);
                int index = action - BrawlMenu.RESTORE;
                if (index < all.size()) {
                    final File selected = all.get(index);
                    withFlushedSaves(() -> {
                        SaveBackups.requestRestore(GameActivity.this, selected);
                        closeMenu();
                        restartForSession();  // applied before the game opens its save
                    });
                }
            }
        }

        @Override public String leaveLabel() {
            return getIntent().getBooleanExtra(ThorLaunchActivity.EXTRA_SESSION, false) ? "Return to WiiDS" : "Quit Game";
        }

        @Override public boolean projectPlusInstalled() { return AppSettings.projectPlusInstalled(GameActivity.this); }

        @Override public int runningMode() { return runningProjectPlus[0] ? 1 : 0; }

        @Override public java.util.List<String> backups() {
            java.util.List<String> labels = new java.util.ArrayList<>();
            for (File f : SaveBackups.list(GameActivity.this)) labels.add(SaveBackups.label(f));
            return labels;
        }

        @Override public String savesNote() { return savesNote; }
    };

    private void applyDisplay() {
        if (!nativeReady) return;
        nativeApplyDisplaySettings(settings.aspectMode(), settings.resolutionScale(), settings.showFps());
    }
}
