package com.wither.nsmbw;

/** The build settings the standalone NSMB app took from Gradle, fixed for WiiDS. */
final class NsmbBuild {
    private NsmbBuild() {}

    static final boolean THOR_BUILD = com.driftdroid.android.BuildConfig.THOR_BUILD;
    /** USA revision 1 (SMNE01): the disc this translation and the Games screen import. */
    static final int DISC_REVISION = 1;
    static final String DOL_SHA256 = "166eb6b7cca21418e316a6cfbf1bfbf0a213eb8f6396d42126a1482bb764a955";
}
