from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
APP=ROOT/'reference/driftdroid/android/app/src/main'
def edit(relative,pairs):
    path=APP/relative;raw=path.read_bytes();s=raw.decode('utf-8');nl='\r\n' if '\r\n' in s else '\n';s=s.replace('\r\n','\n')
    backup=ROOT/'build/galaxy-kaizo/frontend-before'/relative;backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():backup.write_bytes(raw)
    for old,new in pairs:
        if new in s:continue
        assert s.count(old)==1,(relative,old,s.count(old));s=s.replace(old,new)
    path.write_bytes(s.replace('\n',nl).encode('utf-8'))
K='kotlin/com/driftdroid/android/'
edit(K+'ThorGameLaunch.kt',[
 ('    const val GALAXY = "galaxy"','    const val GALAXY = "galaxy"\n    const val KAIZO = "kaizo"'),
 ('    fun launchNsmb(activity: Activity): Result',
  '    fun kaizoInstalled(context: Context): Boolean = android.os.Build.VERSION.SDK_INT >= 32 && dev.thor.galaxy.GalaxyStorage.kaizoReady(context)\n\n    fun launchKaizo(activity: Activity): Result = launch(activity, "dev.thor.galaxy.KaizoActivities$Launch", KAIZO, kaizoInstalled(activity))\n\n    fun launchNsmb(activity: Activity): Result'.replace('KaizoActivities$Launch','KaizoActivities\\$Launch'))])
edit(K+'rom/GameCatalog.kt',[
 ('    /**\n     * MSMBWii\'s files', '    /**\n     * MSMBWii\'s files'), # source anchor retained below
 ('            "Runs inside WiiDS with its own game data and saves. Import your USA revision 0 disc; the lower screen is generated automatically."),',
  '            "Runs inside WiiDS with its own game data and saves. Import your USA revision 0 disc; the lower screen is generated automatically."),\n        KAIZO("Kaizo Mario Galaxy Rebalanced 2.1", Disc.GALAXY_USA,\n            "Uses your USA Galaxy revision 0 disc and the creator’s original Rebalanced 2.1 ZIP. Separate saves and mod-generated lower-screen models."),'),
 ('        game.disc.isInstalled(context) && (game != Game.MSMBWII || msmbwiiModInstalled(context))',
  '        if (game == Game.KAIZO) com.driftdroid.android.ThorGameLaunch.kaizoInstalled(context)\n        else game.disc.isInstalled(context) && (game != Game.MSMBWII || msmbwiiModInstalled(context))')])
edit(K+'ModePickerActivity.kt',[
 ('                GALAXY_CASE -> galaxyInstalled','                GALAXY_CASE -> galaxyInstalled\n                KAIZO_CASE -> kaizoInstalled'),
 ('                ThorLauncherView.Entry("Your games",',
  '                ThorLauncherView.Entry("Kaizo Mario Galaxy Rebalanced", "Play the 2.1 challenge rework with separate saves.", { kaizoInstalled },\n                    art = "kaizo", updatesShortcut = false) { launchKaizoFromLibrary() },\n                ThorLauncherView.Entry("Your games",'),
 ('    private var galaxyInstalled = false','    private var galaxyInstalled = false\n    private var kaizoInstalled = false'),
 ('        galaxyInstalled = runCatching { ThorGameLaunch.galaxyInstalled(this) }.getOrDefault(false)',
  '        galaxyInstalled = runCatching { ThorGameLaunch.galaxyInstalled(this) }.getOrDefault(false)\n        kaizoInstalled = runCatching { ThorGameLaunch.kaizoInstalled(this) }.getOrDefault(false)'),
 ('    private fun launchGalaxyFromLibrary() {',
  '    private fun launchKaizoFromLibrary() {\n        val message = when (ThorGameLaunch.launchKaizo(this)) {\n            ThorGameLaunch.Result.OPENED -> return\n            ThorGameLaunch.Result.NOT_INSTALLED -> "Set up Kaizo Rebalanced first."\n            ThorGameLaunch.Result.UNAVAILABLE -> "Kaizo Rebalanced could not be opened. Try again."\n        }\n        statusText.text = message\n        thorLauncher?.launchFailed(message)\n        updateLowerStatus()\n    }\n\n    private fun launchGalaxyFromLibrary() {'),
 ('        if (product == GALAXY_CASE) {',
  '        if (product == KAIZO_CASE) {\n            refreshNsmbInstalled()\n            if (!kaizoInstalled) mainHandler.post { openGames() }\n            return if (kaizoInstalled) null else "Set up Kaizo Rebalanced first."\n        }\n        if (product == GALAXY_CASE) {'),
 ('        private const val LIBRARY_CASES = 7','        private const val LIBRARY_CASES = 8'),
 ('listOf(listOf(0, 1), listOf(2, 3), listOf(4, 5), listOf(6))','listOf(listOf(0, 1), listOf(2, 3), listOf(4, 5), listOf(6, 7))'),
 ('        private const val GALAXY_CASE = ThorLibraryCompanionView.GALAXY',
  '        private const val GALAXY_CASE = ThorLibraryCompanionView.GALAXY\n        private const val KAIZO_CASE = ThorLibraryCompanionView.KAIZO'),
 ('        gameButton("Super Mario Galaxy", Color.rgb(0x48, 0x91, 0xD8), Color.rgb(0x16, 0x43, 0x82), { galaxyInstalled }) { launchGalaxyFromLibrary() }',
  '        gameButton("Super Mario Galaxy", Color.rgb(0x48, 0x91, 0xD8), Color.rgb(0x16, 0x43, 0x82), { galaxyInstalled }) { launchGalaxyFromLibrary() }\n        gameButton("Kaizo Mario Galaxy Rebalanced", Color.rgb(0x48, 0x91, 0xD8), Color.rgb(0x16, 0x43, 0x82), { kaizoInstalled }) { launchKaizoFromLibrary() }')])
edit(K+'ThorLibraryCompanionView.kt',[
 ('        const val GALAXY = 6','        const val GALAXY = 6\n        const val KAIZO = 7'),
 ('selection.product == ECLIPSE || selection.product == GALAXY)', 'selection.product == ECLIPSE || selection.product == GALAXY || selection.product == KAIZO)'),
 ('when (selection.product) { GALAXY -> "Galaxy";', 'when (selection.product) { KAIZO -> "Kaizo Rebalanced"; GALAXY -> "Galaxy";'),
 ('        GALAXY -> "Super Mario Galaxy"','        GALAXY -> "Super Mario Galaxy"\n        KAIZO -> "Kaizo Mario Galaxy Rebalanced"'),
 ('        GALAXY -> "galaxy"','        GALAXY -> "galaxy"\n        KAIZO -> "kaizo"'),
 ('                selection.product == GALAXY -> if (selection.available)',
  '                selection.product == KAIZO -> if (selection.available) "Rebalanced 2.1 ready" else "Set up Rebalanced first"\n                selection.product == GALAXY -> if (selection.available)'),
 ('                GALAXY -> "USA Wii disc"','                KAIZO -> "Own Galaxy disc + official 2.1 patch"\n                GALAXY -> "USA Wii disc"'),
 ('                selection.product == GALAXY -> "Set up Super Mario Galaxy to play"',
  '                selection.product == KAIZO -> "Set up Kaizo Rebalanced on Your games"\n                selection.product == GALAXY -> "Set up Super Mario Galaxy to play"'),
 ('if (selection.product == GALAXY) "Back returns', 'if (selection.product == GALAXY || selection.product == KAIZO) "Back returns')])
edit(K+'GamesActivity.kt',[
 ('    private var galaxyAgain: Button? = null','    private var galaxyAgain: Button? = null\n    private var kaizoAgain: Button? = null'),
 ('else if (game == Game.GALAXY) {','else if (game == Game.GALAXY || game == Game.KAIZO) {'),
 ('            if (game == Game.GALAXY) {\n                refreshGalaxy(card)',
  '            if (game == Game.GALAXY || game == Game.KAIZO) {\n                refreshGalaxy(card, game == Game.KAIZO)'),
 ('    private fun refreshGalaxy(card: Card) {','    private fun refreshGalaxy(card: Card, kaizo: Boolean = false) {'),
 ('        val ready = disc.isInstalled(this)','        val ready = if (kaizo) ThorGameLaunch.kaizoInstalled(this) else disc.isInstalled(this)'),
 ('ready -> button(card, "Play", true) { ThorGameLaunch.launchGalaxy(this) }',
  'ready -> button(card, "Play", true) { if (kaizo) ThorGameLaunch.launchKaizo(this) else ThorGameLaunch.launchGalaxy(this) }'),
 ('path != null -> button(card, "Set up Galaxy", !GameInstaller.running) { importGalaxy(path) }',
  'path != null -> button(card, if (kaizo) "Set up Rebalanced" else "Set up Galaxy", !GameInstaller.running) { importGalaxy(path, kaizo) }'),
 ('else -> button(card, "Choose file", !GameInstaller.running) { chooseFile(Game.GALAXY) }',
  'else -> button(card, "Choose file", !GameInstaller.running) { chooseFile(if (kaizo) Game.KAIZO else Game.GALAXY) }'),
 ('        galaxyAgain?.let { again ->','        (if (kaizo) kaizoAgain else galaxyAgain)?.let { again ->'),
 ('            again.setOnClickListener { if (path != null) importGalaxy(path) }','            again.setOnClickListener { if (path != null) importGalaxy(path, kaizo) }'),
 ('    private fun importGalaxy(path: String) {','    private fun importGalaxy(path: String, kaizo: Boolean = false) {'),
 ('            .setClassName(packageName, "dev.thor.galaxy.GalaxyImportActivity")',
  '            .setClassName(packageName, if (kaizo) "dev.thor.galaxy.KaizoActivities\\$Import" else "dev.thor.galaxy.GalaxyImportActivity")'),
 ('        if (game == Game.ECLIPSE) {\n            box.addView(styledButton("Import patched image")',
  '        if (game == Game.KAIZO) {\n            kaizoAgain = styledButton("Import again (keeps your saves)") {}.also {\n                it.visibility = View.GONE\n                box.addView(it, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(8) })\n            }\n        }\n        if (game == Game.ECLIPSE) {\n            box.addView(styledButton("Import patched image")')])
edit('AndroidManifest.xml',[
 ('        <!-- Galaxy is package-internal and non-debuggable.',
  '        <activity android:name="dev.thor.galaxy.KaizoActivities$Launch" android:exported="false" android:process=":galaxy_kaizo" android:screenOrientation="landscape" />\n        <activity android:name="dev.thor.galaxy.KaizoActivities$Game" android:exported="false" android:process=":galaxy_kaizo_game" android:screenOrientation="landscape" android:launchMode="singleTask" android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen" android:configChanges="density|keyboard|keyboardHidden|navigation|orientation|screenLayout|screenSize|uiMode">\n            <meta-data android:name="android.app.lib_name" android:value="main" />\n        </activity>\n        <activity android:name="dev.thor.galaxy.KaizoActivities$Import" android:exported="false" android:process=":galaxy_kaizo_import" android:screenOrientation="landscape" android:theme="@android:style/Theme.Material.NoActionBar" />\n        <!-- Galaxy is package-internal and non-debuggable.')])
print('Kaizo case appended as index 7; previous indices preserved; separate internal Activities')
