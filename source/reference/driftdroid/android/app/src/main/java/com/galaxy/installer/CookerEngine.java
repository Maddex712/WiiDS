package com.galaxy.installer;

/**
 * The upstream installer's native entry points (reference/galaxy-android/installer-mobile/jni,
 * built into libinstaller.so by Build-Importer.py). The JNI names are bound to this package and
 * class, so it keeps upstream's name inside the WiiDS Galaxy add-on.
 */
public final class CookerEngine {
    private CookerEngine() {}

    /** Called from native threads; onLog/onProgress are looked up by name (bridge_common.cpp). */
    public interface CookerCallback {
        void onLog(String line);
        void onProgress(int percent, String message);
        void onFinished(boolean success, String message);
    }

    private static boolean loaded;

    /** Loads libinstaller (and with it the embedded Python); false when it is missing. */
    public static synchronized boolean load() {
        if (loaded) return true;
        try {
            System.loadLibrary("c++_shared");
            System.loadLibrary("gfortran");
            System.loadLibrary("openblas");
            System.loadLibrary("jpeg_chaquopy");
            System.loadLibrary("freetype");
            System.loadLibrary("installer");
            loaded = true;
        } catch (UnsatisfiedLinkError missing) {
            android.util.Log.e("GalaxyImport", "libinstaller.so unavailable", missing);
        }
        return loaded;
    }

    public static native int runCooker(String pythonHome, String scriptsDir, String inputDir, String outputDir,
                                       boolean withMovies, CookerCallback callback);
    public static native boolean isWiiDisc(String filePath);
    public static native int extractDisc(String imagePath, String outDir, boolean includeMovies, CookerCallback callback);
}
