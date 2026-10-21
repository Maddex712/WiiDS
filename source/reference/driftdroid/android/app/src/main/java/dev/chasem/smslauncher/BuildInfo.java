package dev.chasem.smslauncher;

/** Embedded metadata comes from the checkouts compiled into this APK. */
final class BuildInfo {
    private BuildInfo() {}
    static String summary() {
        return "Launcher " + BuildConfig.VERSION_NAME + " · " + BuildConfig.BUILD_TYPE + " · ARM64\n"
            + "Launcher build " + BuildConfig.LAUNCHER_REVISION + " · Game build " + BuildConfig.GAME_REVISION;
    }
}
