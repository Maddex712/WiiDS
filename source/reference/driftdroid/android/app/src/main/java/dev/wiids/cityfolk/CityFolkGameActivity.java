package dev.wiids.cityfolk;

import android.content.Intent;
import android.graphics.Bitmap;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
import com.driftdroid.android.dualscreen.WiiDsMenuView.Page;
import com.driftdroid.android.dualscreen.WiiDsScreens;
import dev.wiids.xd.sdl.SDLActivity;

public final class CityFolkGameActivity extends SDLActivity implements DisplayManager.DisplayListener {
    static final int A = 0x0800, B = 0x0400, PLUS = 0x0010, MINUS = 0x1000;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable reconcile = this::showCompanion;
    private DisplayManager displays;
    private CityFolkCompanion companion;
    private WiiDsMenuView menu;
    private CityFolkArt art;
    private View upperMenu;
    private Bitmap menuArt;
    private boolean active;
    private boolean leaving;
    private boolean openSavesOnExit;
    private volatile boolean nativeFinished;

    private static native void setButtons(int mask, boolean held, boolean touch);
    private static native void setAxes(float x, float y);
    private static native void setPointer(float x, float y, boolean valid);
    private static native void clearInput();
    private static native void setPaused(boolean paused);
    private static native void requestQuit();
    private static native int menuValue(boolean aspect);
    private static native void setMenuValue(boolean aspect, int value);
    private static native void setLowerOwnsMap(boolean owns);
    private static native void setLayoutLog(boolean enabled);

    private static native void setLowerOwnsPockets(boolean owns);
    private static final boolean POCKETS_ON_LOWER = !"0".equals(WiiDsDebug.property("debug.wiids.cf.lowerpockets"));

    /** The game's own Map screen is drawn on the upper display unless the lower display shows the map. */
    private void mapOwner(boolean lower) {
        try { setLowerOwnsMap(lower && MAP_ON_LOWER); } catch (UnsatisfiedLinkError older) { /* runtime without the switch */ }
        try { setLowerOwnsPockets(lower && POCKETS_ON_LOWER); } catch (UnsatisfiedLinkError older) { /* runtime without the switch */ }
    }
    private static final boolean MAP_ON_LOWER = !"0".equals(WiiDsDebug.property("debug.wiids.cf.lowermap"));


    @Override protected String[] getLibraries() { return new String[]{"SDL3", "wiids_city_folk"}; }
    @Override protected dev.wiids.xd.sdl.SDLSurface createSDLSurface(android.content.Context context) {
        return new CityFolkSurface(context);
    }
    @Override protected String getMainFunction() { return "WiiDsCityFolkMain"; }
    @Override protected String[] getArguments() {
        return new String[]{CityFolkStorage.game(this).getAbsolutePath(),
                CityFolkStorage.root(this).getAbsolutePath(),
                CityFolkStorage.saves(this).getAbsolutePath(),
                CityFolkStorage.cache(this).getAbsolutePath()};
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (mBrokenLibraries) return;
        com.driftdroid.android.dualscreen.WiiDsFullscreen.apply(this);
        menuArt = WiiDsMenuArt.cityFolk(CityFolkStorage.game(this));
        upperMenu = new com.driftdroid.android.dualscreen.WiiDsEntryKey(this, new TownSkin(), this::openMenu);
        art = CityFolkArt.open(CityFolkStorage.game(this));
        menu = new WiiDsMenuView(this, new TownSkin(), "Animal Crossing", menuArt, this::menuClosed);
        RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(
                WiiDsGameMenu.dp(this, 132), RelativeLayout.LayoutParams.WRAP_CONTENT);
        position.addRule(RelativeLayout.ALIGN_PARENT_END);
        position.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        position.setMargins(0, WiiDsGameMenu.dp(this, 12), WiiDsGameMenu.dp(this, 12), 0);
        position.height = WiiDsGameMenu.dp(this, 52);
        mLayout.addView(upperMenu, position);
        displays = getSystemService(DisplayManager.class);
        if (displays != null) displays.registerDisplayListener(this, ui);
    }

    @Override protected void main() {
        java.io.File cache = CityFolkStorage.cache(this);
        boolean prepared = CityFolkStorage.ready(this) && (cache.isDirectory() || cache.mkdirs());
        int result = prepared ? nativeRunMain(getMainSharedObject(), getMainFunction(), getArguments()) : 1;
        nativeFinished = true;
        if (result != 0) runOnUiThread(() -> Toast.makeText(this,
                "Animal Crossing could not start. Check the disc under Your games.", Toast.LENGTH_LONG).show());
    }

    @Override protected void onNativeMainFinished() {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            Intent library = new Intent(this, LibraryActivity.class).addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            if (openSavesOnExit) library.putExtra(LibraryActivity.EXTRA_SAVES_GAME, CityFolkStorage.ID);
            startActivity(library, WiiDsScreens.launchOptions(this, library, null));
            finish();
        });
    }

    @Override public void onResume() {
        super.onResume();
        if (mBrokenLibraries) return;
        active = true;
        if (!leaving) setPaused(menu.isOpen());
        showCompanion();
        if (menu.isOpen()) hostMenu();
    }

    @Override protected void onPause() {
        active = false;
        ui.removeCallbacks(reconcile);
        if (!mBrokenLibraries) {
            clearInput();
            setPaused(true);
        }
        dismissCompanion(); // an open menu keeps its page and is hosted again on resume
        if (menu != null) menu.detach();
        super.onPause();
    }

    @Override protected void onDestroy() {
        active = false;
        ui.removeCallbacksAndMessages(null);
        if (displays != null) displays.unregisterDisplayListener(this);
        dismissCompanion();
        super.onDestroy();
        if (isFinishing() && (nativeFinished || mBrokenLibraries))
            android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override public void onBackPressed() {
        if (mBrokenLibraries) super.onBackPressed();
        else openMenu();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) com.driftdroid.android.dualscreen.WiiDsFullscreen.apply(this);
        WindowInsetsController bars = getWindow().getInsetsController();
        if (focused && bars != null) {
            bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            bars.hide(WindowInsets.Type.systemBars());
        }
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (mBrokenLibraries) return super.dispatchKeyEvent(event);
        int key = event.getKeyCode();
        // The menu never takes window focus; controller input is handed to it here.
        if (menu.isOpen()) return menu.key(event) || super.dispatchKeyEvent(event);
        if (leaving) return true;
        if (key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_ESCAPE ||
                key == KeyEvent.KEYCODE_BUTTON_MODE || key == KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (event.getAction() == KeyEvent.ACTION_UP) openMenu();
            return true;
        }
        if (!acceptingInput()) return super.dispatchKeyEvent(event);
        int mask = buttonMask(key);
        if (mask == 0) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN || event.getAction() == KeyEvent.ACTION_UP)
            setButtons(mask, event.getAction() == KeyEvent.ACTION_DOWN, false);
        return true;
    }

    private static int buttonMask(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_ENTER: return A;
            case KeyEvent.KEYCODE_BUTTON_B: return B;
            case KeyEvent.KEYCODE_BUTTON_X: return MINUS;
            case KeyEvent.KEYCODE_BUTTON_Y: case KeyEvent.KEYCODE_BUTTON_START: return PLUS;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 0x0001;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 0x0002;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 0x0004;
            case KeyEvent.KEYCODE_DPAD_UP: return 0x0008;
            case KeyEvent.KEYCODE_BUTTON_L1: return 0x2000;
            case KeyEvent.KEYCODE_BUTTON_R1: return 0x4000;
            case KeyEvent.KEYCODE_BUTTON_L2: return 0x0200;
            case KeyEvent.KEYCODE_BUTTON_R2: return 0x0100;
            default: return 0;
        }
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (!mBrokenLibraries && menu.isOpen()) menu.stick(event);
        if (!mBrokenLibraries && acceptingInput() && event.getActionMasked() == MotionEvent.ACTION_MOVE &&
                event.isFromSource(android.view.InputDevice.SOURCE_JOYSTICK)) {
            setAxes(axis(event, MotionEvent.AXIS_X), -axis(event, MotionEvent.AXIS_Y));
            // SDL also receives the joystick. Its right-stick pointer, gyro and rumble still work.
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private static float axis(MotionEvent event, int axis) {
        float value = event.getAxisValue(axis);
        return Math.abs(value) < 0.15f ? 0 : value;
    }

    private boolean acceptingInput() { return active && !menu.isOpen() && !leaving; }

    void touchButton(int mask, boolean held) {
        if (!held || acceptingInput()) setButtons(mask, held, true);
    }

    void aim(float x, float y, boolean visible) {
        if (!visible || acceptingInput()) setPointer(x, y, visible);
    }

    void openMenu() {
        if (!acceptingInput() || mBrokenLibraries) return;
        // One pass on the UI thread: guest input released, guest paused, upper entry hidden, menu shown.
        clearInput();
        setPaused(true);
        if (companion != null) companion.releaseTouches();
        hostMenu();
        menu.show(root());
        refreshUpperEntry();
    }

    Bitmap menuArt() { return menuArt; }
    CityFolkArt art() { return art; }

    /** The lower display owns the menu whenever it is up; otherwise the menu covers the paused game. */
    private void hostMenu() {
        android.view.ViewGroup lower = companion == null ? null : companion.menuLayer();
        if (lower != null) menu.attach(lower, false);
        else menu.attach(mLayout, true);
    }

    private void menuClosed() {
        menu.detach();
        if (active && !leaving) setPaused(false);
        refreshUpperEntry();
    }

    private void refreshUpperEntry() {
        upperMenu.setVisibility(companion == null && !menu.isOpen() ? View.VISIBLE : View.GONE);
    }

    private void show(Page page) {
        if (active && menu.isOpen() && !isFinishing()) menu.show(page);
    }

    private Page root() {
        return new Page("Paused")
                .tile("Resume", "play", menu::close)
                .tile("Display", "display", () -> show(display()))
                .tile("Audio", "audio", () -> show(audio()))
                .tile("Controls", "pad", () -> show(controls()))
                .tile("Saves", "save", () -> show(saves()))
                .dangerTile("Return to WiiDS", "exit", () -> show(exit()));
    }

    private Page starting() {
        Runnable back = () -> show(root());
        return new Page("Starting").back(back).note("Animal Crossing is still starting.").key("Back", "back", back);
    }

    private Page display() {
        if (menuValue(true) < 0) return starting();
        // Aspect modes in the runtime: 0 = 4:3, 1 = Auto, 2 = 16:9.
        String[] ratios = {"4:3", "Auto", "16:9"};
        Runnable back = () -> show(root().focus(1));
        return new Page("Display").back(back)
                .row("Aspect ratio", () -> { int value = menuValue(true); return value >= 0 && value < ratios.length ? ratios[value] : ratios[1]; },
                        step -> { int value = menuValue(true); setMenuValue(true, Math.floorMod((value < 0 || value > 2 ? 1 : value) + step, 3)); })
                .key("Back", "back", back);
    }

    private Page audio() {
        if (menuValue(false) < 0) return starting();
        Runnable back = () -> show(root().focus(2));
        return new Page("Audio").back(back)
                .row("Master volume", () -> Math.max(0, menuValue(false)) + "%",
                        step -> setMenuValue(false, Math.max(0, Math.min(100, (Math.max(0, menuValue(false)) + 5) / 10 * 10 + step * 10))))
                .key("Back", "back", back);
    }

    private Page controls() {
        Runnable back = () -> show(root().focus(3));
        return new Page("Controls").back(back)
                .note("Left stick moves. Aim with the right stick or the lower screen. A selects, B goes back, "
                        + "X opens Pockets, and Y opens Map. Back or Select opens WiiDS.")
                .key("Back", "back", back);
    }

    private Page saves() {
        Runnable back = () -> show(root().focus(4));
        return new Page("Saves").back(back)
                .note("Save inside the game. Save Manager backs up or restores your town.")
                .key("Back", "back", back)
                .primaryKey("Open Save Manager", "save", () -> {
                    Runnable cancel = () -> show(saves());
                    show(new Page("Leave the game?").back(cancel)
                            .note("Save Manager opens after the game closes. Save in the game first to keep your progress.")
                            .key("Back", "back", cancel)
                            .primaryKey("Open Save Manager", "save", () -> {
                                openSavesOnExit = true;
                                leaveGame();
                            }));
                });
    }

    private Page exit() {
        return new Page("Return to WiiDS?").back(() -> show(root().focus(5)))
                .note("Only progress you’ve saved in the game will carry over.")
                .key("Keep playing", "play", menu::close)
                .primaryKey("Return to WiiDS", "exit", this::leaveGame);
    }

    private void leaveGame() {
        if (menu.isOpen()) menu.show(new Page("Closing").note("Returning to WiiDS…"));
        leaving = true;
        clearInput();
        requestQuit();
    }

    private void showCompanion() {
        if (!active || isFinishing() || displays == null) return;
        Display display = WiiDsDebug.singleScreen() ? null : WiiDsDisplays.companion(this, displays);
        if (WiiDsDebug.displayTest()) { ui.removeCallbacks(reconcile); ui.postDelayed(reconcile, 500); }
        if (display == null) {
            dismissCompanion();
            return;
        }
        if (companion != null && companion.isShowing() &&
                companion.getDisplay().getDisplayId() == display.getDisplayId()) return;
        dismissCompanion();
        CityFolkCompanion next = new CityFolkCompanion(this, display);
        companion = next;
        next.setOnDismissListener(dialog -> {
            if (companion != next) return;
            companion = null;
            if (menu.isOpen() && active) hostMenu();
            refreshUpperEntry();
            ui.removeCallbacks(reconcile);
            if (active) ui.postDelayed(reconcile, 250);
        });
        try {
            next.show();
            mapOwner(true);
            if ("1".equals(WiiDsDebug.property("debug.wiids.cf.lytlog"))) try { setLayoutLog(true); } catch (UnsatisfiedLinkError older) { /* no log */ }
            if (menu.isOpen()) hostMenu();
            refreshUpperEntry();
        } catch (WindowManager.InvalidDisplayException | WindowManager.BadTokenException | SecurityException unavailable) {
            dismissCompanion();
        }
    }

    private void dismissCompanion() {
        CityFolkCompanion current = companion;
        companion = null;
        if (current != null && !mBrokenLibraries) mapOwner(false);
        if (current != null) {
            // The menu leaves the lower window before that window goes away, and lands on the game display.
            if (menu.isOpen() && active) menu.attach(mLayout, true);
            else if (menu.isOpen()) menu.detach();
            current.setOnDismissListener(null);
            current.dismiss();
        }
        if (upperMenu != null && menu != null) refreshUpperEntry();
    }

    @Override public void onDisplayAdded(int id) { showCompanion(); }
    @Override public void onDisplayChanged(int id) { showCompanion(); }
    @Override public void onDisplayRemoved(int id) { showCompanion(); }
}
