"""Add Mario Kart: Double Dash!! to a WiiDS Android tree (1.1.2 + issues 9/10 + merged Brawl).

Edits are exact anchored replacements: each anchor must occur exactly once, or nothing is written.
Copies the duo-screen Java (android/ + wiids/) and the stripped ARM64 game library into the tree.
Usage: apply_mkdd_case.py <WiiDS android dir> <libwiids_mkdd_game.so (stripped)>
"""
from pathlib import Path
import shutil
import sys

HERE = Path(__file__).resolve().parent
KT = 'app/src/main/kotlin/com/driftdroid/android/'
JV = 'app/src/main/java/com/driftdroid/android/'

EDITS = {
    JV + 'FrontendGame.java': [
        ('    BRAWL("brawl", "Super Smash Bros. Brawl", "brawl");',
         '    BRAWL("brawl", "Super Smash Bros. Brawl", "brawl"),\n'
         '    MKDD("mkdd", "Mario Kart: Double Dash!!", "mkdd");'),
    ],
    JV + 'FrontendResume.java': [
        ('            case BRAWL: return ":brawl_game";',
         '            case BRAWL: return ":brawl_game";\n            case MKDD: return ":mkdd_game";'),
        ('            case BRAWL: return "dev.wiids.brawl.GameActivity";',
         '            case BRAWL: return "dev.wiids.brawl.GameActivity";\n'
         '            case MKDD: return "dev.thor.mkdd.WiiDsMkddGameActivity";'),
    ],
    KT + 'rom/GameCatalog.kt': [
        ('        GALAXY_USA("RMGE01", 0, WiiDsDisc.Platform.WII, "Super Mario Galaxy", "USA");',
         '        GALAXY_USA("RMGE01", 0, WiiDsDisc.Platform.WII, "Super Mario Galaxy", "USA"),\n'
         '        MKDD_USA("GM4E01", 0, WiiDsDisc.Platform.GAMECUBE, "Mario Kart: Double Dash!!", "USA");'),
        ('            BRAWL_USA_REV2 -> File(context.getExternalFilesDir(null) ?: File(context.filesDir,"data"), "brawl/game")',
         '            BRAWL_USA_REV2 -> File(context.getExternalFilesDir(null) ?: File(context.filesDir,"data"), "brawl/game")\n'
         '            // Only the disc: the importer replaces this folder whole. Saves are mkdd/saves.\n'
         '            MKDD_USA -> File(dev.thor.mkdd.MkddStorage.root(context), "game")'),
        ('        KAIZO("Kaizo Mario Galaxy Rebalanced 2.1", Disc.GALAXY_USA,',
         '        MKDD("Mario Kart: Double Dash!!", Disc.MKDD_USA, "Import your own USA disc (GM4E01). Double Dash runs inside WiiDS with its own memory card; the lower screen\'s standings, items and course maps are built from your disc."),\n'
         '        KAIZO("Kaizo Mario Galaxy Rebalanced 2.1", Disc.GALAXY_USA,'),
    ],
    KT + 'rom/GameInstaller.kt': [
        ('        if(disc==GameCatalog.Disc.BRAWL_USA_REV2) {',
         '        if (disc == GameCatalog.Disc.MKDD_USA) {\n'
         '            dev.thor.mkdd.MkddStorage.validate(temp) { importCancelled }?.let { problem -> temp.deleteRecursively(); return problem }\n'
         '        }\n'
         '        if(disc==GameCatalog.Disc.BRAWL_USA_REV2) {'),
    ],
    KT + 'rom/WiiDsArt.kt': [
        ('            GameCatalog.Disc.BRAWL_USA_REV2 -> emptyList()',
         '            GameCatalog.Disc.BRAWL_USA_REV2 -> emptyList()\n'
         '            // Double Dash builds its duo-screen art in Java (dev.thor.mkdd.MkddArt).\n'
         '            GameCatalog.Disc.MKDD_USA -> emptyList()'),
        ('        disc.isInstalled(context) && (if(disc==GameCatalog.Disc.BRAWL_USA_REV2) !dev.wiids.brawl.BrawlArt.ready(context) else',
         '        disc.isInstalled(context) && (if(disc==GameCatalog.Disc.MKDD_USA) !dev.thor.mkdd.MkddArt.ready(dev.thor.mkdd.MkddStorage.art(context))\n'
         '            else if(disc==GameCatalog.Disc.BRAWL_USA_REV2) !dev.wiids.brawl.BrawlArt.ready(context) else'),
        ('        if(disc==GameCatalog.Disc.BRAWL_USA_REV2)return dev.wiids.brawl.BrawlArt.build(',
         '        if (disc == GameCatalog.Disc.MKDD_USA) {\n'
         '            // A repair pass (onlyMissing) must not rebuild art a running game may be reading.\n'
         '            val art = dev.thor.mkdd.MkddStorage.art(context)\n'
         '            if (onlyMissing && dev.thor.mkdd.MkddArt.ready(art)) return null\n'
         '            return dev.thor.mkdd.MkddArt.build(dev.thor.mkdd.MkddStorage.iso(context), art)\n'
         '        }\n'
         '        if(disc==GameCatalog.Disc.BRAWL_USA_REV2)return dev.wiids.brawl.BrawlArt.build('),
    ],
    KT + 'GameStorage.kt': [
        ('        GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY\n    }',
         '        GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY\n        GameCatalog.Disc.MKDD_USA -> FrontendGame.MKDD\n    }'),
        ('        FrontendGame.BRAWL -> ThorGameLaunch.brawlInstalled(c)\n    }',
         '        FrontendGame.BRAWL -> ThorGameLaunch.brawlInstalled(c)\n        FrontendGame.MKDD -> ThorGameLaunch.mkddInstalled(c)\n    }'),
        ('            FrontendGame.BRAWL -> target("disc",outside,"brawl/game")',
         '            FrontendGame.BRAWL -> target("disc",outside,"brawl/game")\n'
         '            FrontendGame.MKDD -> { target("disc",outside,"mkdd/game"); target("art",outside,"mkdd/lower-art") }'),
        ('        protected+=File(outside,"custom")',
         '        protected+=File(outside,"mkdd/saves")\n        protected+=File(outside,"custom")'),
    ],
    KT + 'SaveManagerProfiles.kt': [
        ('            FrontendGame.BRAWL -> {\n                if(external==null)throw IOException("Brawl save storage is unavailable.")',
         '            FrontendGame.MKDD -> {\n'
         '                if (external == null) throw IOException("Double Dash save storage is unavailable.")\n'
         '                root("main", external, "mkdd/saves")\n'
         '            }\n'
         '            FrontendGame.BRAWL -> {\n                if(external==null)throw IOException("Brawl save storage is unavailable.")'),
    ],
    KT + 'SaveManagerActivity.kt': [
        ('            if(game==FrontendGame.BRAWL) { status.text="Brawl supports WiiDS backup and restore here. Dolphin save-file import is not supported yet.";return@button }',
         '            if(game==FrontendGame.BRAWL) { status.text="Brawl supports WiiDS backup and restore here. Dolphin save-file import is not supported yet.";return@button }\n'
         '            if(game==FrontendGame.MKDD) { status.text="Double Dash supports WiiDS backup and restore here. Dolphin save-file import is not supported yet.";return@button }'),
    ],
    KT + 'ThorGameLaunch.kt': [
        ('    private const val BRAWL_ENTRY = "dev.wiids.brawl.ThorLaunchActivity"',
         '    private const val BRAWL_ENTRY = "dev.wiids.brawl.ThorLaunchActivity"\n'
         '    /** Mario Kart: Double Dash!! runs in its own processes with its own memory card inside WiiDS. */\n'
         '    const val MKDD = "mkdd"\n'
         '    private const val MKDD_ENTRY = "dev.thor.mkdd.MkddLaunchActivity"'),
        ('    fun launchBrawl(activity: Activity): Result = launch(activity,BRAWL_ENTRY,BRAWL,brawlInstalled(activity))',
         '    fun launchBrawl(activity: Activity): Result = launch(activity,BRAWL_ENTRY,BRAWL,brawlInstalled(activity))\n'
         '    fun mkddInstalled(context: Context): Boolean = dev.thor.mkdd.MkddStorage.ready(context)\n'
         '    fun launchMkdd(activity: Activity): Result = launch(activity, MKDD_ENTRY, MKDD, mkddInstalled(activity))'),
    ],
    KT + 'GamesActivity.kt': [
        ('            FrontendGame.BRAWL -> Game.BRAWL\n',
         '            FrontendGame.BRAWL -> Game.BRAWL\n            FrontendGame.MKDD -> Game.MKDD\n'),
    ],
    KT + 'achievements/AchievementsActivity.kt': [
        ('            FrontendGame.BRAWL -> "Super Smash Bros. Brawl"\n            null -> "WiiDS"',
         '            FrontendGame.BRAWL -> "Super Smash Bros. Brawl"\n'
         '            FrontendGame.MKDD -> "Mario Kart: Double Dash!!"\n            null -> "WiiDS"'),
    ],
    KT + 'FrontendLaunchActivity.kt': [
        ('            GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY\n        }',
         '            GameCatalog.Disc.GALAXY_USA -> FrontendGame.GALAXY\n            GameCatalog.Disc.MKDD_USA -> FrontendGame.MKDD\n        }'),
    ],
    KT + 'ThorLauncherArtwork.kt': [
        ('        setOf("kaizo-cover", "kaizo-disc"), setOf("brawl-cover", "brawl-disc"))',
         '        setOf("kaizo-cover", "kaizo-disc"), setOf("brawl-cover", "brawl-disc"), setOf("mkdd-cover", "mkdd-disc"))'),
    ],
    KT + 'ThorLibraryCompanionView.kt': [
        ('selection.product == KAIZO || selection.product == BRAWL) && !selection.options',
         'selection.product == KAIZO || selection.product == BRAWL || selection.product == MKDD) && !selection.options'),
        ('when (selection.product) { BRAWL -> "Brawl"; KAIZO',
         'when (selection.product) { MKDD -> "Double Dash"; BRAWL -> "Brawl"; KAIZO'),
        ('        BRAWL -> "Super Smash Bros. Brawl"\n        MSMBWII -> "MSMBWii: Just a Little Adventure"',
         '        BRAWL -> "Super Smash Bros. Brawl"\n        MKDD -> "Mario Kart: Double Dash!!"\n        MSMBWII -> "MSMBWii: Just a Little Adventure"'),
        ('        BRAWL -> FrontendGame.BRAWL.id\n',
         '        BRAWL -> FrontendGame.BRAWL.id\n        MKDD -> FrontendGame.MKDD.id\n'),
        ('        BRAWL -> "brawl"\n        MSMBWII -> "msmbwii"',
         '        BRAWL -> "brawl"\n        MKDD -> "mkdd"\n        MSMBWII -> "msmbwii"'),
        ('                selection.product == BRAWL -> "Import your Brawl USA revision 2 disc"',
         '                selection.product == BRAWL -> "Import your Brawl USA revision 2 disc"\n'
         '                selection.product == MKDD -> "Import your Double Dash USA disc"'),
        ('                selection.product == BRAWL -> if (selection.available) "Brawl ready" else "Import your Brawl disc"',
         '                selection.product == BRAWL -> if (selection.available) "Brawl ready" else "Import your Brawl disc"\n'
         '                selection.product == MKDD -> if (selection.available) "Double Dash ready" else "Import your Double Dash disc"'),
        ('                BRAWL -> "Import your USA Wii disc, revision 2, in WiiDS"',
         '                BRAWL -> "Import your USA Wii disc, revision 2, in WiiDS"\n                MKDD -> "USA GameCube disc"'),
        ('        const val BRAWL = 8\n', '        const val BRAWL = 8\n        const val MKDD = 9\n'),
        ('"galaxy", "kaizo", "brawl")) for (kind in listOf("cover", "disc"))',
         '"galaxy", "kaizo", "brawl", "mkdd")) for (kind in listOf("cover", "disc"))'),
        ('"galaxy-disc", "kaizo-disc", "brawl-disc")) images[name]',
         '"galaxy-disc", "kaizo-disc", "brawl-disc", "mkdd-disc")) images[name]'),
        ('if (prefix == "sunshine" || prefix == "eclipse") ThorLauncherView.GAMECUBE_DISC',
         'if (prefix == "sunshine" || prefix == "eclipse" || prefix == "mkdd") ThorLauncherView.GAMECUBE_DISC'),
    ],
    KT + 'ModePickerActivity.kt': [
        ('                FrontendGame.BRAWL -> launchBrawlFromLibrary()\n            }',
         '                FrontendGame.BRAWL -> launchBrawlFromLibrary()\n                FrontendGame.MKDD -> launchMkddFromLibrary()\n            }'),
        ('                BRAWL_CASE -> brawlInstalled\n                else -> true',
         '                BRAWL_CASE -> brawlInstalled\n                MKDD_CASE -> mkddInstalled\n                else -> true'),
        ('                    art = "brawl", updatesShortcut = false) { launchBrawlFromLibrary() },',
         '                    art = "brawl", updatesShortcut = false) { launchBrawlFromLibrary() },\n'
         '                // Double Dash: a GameCube case with its 8 cm mini disc, in its own stack.\n'
         '                ThorLauncherView.Entry("Mario Kart: Double Dash!!", "Race in pairs, with the duo screen below.", { mkddInstalled },\n'
         '                    art = "mkdd", casePlastic = ThorLauncherView.CasePlastic.GAMECUBE_BLACK, updatesShortcut = false,\n'
         '                    discScale = ThorLauncherView.GAMECUBE_DISC) { launchMkddFromLibrary() },'),
        ('    private fun launchBrawlFromLibrary() {',
         '    private fun openGamesForMkdd() {\n'
         '        startActivity(Intent(this, GamesActivity::class.java).putExtra("storage_game", ThorGameLaunch.MKDD))\n'
         '    }\n\n'
         '    private fun launchMkddFromLibrary() {\n'
         '        val message = when (ThorGameLaunch.launchMkdd(this)) {\n'
         '            ThorGameLaunch.Result.OPENED -> return\n'
         '            ThorGameLaunch.Result.NOT_INSTALLED -> { openGamesForMkdd(); return }\n'
         '            ThorGameLaunch.Result.UNAVAILABLE -> "Mario Kart: Double Dash!! could not be opened. Try again."\n'
         '        }\n'
         '        statusText.text = message\n'
         '        thorLauncher?.launchFailed(message)\n'
         '        updateLowerStatus()\n'
         '    }\n\n'
         '    private fun launchBrawlFromLibrary() {'),
        ('    private var brawlInstalled = false\n',
         '    private var brawlInstalled = false\n    private var mkddInstalled = false\n'),
        ('        brawlInstalled = runCatching { ThorGameLaunch.brawlInstalled(this) }.getOrDefault(false)\n',
         '        brawlInstalled = runCatching { ThorGameLaunch.brawlInstalled(this) }.getOrDefault(false)\n'
         '        mkddInstalled = runCatching { ThorGameLaunch.mkddInstalled(this) }.getOrDefault(false)\n'),
        ('        if (product == BRAWL_CASE) {',
         '        if (product == MKDD_CASE) {\n'
         '            refreshNsmbInstalled()\n'
         '            if (!mkddInstalled) mainHandler.post { openGamesForMkdd() }\n'
         '            return if (mkddInstalled) null else MKDD_NOT_INSTALLED\n'
         '        }\n'
         '        if (product == BRAWL_CASE) {'),
        ('        private const val LIBRARY_CASES = 9\n', '        private const val LIBRARY_CASES = 10\n'),
        ('listOf(6, 7), listOf(8))', 'listOf(6, 7), listOf(8), listOf(9))'),
        ('FrontendGame.GALAXY, FrontendGame.KAIZO, FrontendGame.BRAWL)\n        private const val GALAXY_CASE',
         'FrontendGame.GALAXY, FrontendGame.KAIZO, FrontendGame.BRAWL, FrontendGame.MKDD)\n        private const val GALAXY_CASE'),
        ('        private const val BRAWL_CASE = ThorLibraryCompanionView.BRAWL\n',
         '        private const val BRAWL_CASE = ThorLibraryCompanionView.BRAWL\n'
         '        private const val MKDD_CASE = ThorLibraryCompanionView.MKDD\n'
         '        private const val MKDD_NOT_INSTALLED = "Import your USA Mario Kart: Double Dash!! disc in Your games first."\n'),
    ],
    'app/src/main/AndroidManifest.xml': [
        ('        <!-- New Super Mario Bros. Wii: launcher-side screens in ":nsmbw"',
         '        <!-- Mario Kart: Double Dash!!: the carousel entry in ":mkdd" (no native code), the game in\n'
         '             ":mkdd_game" (NativeActivity, flat_main.cpp; duo screen on the lower display). -->\n'
         '        <activity android:name="dev.thor.mkdd.MkddLaunchActivity" android:exported="false" android:process=":mkdd"\n'
         '            android:theme="@android:style/Theme.Translucent.NoTitleBar" android:screenOrientation="sensorLandscape" />\n'
         '        <activity android:name="dev.thor.mkdd.WiiDsMkddGameActivity" android:exported="false" android:process=":mkdd_game"\n'
         '            android:launchMode="singleTask" android:screenOrientation="sensorLandscape" android:resizeableActivity="false"\n'
         '            android:preferMinimalPostProcessing="true" android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen"\n'
         '            android:configChanges="density|layoutDirection|locale|grammaticalGender|fontScale|fontWeightAdjustment|colorMode|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|keyboard|keyboardHidden|navigation">\n'
         '            <meta-data android:name="android.app.lib_name" android:value="wiids_mkdd_game" />\n'
         '        </activity>\n\n'
         '        <!-- New Super Mario Bros. Wii: launcher-side screens in ":nsmbw"'),
    ],
}


def main():
    root, lib = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    updated = {}
    for rel, edits in EDITS.items():
        path = root / rel
        raw = path.read_bytes().decode('utf-8')
        crlf = '\r\n' in raw  # keep each file's own line endings
        text = raw.replace('\r\n', '\n')
        for old, new in edits:
            count = text.count(old)
            if count != 1:
                raise SystemExit(f'{rel}: anchor found {count} times: {old[:70]!r}')
            text = text.replace(old, new)
        updated[path] = text.replace('\n', '\r\n') if crlf else text
    for path, text in updated.items():
        path.write_bytes(text.encode('utf-8'))
    java = root / 'app/src/main/java/dev/thor/mkdd'
    java.mkdir(parents=True, exist_ok=True)
    for source in sorted((HERE.parent / 'android/dev/thor/mkdd').glob('*.java')) + sorted((HERE / 'dev/thor/mkdd').glob('*.java')):
        shutil.copy2(source, java / source.name)
    shutil.copy2(lib, root / 'app/src/main/jniLibs/arm64-v8a/libwiids_mkdd_game.so')
    print(f'Applied {sum(len(e) for e in EDITS.values())} edits in {len(EDITS)} files; '
          f'{len(list(java.glob("*.java")))} Java sources; library {lib.stat().st_size} bytes')


if __name__ == '__main__':
    main()
