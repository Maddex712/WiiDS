package dev.thor.mkdd;

import android.content.Context;
import android.content.SharedPreferences;

/** The WiiDS menu's Double Dash settings, in this profile's preferences (mkdd_settings inside WiiDS). */
final class MkddSettings {
    /** Render scale: multiples of the GameCube's 480 lines. Read by flat_main.cpp at start (GCN_SCALE). */
    static final int MIN_SCALE = 1, MAX_SCALE = 3, DEFAULT_SCALE = 2;
    private final SharedPreferences prefs;

    MkddSettings(Context context) { prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE); }

    int scale() {
        int s = prefs.getInt("render_scale", DEFAULT_SCALE);
        return s >= MIN_SCALE && s <= MAX_SCALE ? s : DEFAULT_SCALE;
    }
    void setScale(int scale) { prefs.edit().putInt("render_scale", scale).commit(); }  // the process may end next (Restart now)

    boolean widescreen() { return prefs.getBoolean("widescreen", false); }
    void setWidescreen(boolean on) { prefs.edit().putBoolean("widescreen", on).commit(); }

    /** Standard: face buttons as printed on the Thor. GameCube (default): as a GameCube pad sits. */
    boolean standardLayout() { return prefs.getBoolean("standard_layout", false); }
    void setStandardLayout(boolean standard) { prefs.edit().putBoolean("standard_layout", standard).commit(); }
}
