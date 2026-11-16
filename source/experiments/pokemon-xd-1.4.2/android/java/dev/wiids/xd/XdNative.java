package dev.wiids.xd;

public final class XdNative {
    static { System.loadLibrary("wiids_xd_host"); }
    private XdNative() {}
    public static native int[] read();
    public static native boolean lowerTouch(float x,float y);
    public static native int fieldColor();
    public static native void lowerSurface(android.view.Surface surface,int width,int height);
    public static native void suspend();
    public static native boolean menuOpen(boolean open);
    public static native int[] runtimeRead();
    public static native boolean runtimeSet(int key,int value);
}
