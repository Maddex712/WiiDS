package com.wither.nsmbw;

import android.app.Activity;
import android.app.Application;

/**
 * WiiDS runs New Super Mario Bros. Wii and the MSMBWii mod from one copy of this code, each in
 * its own processes (":nsmbw"/":nsmbw_game" and ":msmbwii"/":msmbwii_game"), so the process
 * name says which one this is. Each has its own native library, saves and settings; both read
 * the same imported NSMB disc.
 */
enum NsmbVariant {
    NSMB("nsmbw", "nsmbw", GameActivity.class, LauncherActivity.class),
    MSMBWII("msmbwii", "msmbwii", MsmbwiiActivities.Game.class, MsmbwiiActivities.Launcher.class);

    /** Matches the launcher's thor_game id and names the data and runtime folders. */
    final String id;
    /** lib<library>.so */
    final String library;
    final Class<? extends Activity> gameActivity;
    final Class<? extends Activity> launcherActivity;

    NsmbVariant(String id, String library, Class<? extends Activity> game, Class<? extends Activity> launcher) {
        this.id = id;
        this.library = library;
        this.gameActivity = game;
        this.launcherActivity = launcher;
    }

    private static NsmbVariant current;

    static synchronized NsmbVariant current() {
        if (current == null) {
            final String process = Application.getProcessName();
            current = process != null && process.contains(":msmbwii") ? MSMBWII : NSMB;
        }
        return current;
    }
}
