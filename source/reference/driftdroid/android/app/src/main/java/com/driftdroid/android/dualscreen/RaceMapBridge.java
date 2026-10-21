package com.driftdroid.android.dualscreen;

/** Snapshot from the guest thread. No Android thread reads guest memory directly. */
public final class RaceMapBridge {
    public static final int HEADER = 8, STRIDE = 8, CAPACITY = 12;
    public static final int SIZE = HEADER + STRIDE * CAPACITY;
    private RaceMapBridge() {}
    public static native void nativeEnable(boolean enabled);
    public static native void nativeMapVisible(boolean visible);
    public static native boolean nativeRead(float[] output);
    /** RR archive basename:SHA256 captured with the preceding nativeRead, or empty. */
    public static native String nativeCourseIdentity();
}
