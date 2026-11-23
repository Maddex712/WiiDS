package dev.wiids.colosseum;

import android.content.Intent;
import android.graphics.Bitmap;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.Toast;

import com.driftdroid.android.LibraryActivity;
import com.driftdroid.android.dualscreen.WiiDsDebug;
import com.driftdroid.android.dualscreen.WiiDsDisplays;
import com.driftdroid.android.dualscreen.WiiDsGameMenu;
import com.driftdroid.android.dualscreen.WiiDsMenuArt;
import com.driftdroid.android.dualscreen.WiiDsMenuView;
import com.driftdroid.android.dualscreen.WiiDsScreens;
import dev.wiids.xd.sdl.SDLActivity;
import dev.wiids.xd.sdl.SDLSurface;

/** Runs the standalone Android host in its own WiiDS game process. */
public final class ColosseumGameActivity extends SDLActivity implements DisplayManager.DisplayListener {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable reconcileCompanion = this::showCompanion;
    private volatile boolean nativeFinished;
    private DisplayManager displays;
    private ColosseumPresentation companion;
    private WiiDsMenuView menu;
    private int settingsCheck;
    private View upperMenu;
    private Bitmap menuArt;
    private ColosseumTouchControls upperControls;
    private boolean upperTouchEnabled;
    private boolean resumed;
    private boolean quitRequested;
    private boolean openSavesOnExit;
    private boolean destroying;

    private static native boolean updateButton(int keyCode, boolean pressed);
    private static native void touchButton(int keyCode, boolean pressed);
    private static native void clearButtons();
    private static native void setExternalPause(boolean paused);
    private static native void requestQuit();
    private static native void setLowerOwnsPlayerStatus(boolean owns);
    private static native void setLowerOwnsBattleMenu(boolean owns);

    /** The upper player plates stop being drawn only while a lower display is there to show that status. */
    private void statusOwner(boolean lower) {
        try { setLowerOwnsPlayerStatus(lower && !"0".equals(WiiDsDebug.property("debug.wiids.colo.lowerstatus"))); }
        catch (UnsatisfiedLinkError older) { /* host without the switch */ }
        // The battle commands, moves and targets move to the lower screen with the status.
        try { setLowerOwnsBattleMenu(lower && !"0".equals(WiiDsDebug.property("debug.wiids.colo.lowermenu"))); }
        catch (UnsatisfiedLinkError older) { /* host without the battle menu feed */ }
    }

    @Override protected String[] getLibraries() {
        return new String[]{"SDL3", "wiids_colosseum_host"};
    }

    @Override protected SDLSurface createSDLSurface(android.content.Context context) {
        return new ColosseumSurface(context);
    }

    @Override protected String[] getArguments() {
        return new String[]{
            ColosseumStorage.iso(this).getAbsolutePath(),
            ColosseumStorage.module(this).getAbsolutePath(),
            ColosseumStorage.config(this).getAbsolutePath(),
            ColosseumStorage.card(this).getAbsolutePath()
        };
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (mBrokenLibraries) return;
        com.driftdroid.android.dualscreen.WiiDsFullscreen.apply(this);
        RelativeLayout layout = (RelativeLayout) mLayout;
        menuArt = WiiDsMenuArt.colosseum(ColosseumStorage.iso(this));
        upperMenu = new com.driftdroid.android.dualscreen.WiiDsEntryKey(this, new OrreSkin(), this::openSessionMenu);
        menu = new WiiDsMenuView(this, new OrreSkin(), "Pokémon Colosseum", menuArt, this::menuClosed);
        RelativeLayout.LayoutParams menuPosition = new RelativeLayout.LayoutParams(dp(132), dp(52));
        menuPosition.setMargins(0, dp(12), dp(12), 0);
        menuPosition.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        menuPosition.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        layout.addView(upperMenu, menuPosition);
        upperControls = new ColosseumTouchControls(this, this::sendTouchButton);
        RelativeLayout.LayoutParams controlsPosition = new RelativeLayout.LayoutParams(dp(116),
                RelativeLayout.LayoutParams.WRAP_CONTENT);
        controlsPosition.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        controlsPosition.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        layout.addView(upperControls, controlsPosition);
        refreshUpperControls();

        displays = getSystemService(DisplayManager.class);
        if (displays != null) displays.registerDisplayListener(this, ui);
    }

    private int dp(int dp) {
        return Math.round(dp * getResources().getDisplayMetrics().density);
    }

    @Override protected void main() {
        int result = nativeRunMain(getMainSharedObject(), getMainFunction(), getArguments());
        nativeFinished = true;
        if (result != 0) {
            runOnUiThread(() -> Toast.makeText(this, "Pokémon Colosseum could not start.", Toast.LENGTH_LONG).show());
        }
    }

    @Override protected void onNativeMainFinished() {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            if (quitRequested) {
                Intent library = new Intent(this, LibraryActivity.class).addFlags(
                        Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                if (openSavesOnExit) library.putExtra(LibraryActivity.EXTRA_SAVES_GAME, "pokemon-colosseum");
                startActivity(library, WiiDsScreens.launchOptions(this, library, null));
            }
            finish();
        });
    }

    @Override public void onResume() {
        super.onResume();
        if (mBrokenLibraries) return;
        resumed = true;
        if (!quitRequested) setExternalPause(menu.isOpen());
        showCompanion();
        if (menu.isOpen()) hostMenu();
    }

    @Override public void onBackPressed() {
        if (mBrokenLibraries) super.onBackPressed();
        else openSessionMenu();
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (mBrokenLibraries) return super.dispatchKeyEvent(event);
        int key = event.getKeyCode();
        int action = event.getAction();
        // The menu never takes window focus; controller input is handed to it here.
        if (menu.isOpen()) return menu.key(event) || super.dispatchKeyEvent(event);
        if (quitRequested) return true;
        if (key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_ESCAPE || key == KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (action == KeyEvent.ACTION_UP) openSessionMenu();
            return true;
        }
        return ((action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP)
            && updateButton(key, action == KeyEvent.ACTION_DOWN)) || super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (!mBrokenLibraries && menu.isOpen()) menu.stick(event);
        return super.dispatchGenericMotionEvent(event); // SDL keeps its axis state current while paused
    }

    void sendTouchButton(int key, boolean pressed) {
        if (mBrokenLibraries || (pressed && (!resumed || menu.isOpen() || quitRequested))) return;
        touchButton(key, pressed);
    }

    void openSessionMenu() {
        if (menu.isOpen() || !resumed || mBrokenLibraries || quitRequested || isFinishing()) return;
        // One pass on the UI thread: guest input released, guest paused, upper entry hidden, menu shown.
        clearButtons();
        setExternalPause(true);
        upperControls.releaseAll();
        if (companion != null) companion.releaseTouches();
        hostMenu();
        menu.show(ColosseumSettings.root(this));
        refreshUpperControls();
    }

    Bitmap menuArt() { return menuArt; }

    void showMenu(WiiDsMenuView.Page page) {
        if (resumed && menu.isOpen() && !isFinishing()) menu.show(page);
    }

    void closeMenu() { menu.close(); }

    /** The lower display owns the menu whenever it is up; otherwise the menu covers the paused game. */
    private void hostMenu() {
        android.view.ViewGroup lower = companion == null ? null : companion.menuLayer();
        if (lower != null) menu.attach(lower, false);
        else menu.attach((RelativeLayout) mLayout, true);
    }

    private void menuClosed() {
        ++settingsCheck;
        menu.detach();
        if (resumed && !quitRequested) setExternalPause(false);
        refreshUpperControls();
    }

    void checkSettingsSaved(Runnable checked) {
        final int check = ++settingsCheck;
        final long timeout = SystemClock.uptimeMillis() + 2500;
        ui.post(new Runnable() {
            @Override public void run() {
                if (check != settingsCheck || !menu.isOpen() || !resumed || quitRequested) return;
                if (ColosseumSettings.savePending() && SystemClock.uptimeMillis() < timeout) {
                    ui.postDelayed(this, 50);
                } else {
                    checked.run();
                    menu.refresh();
                }
            }
        });
    }

    void leaveGame() {
        if (menu.isOpen()) menu.show(ColosseumSettings.closing());
        quitRequested = true;
        clearButtons();
        requestQuit();
    }

    void leaveForSaveManager() {
        openSavesOnExit = true;
        leaveGame();
    }

    boolean hasCompanion() { return companion != null; }
    boolean hasUpperTouchControls() { return upperTouchEnabled; }

    void setUpperTouchControls(boolean enabled) {
        upperTouchEnabled = enabled;
        refreshUpperControls();
    }

    private void refreshUpperControls() {
        boolean upper = companion == null && !menu.isOpen();
        upperMenu.setVisibility(upper ? View.VISIBLE : View.GONE);
        upperControls.setVisibility(upper && upperTouchEnabled ? View.VISIBLE : View.GONE);
        if (!upper || !upperTouchEnabled) upperControls.releaseAll();
    }

    private void showCompanion() {
        if (!resumed || destroying || isFinishing() || displays == null) return;
        Display display = WiiDsDebug.singleScreen() ? null : WiiDsDisplays.companion(this, displays);
        if (WiiDsDebug.displayTest()) { ui.removeCallbacks(reconcileCompanion); ui.postDelayed(reconcileCompanion, 500); }
        if (display == null) {
            closeCompanion();
            return;
        }
        if (companion != null && companion.isShowing() &&
                companion.getDisplay().getDisplayId() == display.getDisplayId()) return;
        closeCompanion();
        ColosseumPresentation next = new ColosseumPresentation(this, display);
        companion = next;
        next.setOnDismissListener(ignored -> {
            if (companion != next) return;
            companion = null;
            if (menu.isOpen() && resumed) hostMenu();
            refreshUpperControls();
            ui.removeCallbacks(reconcileCompanion);
            if (resumed && !destroying && !isFinishing()) ui.postDelayed(reconcileCompanion, 250);
        });
        try {
            next.show();
            statusOwner(true);
            if (menu.isOpen()) hostMenu();
            refreshUpperControls();
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException | SecurityException unavailable) {
            closeCompanion();
        }
    }

    private void closeCompanion() {
        ColosseumPresentation previous = companion;
        companion = null;
        if (previous != null && !mBrokenLibraries) statusOwner(false);
        if (previous != null) {
            // The menu leaves the lower window before that window goes away, and lands on the game display.
            if (menu.isOpen() && resumed && !destroying) menu.attach((RelativeLayout) mLayout, true);
            else if (menu.isOpen()) menu.detach();
            previous.setOnDismissListener(null);
            previous.dismiss();
        }
        if (upperMenu != null) refreshUpperControls();
    }

    @Override public void onDisplayAdded(int id) { showCompanion(); }
    @Override public void onDisplayChanged(int id) { showCompanion(); }
    @Override public void onDisplayRemoved(int id) { showCompanion(); }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) com.driftdroid.android.dualscreen.WiiDsFullscreen.apply(this);
        WindowInsetsController bars = getWindow().getInsetsController();
        if (focused && bars != null) {
            bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            bars.hide(WindowInsets.Type.systemBars());
        }
    }

    @Override protected void onPause() {
        resumed = false;
        ui.removeCallbacks(reconcileCompanion);
        if (!mBrokenLibraries) {
            clearButtons();
            setExternalPause(true);
        }
        closeCompanion(); // an open menu keeps its page and is hosted again on resume
        if (menu != null) menu.detach();
        super.onPause();
    }

    @Override protected void onDestroy() {
        destroying = true;
        ui.removeCallbacksAndMessages(null);
        if (displays != null) displays.unregisterDisplayListener(this);
        closeCompanion();
        super.onDestroy();
        if (isFinishing() && (nativeFinished || mBrokenLibraries)) {
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
