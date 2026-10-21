package dev.chasem.eclipselauncher;

/** The build values the standalone Sunshine app took from Gradle, taken from WiiDS's build. */
final class BuildConfig {
    private BuildConfig() {}

    static final boolean DEBUG = com.driftdroid.android.BuildConfig.DEBUG;
    static final String BUILD_TYPE = com.driftdroid.android.BuildConfig.BUILD_TYPE;
    static final String VERSION_NAME = com.driftdroid.android.BuildConfig.VERSION_NAME;
    static final String LAUNCHER_REVISION = "WiiDS";
    static final String GAME_REVISION = "WiiDS";
}
