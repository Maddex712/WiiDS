package com.wither.nsmbw;

/**
 * The MSMBWii copies of the NSMB activities. The manifest gives them MSMBWii's own processes,
 * which is how the shared code knows to run the mod (see NsmbVariant).
 */
public final class MsmbwiiActivities {
    private MsmbwiiActivities() {}

    public static final class Launch extends ThorLaunchActivity {}
    public static final class Launcher extends LauncherActivity {}
    public static final class Game extends GameActivity {}
}
