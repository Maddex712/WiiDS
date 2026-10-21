package com.driftdroid.android

/**
 * One pinned, known-good Retro Rewind release, fetched straight from the same official
 * update.rwfc.net/cdn.update.rwfc.net infrastructure this app's own WFC payload URL already talks
 * to (see projects/mkwii/recomp.yml's retro_wfc_payload) - not a third-party mirror. Values
 * (archive URL, exact byte size, SHA-256) are the real 6.13.0 full archive as published by that
 * service (the newest full archive there is - 6.13.1 exists only as a small update on top of it,
 * and the site's own "Full Download" link redirects to this same file); every download is verified against ARCHIVE_BYTES/ARCHIVE_SHA256 before it's
 * accepted, so a corrupted or unexpectedly-changed download fails closed instead of silently
 * installing something else.
 *
 * This is the known-good FALLBACK release, used when the live release catalog can't be reached
 * (offline, or the service is down). The app normally discovers releases from that catalog
 * instead - see RetroRewindCatalog - and only what it can actually run is offered, decided by
 * [SUPPORTED_CODE_PUL_SHA256]. Bumping this means re-translating/re-validating our own
 * statically-recompiled Code.pul support (see generated/build_shards - re-translated for 6.13.0
 * on 2026-10-08).
 */
internal object RetroRewindRelease {
    const val VERSION = "6.13.0"
    const val ARCHIVE_URL = "https://cdn.update.rwfc.net/RetroRewind/zip/6.13.0-full.zip"
    const val ARCHIVE_BYTES = 1938862721L
    const val ARCHIVE_SHA256 = "2d6fa8bce76ee056d3d1e4370296a89af71685b57909978c52ff167d4c6248c2"

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
     * replaces the entry here: re-translate against it (base game included - the translator says
     * so when the mod patches different base functions), then swap the hash. 6.13.0's Code.pul
     * also patches the base game in 69 places 6.12.8's did not, so 6.12.8 cannot run on this
     * build.
     *
     * 6.13.0 ("7471e1...") read from the live 6.13.0 delta archive and from 6.13.0-full.zip on
     * update.rwfc.net, 2026-10-08; 6.13.1's delta ships no Code.pul.
     */
    val SUPPORTED_CODE_PUL_SHA256 =
        setOf(
            "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3", // 6.13.0, 6.13.1
        )

    /**
     * Releases already known to change Code.pul, and to what. Lets the catalog judge them without
     * fetching their update archive first - 6.13.0's is 614MB, which is not something to download
     * just to answer "can this build run it?".
     */
    val KNOWN_CODE_PUL_SHA256 =
        mapOf(
            "6.12.8" to "88cd25ff08121f7c4ddb40538703f40c270f414f2dbc55a6b4b6767e62db7253",
            "6.13.0" to "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3",
        )

    /** The build [VERSION] itself ships - the fallback when the network is unavailable and the
     * live catalog can't be consulted. */
    const val CODE_PUL_SHA256 = "7471e1d2f61c11ab0de6544728406ee1359a4eab736b2dc6a73b741f69e7b4a3"
}
