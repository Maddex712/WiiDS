package dev.thor.mkdd;

/** Same-process bridge; MkddGameActivity loads wiids_mkdd_game before calling it. */
public final class MkddNative {
    private MkddNative() {}
    public static native int[] readLowerState();
    /** Active Android device IDs in initial port order. The runtime keeps existing assignments. */
    static native void setControllerDevices(int[] ids);
    /** Freezes the game at its next frame end (silent) while the WiiDS menu is open. */
    public static native void setMenuOpen(boolean open);
    /** 16:9 output: a wider view in 3D, the 2D HUD kept in proportion (the runtime's flat renderer). */
    public static native void setWidescreen(boolean on);
    /** Standard face buttons (as printed on the Thor), else GameCube positions. */
    public static native void setStandardLayout(boolean standard);
}
