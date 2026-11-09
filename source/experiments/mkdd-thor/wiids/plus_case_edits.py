"""Anchored frontend edits for the separate Plus case; consumed by apply_plus.py."""
KT = 'app/src/main/kotlin/com/driftdroid/android/'
JV = 'app/src/main/java/com/driftdroid/android/'
EDITS = {
    JV + 'FrontendGame.java': [
        ('    MKDD("mkdd", "Mario Kart: Double Dash!!", "mkdd");',
         '    MKDD("mkdd", "Mario Kart: Double Dash!!", "mkdd"),\n'
         '    MKDD_PLUS("mkdd-plus", "Mario Kart: Double Dash!! Plus", "mkdd-plus");'),
        ('    public static FrontendGame fromId(String id) {',
         '    /** Longest stable ID wins: mkdd-plus backups must never be treated as mkdd. */\n'
         '    public static FrontendGame fromBackupFilename(String name) {\n'
         '        if (name == null || !name.endsWith(".zip")) return null;\n'
         '        FrontendGame found = null;\n'
         '        for (FrontendGame game : values())\n'
         '            if (name.startsWith(game.id + "-") && (found == null || game.id.length() > found.id.length())) found = game;\n'
         '        return found;\n'
         '    }\n\n'
         '    public static FrontendGame fromId(String id) {')],
    JV + 'FrontendResume.java': [
        ('            case MKDD: return ":mkdd_game";',
         '            case MKDD: return ":mkdd_game";\n            case MKDD_PLUS: return ":mkdd_plus_game";'),
        ('            case MKDD: return "dev.thor.mkdd.WiiDsMkddGameActivity";',
         '            case MKDD: return "dev.thor.mkdd.WiiDsMkddGameActivity";\n'
         '            case MKDD_PLUS: return "dev.thor.mkdd.WiiDsMkddPlusGameActivity";')],
    KT + 'ThorGameLaunch.kt': [
        ('    const val MKDD = "mkdd"', '    const val MKDD = "mkdd"\n    const val MKDD_PLUS = "mkdd-plus"'),
        ('    fun launchMkdd(activity: Activity): Result = launch(activity, MKDD_ENTRY, MKDD, mkddInstalled(activity))',
         '    fun launchMkdd(activity: Activity): Result = launch(activity, MKDD_ENTRY, MKDD, mkddInstalled(activity))\n'
         '    fun mkddPlusInstalled(context: Context): Boolean = dev.thor.mkdd.MkddProfiles.plusReady(context)\n'
         '    fun launchMkddPlus(activity: Activity): Result = launch(activity, MKDD_ENTRY, MKDD_PLUS, mkddPlusInstalled(activity))')],
    KT + 'ModePickerActivity.kt': [
        ('                FrontendGame.MKDD -> launchMkddFromLibrary()',
         '                FrontendGame.MKDD -> launchMkddFromLibrary()\n                FrontendGame.MKDD_PLUS -> launchMkddPlusFromLibrary()'),
        ('                MKDD_CASE -> mkddInstalled', '                MKDD_CASE -> mkddInstalled\n                MKDD_PLUS_CASE -> mkddPlusInstalled'),
        ('                    discScale = ThorLauncherView.GAMECUBE_DISC) { launchMkddFromLibrary() },',
         '                    discScale = ThorLauncherView.GAMECUBE_DISC) { launchMkddFromLibrary() },\n'
         '                ThorLauncherView.Entry("Mario Kart: Double Dash!! Plus", "Race the Plus course collection, with separate saves.", { mkddPlusInstalled },\n'
         '                    art = "mkdd-plus", casePlastic = ThorLauncherView.CasePlastic.GAMECUBE_BLACK, updatesShortcut = false,\n'
         '                    discScale = ThorLauncherView.GAMECUBE_DISC) { launchMkddPlusFromLibrary() },'),
        ('    private fun launchMkddFromLibrary() {',
         '    private fun launchMkddPlusFromLibrary() {\n'
         '        val message = when (ThorGameLaunch.launchMkddPlus(this)) {\n'
         '            ThorGameLaunch.Result.OPENED -> return\n'
         '            ThorGameLaunch.Result.NOT_INSTALLED -> "Set up Double Dash Plus first."\n'
         '            ThorGameLaunch.Result.UNAVAILABLE -> "Double Dash Plus could not be opened. Try again."\n'
         '        }\n'
         '        statusText.text = message\n'
         '        thorLauncher?.launchFailed(message)\n'
         '        updateLowerStatus()\n'
         '    }\n\n'
         '    private fun launchMkddFromLibrary() {'),
        ('    private var mkddInstalled = false', '    private var mkddInstalled = false\n    private var mkddPlusInstalled = false'),
        ('        mkddInstalled = runCatching { ThorGameLaunch.mkddInstalled(this) }.getOrDefault(false)',
         '        mkddInstalled = runCatching { ThorGameLaunch.mkddInstalled(this) }.getOrDefault(false)\n'
         '        mkddPlusInstalled = runCatching { ThorGameLaunch.mkddPlusInstalled(this) }.getOrDefault(false)'),
        ('        if (product == MKDD_CASE) {',
         '        if (product == MKDD_PLUS_CASE) {\n'
         '            refreshNsmbInstalled()\n'
         '            return if (mkddPlusInstalled) null else "Set up Double Dash Plus first."\n'
         '        }\n'
         '        if (product == MKDD_CASE) {'),
        ('        private const val LIBRARY_CASES = 10', '        private const val LIBRARY_CASES = 11'),
        ('listOf(8), listOf(9))', 'listOf(8), listOf(9, 10))'),
        ('FrontendGame.BRAWL, FrontendGame.MKDD)', 'FrontendGame.BRAWL, FrontendGame.MKDD, FrontendGame.MKDD_PLUS)'),
        ('        private const val MKDD_CASE = ThorLibraryCompanionView.MKDD',
         '        private const val MKDD_CASE = ThorLibraryCompanionView.MKDD\n'
         '        private const val MKDD_PLUS_CASE = ThorLibraryCompanionView.MKDD_PLUS')],
    KT + 'ThorLibraryCompanionView.kt': [
        ('selection.product == BRAWL || selection.product == MKDD) && !selection.options',
         'selection.product == BRAWL || selection.product == MKDD || selection.product == MKDD_PLUS) && !selection.options'),
        ('when (selection.product) { MKDD -> "Double Dash";',
         'when (selection.product) { MKDD_PLUS -> "Double Dash Plus"; MKDD -> "Double Dash";'),
        ('        MKDD -> "Mario Kart: Double Dash!!"',
         '        MKDD -> "Mario Kart: Double Dash!!"\n        MKDD_PLUS -> "Mario Kart: Double Dash!! Plus"'),
        ('        MKDD -> FrontendGame.MKDD.id', '        MKDD -> FrontendGame.MKDD.id\n        MKDD_PLUS -> FrontendGame.MKDD_PLUS.id'),
        ('        MKDD -> "mkdd"', '        MKDD -> "mkdd"\n        MKDD_PLUS -> "mkdd-plus"'),
        ('                selection.product == MKDD -> "Import your Double Dash USA disc"',
         '                selection.product == MKDD -> "Import your Double Dash USA disc"\n'
         '                selection.product == MKDD_PLUS -> "Set up Double Dash Plus to play"'),
        ('                selection.product == MKDD -> if (selection.available) "Double Dash ready" else "Import your Double Dash disc"',
         '                selection.product == MKDD -> if (selection.available) "Double Dash ready" else "Import your Double Dash disc"\n'
         '                selection.product == MKDD_PLUS -> if (selection.available) "Double Dash Plus ready" else "Set up Double Dash Plus first"'),
        ('                MKDD -> "USA GameCube disc"', '                MKDD -> "USA GameCube disc"\n                MKDD_PLUS -> "Plus 3.3 / separate memory card"'),
        ('        const val MKDD = 9', '        const val MKDD = 9\n        const val MKDD_PLUS = 10'),
        ('"brawl", "mkdd")) for (kind', '"brawl", "mkdd", "mkdd-plus")) for (kind'),
        ('"brawl-disc", "mkdd-disc")) images[name]', '"brawl-disc", "mkdd-disc", "mkdd-plus-disc")) images[name]'),
        ('prefix == "eclipse" || prefix == "mkdd")', 'prefix == "eclipse" || prefix == "mkdd" || prefix == "mkdd-plus")')],
    KT + 'ThorLauncherArtwork.kt': [
        ('setOf("mkdd-cover", "mkdd-disc"))',
         'setOf("mkdd-cover", "mkdd-disc"), setOf("mkdd-plus-cover", "mkdd-plus-disc"))')],
    KT + 'GameStorage.kt': [
        ('        FrontendGame.MKDD -> ThorGameLaunch.mkddInstalled(c)',
         '        FrontendGame.MKDD -> ThorGameLaunch.mkddInstalled(c)\n        FrontendGame.MKDD_PLUS -> ThorGameLaunch.mkddPlusInstalled(c)')],
    KT + 'SaveManagerProfiles.kt': [
        ('            FrontendGame.MKDD -> {\n                if (external == null) throw IOException("Double Dash save storage is unavailable.")\n                root("main", external, "mkdd/saves")\n            }',
         '            FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> {\n'
         '                if (external == null) throw IOException("Double Dash save storage is unavailable.")\n'
         '                root("main", external, if (game == FrontendGame.MKDD_PLUS) "mkdd/plus/saves" else "mkdd/saves")\n'
         '            }')],
    KT + 'SaveManagerActivity.kt': [
        ('if(game==FrontendGame.MKDD) {', 'if(game==FrontendGame.MKDD || game==FrontendGame.MKDD_PLUS) {'),
        ('it.isFile && it.name.startsWith(selected.id + "-") && it.extension == "zip"',
         'it.isFile && FrontendGame.fromBackupFilename(it.name) == selected'),
        ('FrontendGame.values().firstOrNull { source.name.startsWith(it.id + "-") }',
         'FrontendGame.fromBackupFilename(source.name)')],
    KT + 'GamesActivity.kt': [
        ('            FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> Game.MKDD',
         '            FrontendGame.MKDD -> Game.MKDD'),
        ('        val games = if(selected!=null) listOf(selected) else Game.values().toList()',
         '        if (requested == FrontendGame.MKDD_PLUS) column.addView(text(\n'
         '            "Double Dash Plus uses its own patched disc and saves. This private preview uses files prepared on a PC; an on-device Plus installer is not available yet.", 18f, WHITE))\n'
         '        val games = if (requested == FrontendGame.MKDD_PLUS) emptyList() else if(selected!=null) listOf(selected) else Game.values().toList()')],
    KT + 'GameStorageActivity.kt': [
        ('            if(g==FrontendGame.RETRO_REWIND) {',
         '            if (g == FrontendGame.MKDD_PLUS) {\n'
         '                message("This private preview uses Plus files prepared on a PC. The on-device Plus installer is not available yet.")\n'
         '                return\n'
         '            }\n'
         '            if(g==FrontendGame.RETRO_REWIND) {')],
    KT + 'FrontendLaunchActivity.kt': [
        ('            FrontendGame.KAIZO -> FrontendGame.GALAXY',
         '            FrontendGame.KAIZO -> FrontendGame.GALAXY\n            FrontendGame.MKDD_PLUS -> FrontendGame.MKDD')],
    KT + 'achievements/AchievementsActivity.kt': [
        ('            FrontendGame.MKDD -> "Mario Kart: Double Dash!!"',
         '            FrontendGame.MKDD -> "Mario Kart: Double Dash!!"\n            FrontendGame.MKDD_PLUS -> "Mario Kart: Double Dash!! Plus"')],
}
