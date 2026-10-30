package dev.wiids.brawl;

import android.content.Context;
import android.content.SharedPreferences;

/** Player settings, kept in one SharedPreferences file shared by the launcher and the game. */
final class AppSettings {
    static final int ASPECT_4_3 = 0;
    static final int ASPECT_16_9 = 1;
    static final int ASPECT_FILL = 2;

    /** GPU driver workarounds: picked from the phone's GPU, or forced (AuroraGpuCompat). */
    static final int GPU_COMPAT_AUTO = 0;
    static final int GPU_COMPAT_ON = 1;
    static final int GPU_COMPAT_OFF = 2;

    /** Render scales offered in the menu; 1x renders 480 lines, whatever the aspect; 0 the screen's own. */
    static final float[] RESOLUTION_SCALES = {0f, 0.5f, 0.75f, 1f, 1.5f, 2f, 3f, 4f};

    private static final String FILE = "settings";
    private final SharedPreferences prefs;

    @SuppressWarnings("deprecation")
    AppSettings(Context context) {
        // Both processes (launcher and :game) read it. Writes use commit(): the game process is
        // ended right after its menu closes, which would drop a pending apply().
        // The retained launcher must reload commits made by :game before displaying or editing
        // them. Only the foreground activity edits settings; this flag requests the framework's
        // disk-change check when each AppSettings is constructed, rather than a stale cached copy.
        prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE | Context.MODE_MULTI_PROCESS);
    }

    /**
     * Project+ is installed: its card image (files/sd.raw) and codes (files/Codes/RSBE01.gct, with gc.txt) are in
     * the app's folder. The runtime loads them unless Config.toml says [mods] sd_codes = "vanilla".
     */
    static boolean projectPlusInstalled(Context context) {
        final java.io.File data = AppFiles.dataDir(context);
        return new java.io.File(data, "sd.raw").isFile() && new java.io.File(data, "Codes/RSBE01.gct").isFile();
    }

    /** The mode the player picked; vanilla Brawl until they choose Project+. */
    boolean projectPlus() { return prefs.getBoolean("project_plus", false); }
    void setProjectPlus(boolean on) { prefs.edit().putBoolean("project_plus", on).commit(); }

    /** What the next start plays: Project+ only when it is both chosen and installed. */
    boolean projectPlusActive(Context context) { return projectPlus() && projectPlusInstalled(context); }

    // The WiiDS menu's Controls and Game pages (BrawlMenu).
    /** Standard face buttons (B right, X left); otherwise the GameCube layout (B left, X right). */
    boolean standardLayout() { return prefs.getBoolean("standard_layout", false); }
    void setStandardLayout(boolean on) { prefs.edit().putBoolean("standard_layout", on).commit(); }
    boolean l1Shield() { return prefs.getBoolean("l1_shield", true); }
    void setL1Shield(boolean on) { prefs.edit().putBoolean("l1_shield", on).commit(); }
    boolean rumble() { return prefs.getBoolean("rumble", true); }
    void setRumble(boolean on) { prefs.edit().putBoolean("rumble", on).commit(); }
    /** Press A for the player on the wrist-strap screen and the title screen, straight to the main menu. */
    boolean skipIntro() { return prefs.getBoolean("skip_intro", true); }
    void setSkipIntro(boolean on) { prefs.edit().putBoolean("skip_intro", on).commit(); }
    /** Pause a battle when something else takes the screen (the WiiDS overlay, the shade, Home). */
    boolean autoPause() { return prefs.getBoolean("auto_pause", true); }
    void setAutoPause(boolean on) { prefs.edit().putBoolean("auto_pause", on).commit(); }

    int aspectMode() { return prefs.getInt("aspect_mode", BrawlBuild.THOR_BUILD ? ASPECT_16_9 : ASPECT_FILL); }
    void setAspectMode(int mode) { prefs.edit().putInt("aspect_mode", mode).commit(); }

    float resolutionScale() { return prefs.getFloat("resolution_scale", 1f); }
    void setResolutionScale(float scale) { prefs.edit().putFloat("resolution_scale", scale).commit(); }

    int gpuCompat() { return prefs.getInt("gpu_compat", GPU_COMPAT_AUTO); }
    void setGpuCompat(int mode) { prefs.edit().putInt("gpu_compat", mode).commit(); }

    boolean showFps() { return prefs.getBoolean("show_fps", false); }
    void setShowFps(boolean show) { prefs.edit().putBoolean("show_fps", show).commit(); }
    boolean spinningCoins() { return prefs.getBoolean("spinning_coins", true); }
    void setSpinningCoins(boolean on) { prefs.edit().putBoolean("spinning_coins", on).commit(); }

    boolean frameGraph() { return prefs.getBoolean("frame_graph", false); }
    void setFrameGraph(boolean on) { prefs.edit().putBoolean("frame_graph", on).commit(); }

    boolean touchControls() { return !BrawlBuild.THOR_BUILD && prefs.getBoolean("touch_controls", false); }  // Brawl: off by default (Thor pad reads as a GC pad)
    void setTouchControls(boolean on) { prefs.edit().putBoolean("touch_controls", on).commit(); }

    float touchOpacity() { return prefs.getFloat("touch_opacity", 0.6f); }
    void setTouchOpacity(float opacity) { prefs.edit().putFloat("touch_opacity", opacity).commit(); }

    boolean motionTilt() { return prefs.getBoolean("motion_tilt", !BrawlBuild.THOR_BUILD); }
    void setMotionTilt(boolean on) { prefs.edit().putBoolean("motion_tilt", on).commit(); }

    boolean motionShake() { return prefs.getBoolean("motion_shake", !BrawlBuild.THOR_BUILD); }
    void setMotionShake(boolean on) { prefs.edit().putBoolean("motion_shake", on).commit(); }

    boolean haptics() { return prefs.getBoolean("haptics", true); }
    void setHaptics(boolean on) { prefs.edit().putBoolean("haptics", on).commit(); }

    /** Open the game straight away when the app starts; the in-game menu leads back here. */
    boolean skipStartMenu() { return prefs.getBoolean("skip_start_menu", false); }
    void setSkipStartMenu(boolean on) { prefs.edit().putBoolean("skip_start_menu", on).commit(); }

    /** "2× (960p)": the scale and the number of lines it renders, as the menu shows it. */
    static String resolutionLabel(float scale) {
        if (scale <= 0f) return "Match screen";
        final String factor = scale == Math.rint(scale)
                ? String.valueOf((int) scale) : String.valueOf(scale);
        return factor + "× (" + Math.round(480 * scale) + "p)";
    }
}
