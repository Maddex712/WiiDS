package com.driftdroid.android

/**
 * One pinned, known-good Retro Rewind release, fetched straight from the same official
 * update.rwfc.net/cdn.update.rwfc.net infrastructure this app's own WFC payload URL already talks
 * to (see projects/mkwii/recomp.yml's retro_wfc_payload) - not a third-party mirror. Values
 * (archive URL, exact byte size, SHA-256) are the real 6.13.5 full archive as published by that
 * service (the newest full archive there is, and the one its install feed RetroRewindInstall.txt
 * names); every download is verified against ARCHIVE_BYTES/ARCHIVE_SHA256 before it's
 * accepted, so a corrupted or unexpectedly-changed download fails closed instead of silently
 * installing something else.
 *
 * This is the known-good FALLBACK release, used when the live release catalog can't be reached
 * (offline, or the service is down). The app normally discovers releases from that catalog
 * instead - see RetroRewindCatalog - and only what it can actually run is offered, decided by
 * [SUPPORTED_CODE_PUL_SHA256]. Bumping this means re-translating/re-validating our own
 * statically-recompiled Code.pul support (see generated/build_shards - re-translated for 6.13.5 with the Retro WFC payload).
 */
internal object RetroRewindRelease {
    const val VERSION = "6.13.5"
    const val ARCHIVE_URL = "https://cdn.update.rwfc.net/RetroRewind/zip/6.13.5-full.zip"
    const val ARCHIVE_BYTES = 1939393404L
    const val ARCHIVE_SHA256 = "64d433a7e4d56bb420e2c2eb8221fa13a263e850f578f2960b1ad0a4af529c51"

    /** The build [VERSION] itself ships - the fallback when the network is unavailable and the
     * live catalog can't be consulted. */
    const val CODE_PUL_SHA256 = "d68778e9e126ebd80d45fd4ad57882e269a4b99d121b066910ef04e0a9a4360d"

    /**
     * The Binaries/Code.pul build(s) this app has native code for.
     *
     * The thing compatibility is actually decided on - see [RetroRewindCatalog]. Our Retro Rewind
     * support is a static, ahead-of-time recompilation of this exact file's game logic (see the
     * translator's retro_rewind build shards): a Code.pul that isn't in here has no translated
     * code on the device and would desync at runtime instead of failing cleanly, so it is rejected
     * before it can replace a working install, and an install still holding one is updated before
     * it is launched (see ModePickerActivity).
     *
     * Keying on this hash rather than on a version number is what lets version detection be
     * automatic: Retro Rewind releases that only change assets/tracks ship the same Code.pul (or
     * no Code.pul at all in their delta), so they are adopted with no app update at all.
     *
     * One build of the app carries the translated code of ONE Code.pul, so supporting a new one
     * replaces the entry here: re-translate against it (base game included) before changing the
     * pin. The new Code.pul was checked in both the official full and delta archives.
     */
    val SUPPORTED_CODE_PUL_SHA256 = setOf(CODE_PUL_SHA256)

    /**
     * Releases already known to change Code.pul, and to what. Lets the catalog judge them without
     * fetching their update archive first - 6.13.0's is 614MB, which is not something to download
     * just to answer "can this build run it?".
     */
    val KNOWN_CODE_PUL_SHA256 =
        mapOf(
            "6.12.8" to "88cd25ff08121f7c4ddb40538703f40c270f414f2dbc55a6b4b6767e62db7253",
            "6.13.0" to "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3",
            "6.13.3" to "58f4ac46afdeda4d57aacab361b381ccd95f08c3dbb2779e6e5760b48b1df036",
            "6.13.4" to "11f70007b093d5490ad6ad85868eff4d62b6d80f6ff321cd0c9dec2c59e1b640",
            VERSION to CODE_PUL_SHA256,
        )
}
