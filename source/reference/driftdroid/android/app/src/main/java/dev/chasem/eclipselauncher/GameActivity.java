package dev.chasem.eclipselauncher;

import android.os.Bundle;
import android.os.Build;
import android.os.PowerManager;
import android.app.GameManager;
import android.app.GameState;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.Display;
import java.io.File;
import java.util.ArrayList;
import org.libsdl.sdl2.app.SDLActivity;
import org.libsdl.sdl2.app.SDLSurface;

public class GameActivity extends SDLActivity {
    private final java.util.Set<Integer> assignedBackPresses = new java.util.HashSet<>();
    private volatile String displayRequest = "not requested";
    private volatile String gamePolicy = "not queried";
    private ThorSunshineScreen lowerScreen;
    private SharedPreferences settings() { return getSharedPreferences("eclipse_settings", MODE_PRIVATE); }
    @Override protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // Set before SDL starts its native thread, and again when the
                // surface is recreated. This is a display hint, not a CPU lock.
                // The panel follows the game's rate: 60 or 120 Hz (the Thor runs 60 by default, so
                // 120 FPS needs the request); 30 is asked for only with "Match display refresh".
                int fps = frameRate();
                if (Build.VERSION.SDK_INT >= 30 && (fps > 30 || settings().getBoolean("match_display_rate", false))) {
                    float rate = fps;
                    try {
                        holder.getSurface().setFrameRate(rate, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
                        displayRequest = "requested " + rate + " FPS";
                    } catch (RuntimeException e) { displayRequest = "rejected: " + e.getClass().getSimpleName(); }
                } else displayRequest = Build.VERSION.SDK_INT < 30 ? "unavailable" : "disabled";
                super.surfaceChanged(holder, format, width, height);
            }
        };
    }
    private void reportGamePolicy(boolean playing) {
        if (Build.VERSION.SDK_INT < 31) { gamePolicy = "GameManager unavailable"; return; }
        try {
            GameManager manager = getSystemService(GameManager.class);
            if (manager == null) { gamePolicy = "GameManager unavailable"; return; }
            int mode = manager.getGameMode();
            String name = mode == GameManager.GAME_MODE_PERFORMANCE ? "performance"
                : mode == GameManager.GAME_MODE_BATTERY ? "battery"
                : mode == GameManager.GAME_MODE_STANDARD ? "standard" : "unsupported";
            String state = "unavailable";
            if (Build.VERSION.SDK_INT >= 33) {
                // Sunshine is pausable; do not claim uninterruptible network
                // play or permanent loading to obtain an inappropriate boost.
                manager.setGameState(new GameState(false,
                    playing ? GameState.MODE_GAMEPLAY_INTERRUPTIBLE : GameState.MODE_NONE));
                state = playing ? "gameplay interruptible" : "none";
            }
            gamePolicy = "game mode " + name + ", reported state " + state;
        } catch (RuntimeException e) { gamePolicy = "GameManager rejected: " + e.getClass().getSimpleName(); }
        android.util.Log.i("SMSLauncher", gamePolicy);
    }
    private void traceLifecycle(String boundary) {
        Thread sdl = mSDLThread;
        android.util.Log.i("SMS-Thor", boundary + " pid=" + android.os.Process.myPid()
            + " finishing=" + isFinishing() + " configuration=" + isChangingConfigurations()
            + " SDLThread=" + (sdl == null ? "none" : sdl.getState()));
    }
    private int frameRate() {
        String value = settings().getString("frame_rate", "60");
        return "30".equals(value) ? 30 : "120".equals(value) ? 120 : 60;
    }
    /** Picks the panel mode for the game's rate: same size, the lowest refresh at or above it. */
    private void applyDisplayMode() {
        Display display = getWindow().getDecorView().getDisplay();
        if (display == null) display = getWindowManager().getDefaultDisplay();
        int target = Math.max(60, frameRate());
        Display.Mode current = display.getMode(), best = null;
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() != current.getPhysicalWidth() || mode.getPhysicalHeight() != current.getPhysicalHeight()) continue;
            if (mode.getRefreshRate() + 0.5f < target) continue;
            if (best == null || mode.getRefreshRate() < best.getRefreshRate()) best = mode;
        }
        if (best == null) return;
        android.view.WindowManager.LayoutParams params = getWindow().getAttributes();
        if (params.preferredDisplayModeId != best.getModeId()) {
            params.preferredDisplayModeId = best.getModeId();
            getWindow().setAttributes(params);
        }
        android.util.Log.i("SMS-Thor", "Panel mode for " + frameRate() + " FPS: " + best.getRefreshRate() + " Hz");
    }
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT < 30) return;
        android.view.WindowInsetsController bars = getWindow().getInsetsController();
        if (bars == null) return;
        bars.hide(android.view.WindowInsets.Type.systemBars());
        bars.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }
    @Override protected void onResume() { traceLifecycle("resume begin"); reportGamePolicy(true); super.onResume(); applyDisplayMode(); hideSystemBars(); if (lowerScreen != null) lowerScreen.start(); traceLifecycle("resume end"); }
    @Override protected void onPause() { traceLifecycle("pause begin"); if (lowerScreen != null) lowerScreen.stop(); super.onPause(); reportGamePolicy(false); traceLifecycle("pause end"); }
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "eclipsebootstrap"}; }
    @Override protected String[] getArguments() {
        File data = new File(ThorStorage.root(this), "game");
        ArrayList<String> args = new ArrayList<>();
        args.add("--data"); args.add(data.getAbsolutePath());
        args.add("--libraries"); args.add(getApplicationInfo().nativeLibraryDir);
        args.add("--disc"); args.add(ThorStorage.disc(this).getAbsolutePath());
        args.add("--env"); args.add("SMS_SAVE_DIR=" + new File(ThorStorage.root(this), "saves/card-a").getAbsolutePath());
        args.add("--env"); args.add("SMS_SETTINGS=" + new File(data, "settings.txt").getAbsolutePath());
        args.add("--env"); args.add("SMS_ANDROID_CONTROLLER_MAPPINGS=" + new File(ThorStorage.root(this), "controller-mappings.txt").getAbsolutePath());
        // The Thor's lower screen shows the Shine, blue-coin and coin counters (ThorSunshineScreen):
        // the top screen leaves them out and moves lives to its corner.
        if (getIntent().getBooleanExtra("thor_library", false) && ThorSunshineScreen.hasLowerPanel(this)) {
            args.add("--env"); args.add("SMS_THOR_SPLIT_HUD=1");
        }
        File probe = new File(data, "thor-probe-once");
        if (probe.isFile()) {
            if (probe.delete()) {
                args.add("--env"); args.add("SMS_THOR_CONTEXT_PROBE=1");
                android.util.Log.i("SMS-Thor", "Consumed one-session native context probe request");
            } else android.util.Log.w("SMS-Thor", "Could not consume context probe request; probe stays disabled");
        }
        PowerManager power = getSystemService(PowerManager.class);
        Display display = getWindow().getDecorView().getDisplay();
        args.add("--env"); args.add("SMS_ANDROID_STARTUP_POLICY=" + gamePolicy
            + "; " + BuildInfo.summary().replace('\n', ';')
            + ", battery saver " + (power != null && power.isPowerSaveMode())
            + ", display " + (display == null ? "unknown" : display.getRefreshRate() + " Hz")
            + ", " + displayRequest);
        if (BuildConfig.DEBUG) {
            if (getIntent().getBooleanExtra("gxSelftest", false)) { args.add("--gx-selftest"); args.add("1"); }
            String[] testEnvironment = getIntent().getStringArrayExtra("testEnvironment");
            if (testEnvironment != null) for (String value : testEnvironment) {
                if (value.startsWith("SMS_")) { args.add("--env"); args.add(value); }
            }
        }
        return args.toArray(new String[0]);
    }
    @Override protected void onCreate(Bundle state) {
        traceLifecycle("create begin");
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(state);
        // The Thor's lower panel shows the area, collection and FLUDD (ThorSunshineScreen).
        if (getIntent().getBooleanExtra("thor_library", false)) lowerScreen = new ThorSunshineScreen(this);
        if (android.os.Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::finish);
        traceLifecycle("create end");
    }
    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
            if(event.getAction()==android.view.KeyEvent.ACTION_DOWN&&ControllerProfiles.backAssigned(this,event.getDevice()))
                assignedBackPresses.add(event.getDeviceId());
            if(assignedBackPresses.contains(event.getDeviceId())) {
                if(event.getAction()==android.view.KeyEvent.ACTION_UP)assignedBackPresses.remove(event.getDeviceId());
                SDLActivity.handleKeyEvent(mSurface,event.getKeyCode(),event,null);
                return true;
            }
            if (event.getAction() == android.view.KeyEvent.ACTION_UP) finish();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
    @Override public void finish() {
        traceLifecycle("finish requested");
        if (getIntent().getBooleanExtra("thor_library", false)) ThorStorage.returnToLibrary(this);
        super.finish();
    }
    @Override protected void onDestroy() {
        if (lowerScreen != null) { lowerScreen.stop(); lowerScreen = null; }
        traceLifecycle("destroy begin");
        // SDL's super.onDestroy sends SDL_QUIT, then joins SDLThread without a
        // timeout. That thread waits for the native game thread, so a paused
        // renderer/event-pump shutdown can otherwise strand the UI in join()
        // and make the following Play wait forever in this old process.
        Thread main = android.os.Looper.getMainLooper().getThread();
        Thread sdl = mSDLThread;
        ThorSessionShutdown shutdown = new ThorSessionShutdown(2000, () -> {
            traceLifecycle("destroy exceeded 2000 ms; ending session");
            for (Thread thread : new Thread[]{main, sdl}) if (thread != null) {
                android.util.Log.e("SMS-Thor", "Teardown thread " + thread.getName() + " state=" + thread.getState());
                for (StackTraceElement frame : thread.getStackTrace()) android.util.Log.e("SMS-Thor", "  at " + frame);
            }
        }, this::terminateSessionProcess);
        try {
            super.onDestroy();
            traceLifecycle("destroy SDL cleanup returned");
        } finally {
            shutdown.close();
            // The game owns global heaps and SDK threads. Upstream already
            // terminates this process after teardown to keep each Play fresh.
            terminateSessionProcess();
        }
    }
    private void terminateSessionProcess() {
        String expected = getPackageName() + ":eclipse_game";
        String actual = android.app.Application.getProcessName();
        if (!expected.equals(actual)) {
            android.util.Log.e("SMS-Thor", "Refusing to terminate unexpected process " + actual);
            return;
        }
        android.util.Log.i("SMS-Thor", "Ending own session process pid=" + android.os.Process.myPid());
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
