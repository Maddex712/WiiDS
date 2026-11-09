package dev.thor.mkdd;

import android.app.NativeActivity;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.InputDevice;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Double Dash on the Thor: the game draws on this window (upper display, native flat_main.cpp),
 * the duo screen on the lower display. Its files are in the external files dir: game/game.iso
 * (WiiDS; the lab keeps game.iso flat), saves/memcard_a.raw, save-backups/, shaders.bin and
 * lower-art/ (MkddArt or design/stage_art.py output). WiiDsMkddGameActivity scopes that dir to mkdd/.
 *
 * Back opens the WiiDS menu (DuoMenu) over the frozen game.
 */
public class MkddGameActivity extends NativeActivity implements DuoMenu.Host, InputManager.InputDeviceListener {
    private LowerScreenController lower;
    private MkddSettings settings;
    private MkddMenuDialog menu;
    private DuoArt menuArt;
    private File art;
    private String savesNote;
    private int runningScale;
    private InputManager inputManager;

    @Override protected void onCreate(Bundle state) {
        settings = new MkddSettings(this);
        runningScale = settings.scale();
        // Read once by flat_main.cpp at start (gx::render_init).
        try { Os.setenv("GCN_SCALE", Integer.toString(runningScale), true); }
        catch (ErrnoException e) { Log.w(DuoArt.TAG, "Render scale not set", e); }
        // Before the game opens its memory card: a restore asked for in the menu, then this start's backup.
        try { MkddSaves.prepare(this); }
        catch (IOException | RuntimeException e) { Log.w(DuoArt.TAG, "Memory card backup skipped", e); }
        // Loaded here first so Java's JNI lookup finds MkddNative in the same library
        // NativeActivity then runs (it gets the already-loaded handle).
        System.loadLibrary(gameLibrary());
        super.onCreate(state);
        inputManager = getSystemService(InputManager.class);
        inputManager.registerInputDeviceListener(this, null);
        refreshControllers();
        MkddNative.setStandardLayout(settings.standardLayout());
        MkddNative.setWidescreen(settings.widescreen());
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        fullscreen();
        art = new File(getExternalFilesDir(null), "lower-art");
        File committedArt = new File(getExternalFilesDir(null), "game/lower-art");
        if (committedArt.isDirectory()) art = committedArt;
        lower = new LowerScreenController(this, MkddNative::readLowerState,
                context -> new DoubleDashLowerView(context, art));
        // The menu's lettering, ready before the first Back (about 100 small PNGs).
        new Thread(() -> {
            DuoArt loaded = DuoArt.load(art);
            runOnUiThread(() -> { if (menuArt == null) menuArt = loaded; });
        }, "MkddMenuArt").start();
    }

    private void fullscreen() {
        getWindow().setDecorFitsSystemWindows(false);
        WindowInsetsController insets = getWindow().getInsetsController();
        if (insets != null) {
            insets.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            insets.hide(WindowInsets.Type.systemBars());
        }
    }

    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) fullscreen();
    }

    @Override protected void onResume() { super.onResume(); lower.resume(); }
    @Override protected void onPause() { lower.pause(); super.onPause(); }
    @Override protected void onDestroy() {
        inputManager.unregisterInputDeviceListener(this);
        if (menu != null) menu.dismiss();
        lower.close();
        super.onDestroy();
    }

    @Override public void onInputDeviceAdded(int deviceId) { refreshControllers(); }
    @Override public void onInputDeviceRemoved(int deviceId) { refreshControllers(); }
    @Override public void onInputDeviceChanged(int deviceId) { refreshControllers(); }

    private void refreshControllers() {
        int[] ids = inputManager.getInputDeviceIds();
        Arrays.sort(ids);
        int[] controllers = new int[ids.length + 1];
        int count = 1;
        controllers[0] = -1;
        for (int id : ids) {
            InputDevice device = inputManager.getInputDevice(id);
            if (device == null || device.isVirtual() || !device.isEnabled()
                    || (!device.supportsSource(InputDevice.SOURCE_GAMEPAD)
                        && !device.supportsSource(InputDevice.SOURCE_JOYSTICK))) continue;
            // The Thor exposes its built-in controls as an external Odin Controller.
            if (controllers[0] < 0 && ("Odin Controller".equals(device.getName()) || !device.isExternal()))
                controllers[0] = id;
            else controllers[count++] = id;
        }
        MkddNative.setControllerDevices(Arrays.copyOf(controllers, count));
    }

    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() { openMenu(); }

    private void openMenu() {
        if (menu != null || isFinishing()) return;
        MkddNative.setMenuOpen(true);
        lower.hold(true);
        // menuArt is null while the art is still building: the menu falls back to plain type.
        savesNote = null;
        menu = new MkddMenuDialog(this, new DuoMenu(this, menuArt, this));
        menu.show();
    }

    private void closeMenu() {
        if (menu == null) return;
        menu.dismiss();
        menu = null;
        lower.hold(false);
        MkddNative.setMenuOpen(false);
    }

    // ---- the menu's host ----

    @Override public int get(int key) {
        switch (key) {
            case DuoMenu.LAYOUT: return settings.standardLayout() ? 1 : 0;
            case DuoMenu.SCALE: return settings.scale() - MkddSettings.MIN_SCALE;
            case DuoMenu.ASPECT: return settings.widescreen() ? 1 : 0;
            default: return 0;
        }
    }

    @Override public void set(int key, int value) {
        switch (key) {
            case DuoMenu.LAYOUT:
                settings.setStandardLayout(value != 0);
                MkddNative.setStandardLayout(value != 0);
                break;
            case DuoMenu.SCALE: settings.setScale(value + MkddSettings.MIN_SCALE); break;
            case DuoMenu.ASPECT:
                settings.setWidescreen(value != 0);
                MkddNative.setWidescreen(value != 0);
                break;
            default: break;
        }
    }

    @Override public void act(int action) {
        if (action == DuoMenu.RESUME) closeMenu();
        else if (action == DuoMenu.LEAVE) leave();
        else if (action == DuoMenu.RESTART) restart();
        else if (action == DuoMenu.BACK_UP) {
            try { savesNote = MkddSaves.backUp(this) != null ? "Backed up just now." : "There's no memory card to back up yet."; }
            catch (IOException e) { savesNote = "The backup couldn't be saved. Check storage and try again."; }
        } else if (action >= DuoMenu.RESTORE) {
            List<File> backups = MkddSaves.list(this);
            int i = action - DuoMenu.RESTORE;
            if (i >= backups.size()) return;
            try {
                MkddSaves.requestRestore(this, backups.get(i));
                if (canRestart()) restart(); else leave();
            } catch (IOException e) { savesNote = "The restore couldn't be set up. Check storage and try again."; }
        }
    }

    @Override public boolean canRestart() { return false; }
    protected String gameLibrary() { return "wiids_mkdd_game"; }
    @Override public int runningScale() { return runningScale; }
    @Override public String leaveLabel() { return "Quit"; }
    @Override public String savesNote() { return savesNote; }
    @Override public List<String> backups() {
        List<String> out = new ArrayList<>();
        for (File b : MkddSaves.list(this)) out.add(MkddSaves.label(b));
        return out;
    }

    /**
     * Ends the game's process at once: the runtime writes the memory card through a temporary file
     * and a rename on every card write (gcn-recomp MemCard::flush), so nothing is left to save.
     */
    protected void leave() {
        finish();
        Process.killProcess(Process.myPid());
    }

    /** Starts the game again in a fresh process (WiiDS only; see WiiDsMkddGameActivity). */
    protected void restart() { leave(); }
}
