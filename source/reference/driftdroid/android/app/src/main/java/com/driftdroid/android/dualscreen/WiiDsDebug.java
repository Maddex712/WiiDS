package com.driftdroid.android.dualscreen;

/** Test switches read from adb-settable system properties. They change nothing unless set. */
public final class WiiDsDebug {
    private WiiDsDebug() {}

    public static String property(String name) {
        try {
            return (String) Class.forName("android.os.SystemProperties").getMethod("get", String.class).invoke(null, name);
        } catch (ReflectiveOperationException | RuntimeException unavailable) {
            return "";
        }
    }

    /** {@code setprop debug.wiids.displaytest 1} before launch: games re-check their displays twice a second. */
    public static boolean displayTest() { return "1".equals(property("debug.wiids.displaytest")); }

    /** {@code setprop debug.wiids.single 1}: act as if the lower display were gone. */
    public static boolean singleScreen() { return "1".equals(property("debug.wiids.single")); }
}
