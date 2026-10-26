package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageInstaller
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.ViewOutlineProvider
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import java.io.File
import java.io.RandomAccessFile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * The app's actual launcher (see AndroidManifest.xml). Chooses Original Mario Kart Wii vs. Retro
 * Rewind BEFORE any guest engine state exists, since neither the combined native library
 * (libGameCombined.so - both products, see runtime/cmake/build_combined_android_lib.py) nor a
 * running game has a supported way to reset and switch profiles live - MainActivity.EXTRA_PRODUCT
 * tells it which one nativeSetActiveProduct() should select.
 *
 * Retro Rewind's pack (tracks/textures, ~2GB - distinct from Code.pul, which only carries the
 * mod's patched game LOGIC and is already compiled into libGameCombined.so) is never bundled or
 * downloaded by this app, matching how the PC version works: WiiCompiled.Setup's
 * RetroRewindSource.cs only ever resolves a folder the user already has (normally obtained via the
 * separate Wheel Wizard tool) - it never packages or downloads it itself. This picker offers two
 * ways to hand over a copy the user already obtained: the .zip file exactly as downloaded (most
 * people will only ever have this - extracted first, matching how someone who downloaded Retro
 * Rewind normally has it), or an already-extracted folder for anyone who prefers that.
 */
class ModePickerActivity : Activity() {

    private lateinit var root: LinearLayout
    private lateinit var baseButton: Button
    private lateinit var retroButton: Button
    private lateinit var retroMenuButton: Button
    /** Plain launcher (no Wii-menu art yet): the other games and the Games screen. */
    private val otherGameButtons = LinkedHashMap<Button, () -> Boolean>()
    private lateinit var statusText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var diagnosticsButton: Button
    private lateinit var graphicsButton: Button
    private lateinit var iconButton: Button
    private lateinit var serverSettingsButton: Button
    private lateinit var modsButton: Button
    private lateinit var miiDataButton: Button
    private lateinit var updateButton: Button
    private var thorLauncher: ThorLauncherView? = null
    private var thorCompanion: ThorLowerCompanion? = null
    private var thorLibraryProduct: Int? = 0
    private var thorLibraryOptions = false
    private var thorPhase = ThorLauncherView.Phase.BROWSE
    private var thorPhaseMessage = ""
    /** Cached Code.pul compatibility (hashing is file IO, never done on the UI thread). */
    @Volatile private var retroCodeSupported: Boolean? = null
    private var routingPendingUpdate = false
    private var updateAfterSettings: AppUpdate.Update? = null
    private var updateReceiver: BroadcastReceiver? = null
    @Volatile private var updateCancelled = false
    private var updateInstalling = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private var presentationLoading = false
    private var libraryResumed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (BuildConfig.THOR_BUILD && ThorBundledPresentation.available(this)) {
            presentationLoading = true
            val preparing = TextView(this).apply {
                text = "Preparing your WiiDS library…"
                textSize = 22f
                setTextColor(Color.rgb(0x47, 0x55, 0x64))
                setBackgroundColor(Color.rgb(0xF1, 0xF5, 0xF8))
                gravity = Gravity.CENTER
            }
            setContentView(preparing)
            val app = applicationContext
            Thread {
                var lastProgress = 0L
                val error = runCatching {
                    ThorBundledPresentation.install(app) { done, total ->
                        val now = android.os.SystemClock.uptimeMillis()
                        if (now - lastProgress >= 100 || done == total) {
                            lastProgress = now
                            runOnUiThread { if (!isFinishing && !isDestroyed) preparing.text = "Preparing your WiiDS library… ${done * 100 / total}%" }
                        }
                    }
                }.exceptionOrNull()
                runOnUiThread {
                    if (isFinishing || isDestroyed) return@runOnUiThread
                    presentationLoading = false
                    if (error != null) android.util.Log.w("WiiDS", "Bundled presentation could not be prepared; existing files were kept", error)
                    createLibrary()
                    if (libraryResumed) resumeLibrary()
                }
            }.apply { name = "WiiDsPresentation"; isDaemon = true }.start()
            return
        }
        createLibrary()
    }

    private fun createLibrary() {
        refreshNsmbInstalled()
        buildUi()
        if (routePendingFrontend()) return
        if (routePendingThorUpdate()) return
        if (BuildConfig.THOR_BUILD) {
            val restartProduct = intent.getStringExtra(EXTRA_RESTART_PRODUCT)
            intent.removeExtra(EXTRA_RESTART_PRODUCT)
            if (restartProduct == MainActivity.PRODUCT_BASE ||
                (restartProduct == MainActivity.PRODUCT_RETRO_REWIND && BuildConfig.THOR_RETRO_REWIND)) {
                mainHandler.post { if (!isFinishing && !isDestroyed) launchProduct(restartProduct) }
            }
        }
    }

    override fun onNewIntent(request: Intent) {
        super.onNewIntent(request)
        setIntent(request)
        if (!presentationLoading) routePendingFrontend()
    }

    private fun routePendingFrontend(): Boolean {
        val id = intent.getStringExtra(EXTRA_FRONTEND_GAME) ?: return false
        intent.removeExtra(EXTRA_FRONTEND_GAME)
        if (!BuildConfig.THOR_BUILD) return false
        val game = FrontendGame.fromId(id) ?: return false
        mainHandler.post {
            if (isFinishing || isDestroyed) return@post
            when (game) {
                FrontendGame.MARIO_KART -> {
                    val blocker = thorLaunchBlocker(0)
                    if (blocker == null) launchProduct(MainActivity.PRODUCT_BASE)
                    else Toast.makeText(this, blocker, Toast.LENGTH_LONG).show()
                }
                FrontendGame.RETRO_REWIND -> launchRetroRewindOrUpdate()
                FrontendGame.NSMB -> launchNsmbFromLibrary()
                FrontendGame.MSMBWII -> launchMsmbwiiFromLibrary()
                FrontendGame.SUNSHINE -> launchSunshineFromLibrary()
                FrontendGame.ECLIPSE -> launchEclipseFromLibrary()
                FrontendGame.GALAXY -> launchGalaxyFromLibrary()
                FrontendGame.KAIZO -> launchKaizoFromLibrary()
            }
        }
        return true
    }

    override fun onResume() {
        super.onResume()
        libraryResumed = true
        if (!presentationLoading) resumeLibrary()
    }

    private fun resumeLibrary() {
        applyThorFullscreen()
        if (routePendingThorUpdate()) return
        // First run: nothing imported yet, so start where every game is set up.
        if (!gamesOfferedThisProcess && com.driftdroid.android.rom.GameCatalog.Disc.values().none { it.isInstalled(this) }) {
            gamesOfferedThisProcess = true
            openGames()
        }
        refreshNsmbInstalled()
        refreshChannelButtons()
        // Art an app update added (or a failed build) is made from the imported discs in the background.
        if (!com.driftdroid.android.rom.GameInstaller.running) com.driftdroid.android.rom.GameInstaller.repairArt(this)
        // Mario Kart's Wii-menu art may have just been built from a freshly imported disc.
        if (thorLauncher == null && File(com.driftdroid.android.rom.WiiDsArt.thorMap(this), "menu.json").isFile) showThorLauncher()
        val thorDevice = BuildConfig.THOR_BUILD && Build.MODEL.equals("AYN Thor", ignoreCase = true)
        if (thorDevice) {
            if (thorCompanion == null) thorCompanion = ThorLowerCompanion(this, File(filesDir, "WiiCompiled/ThorMap")).also {
                it.onOpenAchievements = { game, item -> if (libraryResumed) openAchievements(game, item) }
            }
            updateLowerStatus()
            thorCompanion?.start()
            // A game or the browser may have recorded unlocks since the previews were read.
            thorCompanion?.refreshAchievements()
            // RetroAchievements progress, at most once a day while an account is connected.
            com.driftdroid.android.achievements.RaLibrary.syncIfDue(this) { _, _, _ -> thorCompanion?.refreshAchievements() }
        }
        // The case launcher runs on any THOR_BUILD device; only the lower-screen companion needs the Thor.
        thorLauncher?.refreshSelection()
        thorLauncher?.onHostResumed()
        if (thorDevice) {
            refreshRetroCodeSupport()
            // Retro Rewind's lower-screen maps follow its installed pack (baked on this device).
            com.driftdroid.android.rom.WiiDsArt.repairRetroRewind(this)
            // Debug builds only: exercise the storage search without removing installed game data.
            if (BuildConfig.DEBUG && intent.getBooleanExtra("thor_rom_scan_test", false)) {
                intent.removeExtra("thor_rom_scan_test")
                mainHandler.postDelayed({ findRomOnStorage(0) }, 1500)
            }
            // Back from the All files access page: continue the search the player asked for.
            romScanPending?.let { product ->
                romScanPending = null
                if (com.driftdroid.android.rom.RomScanner.hasAccess() && !isRomImported()) findRomOnStorage(product)
            }
        }
        // An install may still be running in RetroRewindInstallService from a previous instance
        // of this Activity (e.g. this one is a fresh recreation after Android reclaimed the old
        // one while backgrounded) - pick up its live progress instead of showing an idle screen
        // over a copy that's actually still going.
        if (RetroRewindInstallStatus.running) {
            beginImportUi(RetroRewindInstallStatus.label)
            startStatusPolling()
        }
        checkForUpdate()
        // Back from Android's "Install unknown apps" page: continue the update if it was allowed.
        updateAfterSettings?.let { update ->
            updateAfterSettings = null
            if (packageManager.canRequestPackageInstalls()) downloadUpdate(update)
        }
    }

    override fun onPause() {
        libraryResumed = false
        thorLauncher?.onHostPaused()
        // During a committed launch the lower companion stays up until onStop, so the lower
        // display never flashes the system home screen before the game's own presentation.
        if (thorLauncher?.launchCommitted != true) thorCompanion?.stop()
        super.onPause()
        stopStatusPolling()
    }

    private fun routePendingThorUpdate(): Boolean {
        if (!BuildConfig.THOR_BUILD || !BuildConfig.THOR_RETRO_REWIND || !ThorUpdateRestart.requested(this)) {
            routingPendingUpdate = false
            return false
        }
        if (!routingPendingUpdate) {
            routingPendingUpdate = true
            startActivity(Intent(this, ThorRetroRewindUpdatesActivity::class.java))
        }
        return true
    }

    override fun onStop() {
        thorCompanion?.stop()
        super.onStop()
    }

    override fun onDestroy() {
        thorCompanion?.stop()
        thorCompanion = null
        updateCancelled = true
        updateReceiver?.let { runCatching { unregisterReceiver(it) } }
        updateReceiver = null
        super.onDestroy()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    /** The game Options > Achievements opens: the case last selected on the shelf. */
    private var lastLibraryGame = FrontendGame.NSMB

    /** The WiiDS achievement browser for [game], optionally focused on one achievement. */
    private fun openAchievements(game: String, achievement: String?) {
        startActivity(Intent(this, com.driftdroid.android.achievements.AchievementsActivity::class.java)
            .putExtra(com.driftdroid.android.achievements.AchievementsActivity.EXTRA_GAME, game)
            .putExtra(com.driftdroid.android.achievements.AchievementsActivity.EXTRA_ACHIEVEMENT, achievement))
        @Suppress("DEPRECATION") overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
    }

    private fun updateLowerStatus() {
        val status = if (::statusText.isInitialized) statusText.text.toString().trim() else ""
        if (thorLauncher != null) {
            val ready = when (thorLibraryProduct) {
                0 -> isRomImported()
                1 -> BuildConfig.THOR_RETRO_REWIND && isRomImported() && isRetroRewindInstalled()
                NSMB_CASE -> nsmbInstalled
                SUNSHINE_CASE -> sunshineInstalled
                ECLIPSE_CASE -> eclipseInstalled
                GALAXY_CASE -> galaxyInstalled
                KAIZO_CASE -> kaizoInstalled
                MSMBWII_CASE -> msmbwiiInstalled
                else -> true
            }
            thorCompanion?.setLibrarySelection(thorLibraryProduct, thorLibraryOptions, ready, status = status,
                phase = thorPhase, message = thorPhaseMessage)
        } else {
            thorCompanion?.setStatus("Ready to race", status.ifEmpty { "Choose a game on the upper screen." })
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyThorFullscreen()
    }

    private fun applyThorFullscreen() {
        if (!BuildConfig.THOR_BUILD || Build.VERSION.SDK_INT < 30) return
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.apply {
            systemBarsBehavior = android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(android.view.WindowInsets.Type.systemBars())
        }
    }

    /** The existing launcher remains the first-run fallback until the owner's disc art is staged. */
    private fun showThorLauncher(): Boolean {
        if (!BuildConfig.THOR_BUILD) return false
        return runCatching {
            val folder = File(filesDir, "WiiCompiled/ThorMap")
            val manifest = org.json.JSONObject(File(folder, "menu.json").readText())
            require(manifest.getInt("schema") == 1)
            val artwork = ThorMenuArtwork(folder)
            val images = manifest.getJSONObject("images")
            val chrome = listOf("background", "header", "button", "button_selected").associateWith { role ->
                artwork.bitmap(images.getJSONObject(role).getString("texture"))
            }.toMutableMap()
            if (images.has("footer")) chrome["footer"] = artwork.bitmap(images.getJSONObject("footer").getString("texture"))
            if (images.has("panel")) chrome["panel"] = artwork.bitmap(images.getJSONObject("panel").getString("texture"))
            val entries = listOf(
                // The first LIBRARY_CASES entries are game cases, in shelf order.
                ThorLauncherView.Entry("Mario Kart Wii", "Play the original Mario Kart Wii.", { baseButton.isEnabled },
                    art = "original") { baseButton.performClick() },
                ThorLauncherView.Entry("Retro Rewind", "Race the Retro Rewind course collection.", { retroButton.isEnabled && BuildConfig.THOR_RETRO_REWIND },
                    art = "retro-rewind") { retroButton.performClick() },
                // A separate app: launched through ThorGameLaunch, never MainActivity.EXTRA_PRODUCT.
                ThorLauncherView.Entry("New Super Mario Bros. Wii", "Play New Super Mario Bros. Wii.", { nsmbInstalled },
                    art = "nsmbw", casePlastic = ThorLauncherView.CasePlastic.WII_RED, updatesShortcut = false) { launchNsmbFromLibrary() },
                ThorLauncherView.Entry("MSMBWii: Just a Little Adventure", "Play MidiGuyDP's New Super Mario Bros. Wii level mod.", { msmbwiiInstalled },
                    art = "msmbwii", casePlastic = ThorLauncherView.CasePlastic.WII_RED, updatesShortcut = false) { launchMsmbwiiFromLibrary() },
                // Wii cases stay together; the GameCube case follows them with its 8 cm mini disc.
                ThorLauncherView.Entry("Super Mario Sunshine", "Play Super Mario Sunshine.", { sunshineInstalled },
                    art = "sunshine", casePlastic = ThorLauncherView.CasePlastic.GAMECUBE_BLACK, updatesShortcut = false, discScale = ThorLauncherView.GAMECUBE_DISC) { launchSunshineFromLibrary() },
                ThorLauncherView.Entry("Super Mario Eclipse", "Play the Sunshine expansion with its own saves.", { eclipseInstalled },
                    art = "eclipse", casePlastic = ThorLauncherView.CasePlastic.GAMECUBE_BLACK, updatesShortcut = false, discScale = ThorLauncherView.GAMECUBE_DISC) { launchEclipseFromLibrary() },
                ThorLauncherView.Entry("Super Mario Galaxy", "Explore the galaxies with Mario.", { galaxyInstalled },
                    art = "galaxy", updatesShortcut = false) { launchGalaxyFromLibrary() },
                ThorLauncherView.Entry("Kaizo Mario Galaxy Rebalanced", "Play the 2.1 challenge rework with separate saves.", { kaizoInstalled },
                    art = "kaizo", updatesShortcut = false) { launchKaizoFromLibrary() },
                ThorLauncherView.Entry("Your games", "Find and import your own game discs.") { openGames() },
                ThorLauncherView.Entry("Check for updates", "Download and install the latest WiiDS release.") {
                    startActivity(Intent(this, WiiDsUpdateActivity::class.java))
                },
                ThorLauncherView.Entry("Controller buttons", "Change what each controller button does, game by game.") { showControllerButtons() },
                ThorLauncherView.Entry("Game shortcuts", "Add a game or mod directly to Cocoon or your home screen.") { GameShortcuts.show(this) },
                ThorLauncherView.Entry("Save manager", "Back up, export or restore each game's in-game saves.") {
                    startActivity(Intent(this, SaveManagerActivity::class.java))
                },
                ThorLauncherView.Entry("Achievements", "Your RetroAchievements for each game, with everything already earned.") {
                    openAchievements(lastLibraryGame.id, null)
                },
                ThorLauncherView.Entry("Graphics", "Choose a renderer before starting the game.") { graphicsButton.performClick() },
                ThorLauncherView.Entry("Mii Data", "Import and manage your own Mii data.") { miiDataButton.performClick() },
                ThorLauncherView.Entry("Retro Rewind updates", "Check and download compatible Retro Rewind updates.", { BuildConfig.THOR_RETRO_REWIND }) { if (isRetroRewindInstalled()) showRetroRewindMenuDialog() else showInstallRetroRewindDialog() },
                ThorLauncherView.Entry("Texture Packs", "Manage texture packs before starting the game.") { modsButton.performClick() },
                ThorLauncherView.Entry("Diagnostics", "Export logs to help investigate a problem.") { diagnosticsButton.performClick() },
                ThorLauncherView.Entry("App Icon", "Choose an icon for your launcher.") { iconButton.performClick() },
                ThorLauncherView.Entry("Server Settings", "Configure an optional private server.") { serverSettingsButton.performClick() },
            )
            val view = ThorLauncherView(this, artwork, chrome, entries, LIBRARY_CASES, LIBRARY_BUNDLES,
                onSelectionChanged = { product, options ->
                    thorLibraryProduct = product
                    product?.let { LIBRARY_GAMES.getOrNull(it) }?.let { lastLibraryGame = it }
                    thorLibraryOptions = options
                    updateLowerStatus()
                },
                onPhaseChanged = { phase, product, message ->
                    thorPhase = phase
                    thorPhaseMessage = message
                    if (product != null && !thorLibraryOptions) thorLibraryProduct = product
                    updateLowerStatus()
                },
                preflight = { product -> thorLaunchBlocker(product) },
                onAchievements = { product -> LIBRARY_GAMES.getOrNull(product)?.let { openAchievements(it.id, null) } },
                thorMap = folder) { statusText.text.toString() }
            thorLauncher = view
            statusText.addTextChangedListener(object : android.text.TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { view.invalidate() }
                override fun afterTextChanged(s: android.text.Editable?) {}
            })
            setContentView(view)
            true
        }.getOrElse { error ->
            android.util.Log.w("ThorMenu", "Private Wii menu art unavailable; retaining import launcher", error)
            false
        }
    }

    // --- DriftDroid updating itself (see AppUpdate; the flow is KartPad's) ---

    /** Runs once, from the case launcher's commit point (or a closed case whose game is missing). */
    private fun launchNsmbFromLibrary() {
        val message = when (ThorGameLaunch.launchNsmb(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> NSMB_NOT_INSTALLED
            ThorGameLaunch.Result.UNAVAILABLE -> "New Super Mario Bros. Wii could not be opened. Try again."
        }
        refreshNsmbInstalled()
        val launcher = thorLauncher
        if (launcher != null) { launcher.launchFailed(message); return }
        AlertDialog.Builder(this).setTitle("New Super Mario Bros. Wii")
            .setMessage(message).setPositiveButton(android.R.string.ok, null).show()
    }

    /** Uses the same single commit point as the other cases, with a separate native runtime. */
    private fun launchSunshineFromLibrary() {
        val message = when (ThorGameLaunch.launchSunshine(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> SUNSHINE_NOT_INSTALLED
            ThorGameLaunch.Result.UNAVAILABLE -> "Super Mario Sunshine could not be opened. Try again."
        }
        refreshNsmbInstalled()
        val launcher = thorLauncher
        if (launcher != null) { launcher.launchFailed(message); return }
        AlertDialog.Builder(this).setTitle("Super Mario Sunshine")
            .setMessage(message).setPositiveButton(android.R.string.ok, null).show()
    }

    /** Eclipse uses its own runtime process and profile; Sunshine's profile is never reused. */
    private fun launchEclipseFromLibrary() {
        val message = when (ThorGameLaunch.launchEclipse(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> ECLIPSE_NOT_INSTALLED
            ThorGameLaunch.Result.UNAVAILABLE -> "Super Mario Eclipse could not be opened. Try again."
        }
        refreshNsmbInstalled()
        val launcher = thorLauncher
        if (launcher != null) { launcher.launchFailed(message); return }
        AlertDialog.Builder(this).setTitle("Super Mario Eclipse")
            .setMessage(message).setPositiveButton(android.R.string.ok, null).show()
    }

    /** The MSMBWii level mod: the same contract, in its own NSMB-based app with separate saves. */
    private fun launchMsmbwiiFromLibrary() {
        val message = when (ThorGameLaunch.launchMsmbwii(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> MSMBWII_NOT_INSTALLED
            ThorGameLaunch.Result.UNAVAILABLE -> "MSMBWii could not be opened. Try again."
        }
        refreshNsmbInstalled()
        val launcher = thorLauncher
        if (launcher != null) { launcher.launchFailed(message); return }
        AlertDialog.Builder(this).setTitle("MSMBWii")
            .setMessage(message).setPositiveButton(android.R.string.ok, null).show()
    }

    /** Package lookups stay off the per-frame drawing path; refreshed on resume and after a failed launch. */
    private var nsmbInstalled = false
    private fun launchKaizoFromLibrary() {
        val message = when (ThorGameLaunch.launchKaizo(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> "Set up Kaizo Rebalanced first."
            ThorGameLaunch.Result.UNAVAILABLE -> "Kaizo Rebalanced could not be opened. Try again."
        }
        statusText.text = message
        thorLauncher?.launchFailed(message)
        updateLowerStatus()
    }

    private fun launchGalaxyFromLibrary() {
        val message = when (ThorGameLaunch.launchGalaxy(this)) {
            ThorGameLaunch.Result.OPENED -> return
            ThorGameLaunch.Result.NOT_INSTALLED -> "Set up Super Mario Galaxy first."
            ThorGameLaunch.Result.UNAVAILABLE -> "Super Mario Galaxy could not be opened. Try again."
        }
        statusText.text = message
        thorLauncher?.launchFailed(message)
        updateLowerStatus()
    }

    private var sunshineInstalled = false
    private var galaxyInstalled = false
    private var kaizoInstalled = false
    private var eclipseInstalled = false
    private var msmbwiiInstalled = false
    private fun refreshNsmbInstalled() {
        nsmbInstalled = runCatching { ThorGameLaunch.nsmbInstalled(this) }.getOrDefault(false)
        sunshineInstalled = runCatching { ThorGameLaunch.sunshineInstalled(this) }.getOrDefault(false)
        galaxyInstalled = runCatching { ThorGameLaunch.galaxyInstalled(this) }.getOrDefault(false)
        kaizoInstalled = runCatching { ThorGameLaunch.kaizoInstalled(this) }.getOrDefault(false)
        eclipseInstalled = runCatching { ThorGameLaunch.eclipseInstalled(this) }.getOrDefault(false)
        msmbwiiInstalled = runCatching { ThorGameLaunch.msmbwiiInstalled(this) }.getOrDefault(false)
    }

    /** Looks for a newer release in the background and shows the button when there is one.
     * [force] skips the once-an-hour limit - used when something just said an update is needed. */
    private fun checkForUpdate(force: Boolean = false) {
        // This local variant must keep its Thor changes rather than offer the upstream APK.
        if (BuildConfig.THOR_BUILD) return
        val context = applicationContext
        Thread {
            AppUpdate.refresh(context, force)
            val update = AppUpdate.known(context)
            if (update == null) AppUpdate.clear(context)
            mainHandler.post {
                if (!isFinishing && !isDestroyed) {
                    updateButton.text = if (update != null) "Update available: ${update.version}" else "Update available"
                    updateButton.visibility = if (update != null) View.VISIBLE else View.GONE
                }
            }
        }.apply { isDaemon = true; name = "DriftDroidUpdateCheck" }.start()
    }

    private fun showUpdate() {
        val update = AppUpdate.known(this) ?: return
        val dialog =
            ThorDialogs.builder(this)
                .setTitle("DriftDroid ${update.version} is out")
                .setMessage(
                    "You have ${AppUpdate.installedVersionName(this)}. DriftDroid can download the update and " +
                        "install it over this one. Your saves, game data and Retro Rewind stay.",
                )
                .setNeutralButton("Release Notes") { _, _ -> openWeb(update.pageUrl) }
                .setNegativeButton("Later", null)
        if (update.apkUrl != null) dialog.setPositiveButton("Update Now") { _, _ -> startUpdate(update) }
        dialog.show()
    }

    private fun openWeb(url: String) {
        runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
    }

    /** Android asks once before an app may install its own updates. */
    private fun startUpdate(update: AppUpdate.Update) {
        if (packageManager.canRequestPackageInstalls()) {
            downloadUpdate(update)
            return
        }
        ThorDialogs.builder(this)
            .setTitle("Allow DriftDroid to Install Updates")
            .setMessage(
                "Android asks once before an app can install its own updates. On the next screen, turn on " +
                    "Allow from this source, then come back to DriftDroid.",
            )
            .setPositiveButton("Continue") { _, _ ->
                updateAfterSettings = update
                try {
                    startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:$packageName")))
                } catch (_: Exception) {
                    updateAfterSettings = null
                    showUpdateFailed(update, "Android's settings couldn't be opened.")
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun downloadUpdate(update: AppUpdate.Update) {
        val label = TextView(this)
        label.text = "Connecting to GitHub..."
        label.setPadding(0, 0, 0, dp(12))
        val bar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal)
        bar.max = 1000
        bar.isIndeterminate = true
        val content = LinearLayout(this)
        content.orientation = LinearLayout.VERTICAL
        content.setPadding(dp(24), dp(8), dp(24), 0)
        content.addView(label)
        content.addView(bar)
        updateCancelled = false
        val dialog =
            ThorDialogs.builder(this)
                .setTitle("Updating to DriftDroid ${update.version}")
                .setView(content)
                .setNegativeButton("Cancel") { _, _ -> updateCancelled = true }
                .setCancelable(false)
                .show()
        listenForInstall(update, dialog)
        val context = applicationContext
        Thread {
            val result =
                runCatching {
                    val apk =
                        AppUpdate.download(context, update, { done, total ->
                            mainHandler.post {
                                if (total > 0) {
                                    bar.isIndeterminate = false
                                    bar.progress = (done * 1000 / total).toInt()
                                    label.text = "Downloading: ${done / 1_048_576} of ${total / 1_048_576} MB"
                                } else {
                                    label.text = "Downloading: ${done / 1_048_576} MB"
                                }
                            }
                        }) { updateCancelled }
                    mainHandler.post {
                        label.text =
                            "Download checked. Android will ask you to confirm the update. DriftDroid closes " +
                                "when it's done; open it again."
                        bar.isIndeterminate = true
                        dialog.getButton(AlertDialog.BUTTON_NEGATIVE)?.isEnabled = false
                    }
                    updateInstalling = true
                    AppUpdate.install(context, apk)
                }
            result.onFailure { error ->
                updateInstalling = false
                mainHandler.post {
                    if (isFinishing || isDestroyed) return@post
                    dialog.dismiss()
                    if (error !is AppUpdate.Cancelled) showUpdateFailed(update, error.message)
                }
            }
        }.apply { isDaemon = true; name = "DriftDroidUpdateDownload" }.start()
    }

    /** Android's installer reports here: ask the player to confirm, then it replaces DriftDroid. */
    private fun listenForInstall(update: AppUpdate.Update, dialog: AlertDialog) {
        updateReceiver?.let { runCatching { unregisterReceiver(it) } }
        val receiver =
            object : BroadcastReceiver() {
                override fun onReceive(context: Context, intent: Intent) {
                    if (!updateInstalling) return
                    when (intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
                        PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                            val confirm =
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                                } else {
                                    @Suppress("DEPRECATION")
                                    intent.getParcelableExtra(Intent.EXTRA_INTENT)
                                }
                            // Only Android's own confirmation screen, never one of DriftDroid's.
                            if (confirm != null && confirm.component?.packageName != packageName) {
                                runCatching { startActivity(confirm) }
                            }
                        }
                        PackageInstaller.STATUS_SUCCESS, PackageInstaller.STATUS_FAILURE_ABORTED -> {
                            updateInstalling = false
                            dialog.dismiss()
                        }
                        else -> {
                            updateInstalling = false
                            dialog.dismiss()
                            showUpdateFailed(update, intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE))
                        }
                    }
                }
            }
        updateReceiver = receiver
        val filter = IntentFilter(AppUpdate.ACTION_INSTALL_STATUS)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(receiver, filter)
        }
    }

    private fun showUpdateFailed(update: AppUpdate.Update, reason: String?) {
        val dialog =
            ThorDialogs.builder(this)
                .setTitle("The Update Didn't Finish")
                .setMessage(
                    (reason?.takeIf { it.isNotBlank() } ?: "Something went wrong.") +
                        "\n\nYou can also download the APK in your browser and install it over DriftDroid.",
                )
                .setNegativeButton("Close", null)
        update.apkUrl?.let { url -> dialog.setPositiveButton("Download in Browser") { _, _ -> openWeb(url) } }
        dialog.show()
    }


    /** A rounded "channel" tile background, Wii Menu channel style. */
    private fun channelDrawable(topColor: Int, bottomColor: Int, radius: Float): GradientDrawable =
        GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM,
            intArrayOf(topColor, bottomColor),
        ).also { it.cornerRadius = radius }

    private fun styleChannelButton(
        button: Button,
        topColor: Int,
        bottomColor: Int,
        radius: Float = dp(18).toFloat(),
    ) {
        button.background = channelDrawable(topColor, bottomColor, radius)
        button.setTextColor(Color.WHITE)
        button.typeface = Typeface.DEFAULT_BOLD
        button.isAllCaps = false
        button.setShadowLayer(3f, 0f, 2f, Color.argb(140, 0, 0, 0))
        button.stateListAnimator = null
        button.elevation = dp(4).toFloat()
        addPressBounce(button)
    }

    /** A game channel drawn in code (WiiDS ships no game artwork); dimmed while locked. */
    private fun styleTextChannel(button: Button, label: String, top: Int, bottom: Int, locked: Boolean, lockedLabel: String) {
        button.text = if (locked) lockedLabel else label
        button.textSize = 17f
        button.isAllCaps = false
        if (locked) styleChannelButton(button, Color.rgb(0x5A, 0x60, 0x6B), Color.rgb(0x3A, 0x3F, 0x48), dp(18).toFloat())
        else styleChannelButton(button, top, bottom, dp(18).toFloat())
        if (locked) button.setTextColor(Color.argb(200, 255, 255, 255))
    }

    /**
     * The little "press in, spring back" wiggle every Wii Menu channel does when tapped -
     * squishes non-uniformly (width in more than height) rather than a plain uniform scale, and
     * overshoots slightly past 1.0 on release before settling - the actual difference between
     * "shrinks and grows back" (mechanical) and "liquid"/gel-like motion is in that asymmetry and
     * overshoot, not just easing curve choice. Purely a touch-feedback animation (returns false so
     * the real click listener still fires), not a gate on the click itself.
     */
    private fun addPressBounce(view: View) {
        val overshoot = android.view.animation.OvershootInterpolator(3.5f)
        view.setOnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN ->
                    v.animate().scaleX(0.92f).scaleY(0.95f).setDuration(90)
                        .setInterpolator(android.view.animation.DecelerateInterpolator()).start()
                // Sound plays on an actual completed tap only, not ACTION_CANCEL (a drag that
                // left the view, or the touch getting reassigned elsewhere) - both still spring
                // the view back visually, but a canceled touch never counted as a "click" and
                // shouldn't sound like one either.
                MotionEvent.ACTION_UP -> {
                    v.animate().scaleX(1f).scaleY(1f).setDuration(260).setInterpolator(overshoot).start()
                    UiSounds.playClick(v.context)
                }
                MotionEvent.ACTION_CANCEL ->
                    v.animate().scaleX(1f).scaleY(1f).setDuration(260).setInterpolator(overshoot).start()
            }
            false
        }
    }

    private fun buildUi() {
        root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.gravity = Gravity.CENTER
        val pad = dp(24)
        root.setPadding(pad, pad, pad, pad)

        // WiiDS ships no game artwork: the wordmark is plain text.
        val title = TextView(this)
        title.text = "WiiDS"
        title.textSize = 44f
        title.typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        title.setTextColor(Color.WHITE)
        title.gravity = Gravity.CENTER
        title.setShadowLayer(6f, 0f, 3f, Color.argb(160, 0, 0, 0))
        title.layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        title.setPadding(0, dp(8), 0, 0)

        val subtitle = TextView(this)
        subtitle.text = if (BuildConfig.THOR_BUILD) "AYN Thor · Dual Screen" else "Kart Channel"
        subtitle.setTextColor(Color.argb(230, 0xDD, 0xDD, 0xDD))
        subtitle.textSize = 14f
        subtitle.gravity = Gravity.CENTER
        subtitle.setPadding(0, dp(2), 0, dp(28))

        baseButton = Button(this)
        baseButton.setOnClickListener { if (isRomImported()) launchProduct(MainActivity.PRODUCT_BASE) else openGames() }

        retroButton = Button(this)
        retroButton.setOnClickListener {
            if (isRetroRewindInstalled()) {
                launchRetroRewindOrUpdate()
            } else {
                showInstallRetroRewindDialog()
            }
        }

        val buttonParams = LinearLayout.LayoutParams(dp(280), dp(64))
        buttonParams.topMargin = dp(14)

        retroMenuButton = Button(this)
        retroMenuButton.text = "⋮" // vertical ellipsis ("3 dots")
        retroMenuButton.textSize = 18f
        retroMenuButton.setOnClickListener { showRetroRewindMenuDialog() }
        styleChannelButton(retroMenuButton, Color.rgb(0xF2, 0x5C, 0x4C), Color.rgb(0xD1, 0x2A, 0x1E), dp(14).toFloat())

        val retroRowParams =
            LinearLayout.LayoutParams(dp(280), dp(64))
        retroRowParams.topMargin = dp(14)
        val retroRow = LinearLayout(this)
        retroRow.orientation = LinearLayout.HORIZONTAL
        // RR appears only when its standalone native product was explicitly packaged.
        if (BuildConfig.THOR_BUILD && !BuildConfig.THOR_RETRO_REWIND) retroRow.visibility = View.GONE
        val retroButtonParams =
            LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f)
        val retroMenuButtonParams =
            LinearLayout.LayoutParams(dp(56), ViewGroup.LayoutParams.MATCH_PARENT)
        retroMenuButtonParams.marginStart = dp(8)
        retroRow.addView(retroButton, retroButtonParams)
        retroRow.addView(retroMenuButton, retroMenuButtonParams)

        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal)
        progressBar.max = 1000
        progressBar.visibility = View.GONE
        val progressParams = LinearLayout.LayoutParams(dp(280), ViewGroup.LayoutParams.WRAP_CONTENT)
        progressParams.topMargin = dp(14)

        statusText = TextView(this)
        statusText.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { updateLowerStatus() }
            override fun afterTextChanged(s: android.text.Editable?) {}
        })
        statusText.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        statusText.textSize = 13f
        statusText.gravity = Gravity.CENTER
        statusText.setPadding(0, dp(16), 0, 0)

        // Shown only once a newer DriftDroid release has been seen (see checkForUpdate).
        updateButton = Button(this)
        updateButton.text = "Update available"
        updateButton.textSize = 13f
        updateButton.setOnClickListener { showUpdate() }
        val updateButtonParams = LinearLayout.LayoutParams(dp(260), dp(44))
        updateButtonParams.topMargin = dp(12)
        styleChannelButton(updateButton, Color.rgb(0x3F, 0xA9, 0x5A), Color.rgb(0x22, 0x7A, 0x3C), dp(12).toFloat())
        updateButton.visibility = if (AppUpdate.known(this) != null) View.VISIBLE else View.GONE

        diagnosticsButton = Button(this)
        diagnosticsButton.text = "Export Diagnostics"
        diagnosticsButton.textSize = 11f
        diagnosticsButton.setOnClickListener { launchDiagnosticsExportPicker() }
        val diagnosticsButtonParams =
            LinearLayout.LayoutParams(dp(260), dp(40))
        diagnosticsButtonParams.topMargin = dp(24)
        styleChannelButton(
            diagnosticsButton,
            Color.rgb(0x5A, 0x5A, 0x5A),
            Color.rgb(0x3A, 0x3A, 0x3A),
            dp(12).toFloat(),
        )
        diagnosticsButton.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        diagnosticsButton.setShadowLayer(0f, 0f, 0f, 0)

        serverSettingsButton = Button(this)
        serverSettingsButton.text = "Experimental Server Settings..."
        serverSettingsButton.textSize = 11f
        serverSettingsButton.setOnClickListener { PrivateServerSettings.show(this) }
        val serverSettingsButtonParams =
            LinearLayout.LayoutParams(dp(260), dp(40))
        serverSettingsButtonParams.topMargin = dp(8)
        styleChannelButton(
            serverSettingsButton,
            Color.rgb(0x5A, 0x5A, 0x5A),
            Color.rgb(0x3A, 0x3A, 0x3A),
            dp(12).toFloat(),
        )
        serverSettingsButton.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        serverSettingsButton.setShadowLayer(0f, 0f, 0f, 0)

        // Renderer choice lives here because a device with no working Vulkan driver crashes before
        // the in-game settings exist (GitHub issue #3).
        graphicsButton = Button(this)
        graphicsButton.text = "Graphics..."
        graphicsButton.textSize = 11f
        graphicsButton.setOnClickListener { GraphicsSettings.show(this) }
        val graphicsButtonParams =
            LinearLayout.LayoutParams(dp(260), dp(40))
        graphicsButtonParams.topMargin = dp(8)
        styleChannelButton(
            graphicsButton,
            Color.rgb(0x5A, 0x5A, 0x5A),
            Color.rgb(0x3A, 0x3A, 0x3A),
            dp(12).toFloat(),
        )
        graphicsButton.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        graphicsButton.setShadowLayer(0f, 0f, 0f, 0)

        // Custom home-screen shortcut icon. Lives here rather than in the in-game sidebar because
        // it opens the gallery picker, which must not run over a live game surface (see the
        // Mods... button below for the crash that caused).
        iconButton = Button(this)
        iconButton.text = "App icon..."
        iconButton.textSize = 11f
        iconButton.setOnClickListener { IconSettings.show(this) }
        val iconButtonParams =
            LinearLayout.LayoutParams(dp(260), dp(40))
        iconButtonParams.topMargin = dp(8)
        styleChannelButton(
            iconButton,
            Color.rgb(0x5A, 0x5A, 0x5A),
            Color.rgb(0x3A, 0x3A, 0x3A),
            dp(12).toFloat(),
        )
        iconButton.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        iconButton.setShadowLayer(0f, 0f, 0f, 0)

        // Import/manage from here, not from the in-game settings sidebar - launching Android's
        // file picker while a live game session's GPU surface is running lost the WebGPU/Vulkan
        // surface on-device ("QueuePresent failed with VK_ERROR_SURFACE_LOST_KHR") and crashed
        // the game. No such surface exists yet on this screen, so it's safe here - same reasoning
        // as why Retro Rewind's own install flow only ever runs from this Activity.
        modsButton = Button(this)
        modsButton.text = "Mods..."
        modsButton.textSize = 11f
        modsButton.setOnClickListener { ModManager.showManagerDialog(this) }
        val modsButtonParams =
            LinearLayout.LayoutParams(dp(260), dp(40))
        modsButtonParams.topMargin = dp(8)
        styleChannelButton(
            modsButton,
            Color.rgb(0x5A, 0x5A, 0x5A),
            Color.rgb(0x3A, 0x3A, 0x3A),
            dp(12).toFloat(),
        )
        modsButton.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        modsButton.setShadowLayer(0f, 0f, 0f, 0)

        // A square channel-style tile, same idea as baseButton/retroButton but square (96dp,
        // matching the eventual custom Mii icon art) rather than a wide banner - Mii Data isn't a
        // product to launch, but it's frequent enough (importing/exporting Miis) to earn its own
        // channel rather than living in the pale utility-button stack below.
        miiDataButton = Button(this)
        miiDataButton.setOnClickListener { showMiiDataMenuDialog() }
        val miiDataButtonParams = LinearLayout.LayoutParams(dp(96), dp(96))
        miiDataButtonParams.topMargin = dp(14)
        miiDataButton.text = "Mii Data"
        miiDataButton.textSize = 14f
        miiDataButton.isAllCaps = false
        miiDataButton.setTextColor(Color.WHITE)
        styleChannelButton(miiDataButton, Color.rgb(0x6C, 0xB4, 0xE8), Color.rgb(0x2F, 0x7B, 0xC0), dp(20).toFloat())

        root.addView(title)
        root.addView(subtitle)
        // Every WiiDS game, not only Mario Kart: a player may own any of them.
        fun gameButton(label: String, top: Int, bottom: Int, shown: () -> Boolean, action: () -> Unit) {
            val button = Button(this)
            button.text = label
            button.textSize = 15f
            button.isAllCaps = false
            button.setTextColor(Color.WHITE)
            button.setOnClickListener { action() }
            styleChannelButton(button, top, bottom, dp(14).toFloat())
            root.addView(button, LinearLayout.LayoutParams(dp(280), dp(56)).apply { topMargin = dp(10) })
            otherGameButtons[button] = shown
        }
        gameButton("Your games", Color.rgb(0x4C, 0x8D, 0xF2), Color.rgb(0x1E, 0x5B, 0xC6), { true }) { openGames() }
        if (BuildConfig.THOR_BUILD) gameButton("Check for updates", Color.rgb(0x4C, 0x8D, 0xF2), Color.rgb(0x1E, 0x5B, 0xC6), { true }) {
            startActivity(Intent(this, WiiDsUpdateActivity::class.java))
        }
        if (BuildConfig.THOR_BUILD) gameButton("Game shortcuts", Color.rgb(0x4C, 0x8D, 0xF2), Color.rgb(0x1E, 0x5B, 0xC6), { true }) { GameShortcuts.show(this) }
        if (BuildConfig.THOR_BUILD) gameButton("Save manager", Color.rgb(0x4C, 0x8D, 0xF2), Color.rgb(0x1E, 0x5B, 0xC6), { true }) {
            startActivity(Intent(this, SaveManagerActivity::class.java))
        }
        root.addView(baseButton, buttonParams)
        gameButton("New Super Mario Bros. Wii", Color.rgb(0xF2, 0x5C, 0x4C), Color.rgb(0xD1, 0x2A, 0x1E), { nsmbInstalled }) { launchNsmbFromLibrary() }
        gameButton("MSMBWii: Just a Little Adventure", Color.rgb(0x7A, 0x5C, 0xE6), Color.rgb(0x4B, 0x34, 0xB4), { msmbwiiInstalled }) { launchMsmbwiiFromLibrary() }
        gameButton("Super Mario Sunshine", Color.rgb(0xF5, 0xA6, 0x23), Color.rgb(0xD9, 0x7A, 0x10), { sunshineInstalled }) { launchSunshineFromLibrary() }
        gameButton("Super Mario Eclipse", Color.rgb(0xA5, 0x7F, 0xD0), Color.rgb(0x55, 0x36, 0x88), { eclipseInstalled }) { launchEclipseFromLibrary() }
        gameButton("Super Mario Galaxy", Color.rgb(0x48, 0x91, 0xD8), Color.rgb(0x16, 0x43, 0x82), { galaxyInstalled }) { launchGalaxyFromLibrary() }
        gameButton("Kaizo Mario Galaxy Rebalanced", Color.rgb(0x48, 0x91, 0xD8), Color.rgb(0x16, 0x43, 0x82), { kaizoInstalled }) { launchKaizoFromLibrary() }
        root.addView(retroRow, retroRowParams)
        root.addView(miiDataButton, miiDataButtonParams)
        root.addView(progressBar, progressParams)
        root.addView(statusText)
        root.addView(updateButton, updateButtonParams)
        root.addView(diagnosticsButton, diagnosticsButtonParams)
        root.addView(graphicsButton, graphicsButtonParams)
        root.addView(iconButton, iconButtonParams)
        root.addView(serverSettingsButton, serverSettingsButtonParams)
        root.addView(modsButton, modsButtonParams)

        // The picker has grown past what fits on-screen in landscape on some phones - confirmed
        // directly on-device: the two lowest buttons (server settings, Mii data) were laid out
        // fine but simply extended below the visible screen with no way to reach them, since a
        // LinearLayout doesn't scroll on its own. A ScrollView guarantees every control stays
        // reachable regardless of screen height/font scale, at the cost of no longer being
        // perfectly vertically centered when everything DOES fit - worth it for correctness.
        val scroll = android.widget.ScrollView(this)
        scroll.background = ScrollingCheckerboardDrawable(this)
        scroll.addView(
            root,
            ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )
        if (!showThorLauncher()) setContentView(scroll)
        refreshChannelButtons()
    }

    private fun refreshChannelButtons() {
        val romInstalled = isRomImported()
        styleTextChannel(baseButton, "Mario Kart Wii", Color.rgb(0x4F, 0xC3, 0xF7), Color.rgb(0x1E, 0x7F, 0xC4),
            locked = !romInstalled, lockedLabel = "Mario Kart Wii · import")
        for ((button, shown) in otherGameButtons) button.visibility = if (shown()) View.VISIBLE else View.GONE

        val retroInstalled = isRetroRewindInstalled()
        styleTextChannel(retroButton, "Retro Rewind", Color.rgb(0x3D, 0xC7, 0x7A), Color.rgb(0x1E, 0x8A, 0x4D),
            locked = !retroInstalled, lockedLabel = "Retro Rewind · install")
        // Nothing to update or delete until a copy is actually installed.
        retroMenuButton.visibility = if (retroInstalled) View.VISIBLE else View.GONE
        thorLauncher?.invalidate()
        updateLowerStatus()
    }

    private fun isRomImported(): Boolean =
        com.driftdroid.android.rom.RomImportOverlay.isRomAlreadyImported(File(filesDir, "WiiCompiled/DiscData"))

    /**
     * Every game's controller layout, from one place. Sunshine and Eclipse have their own
     * button-assignment screens (captured before SDL, saved per controller); the others change
     * buttons inside the game, so this says where.
     */
    private fun showControllerButtons() {
        val games = arrayOf("Super Mario Sunshine", "Super Mario Eclipse", "Super Mario Galaxy",
            "New Super Mario Bros. Wii and MSMBWii", "Mario Kart Wii and Retro Rewind")
        ThorDialogs.builder(this)
            .setTitle("Controller buttons")
            .setItems(games) { _, which ->
                when (which) {
                    0 -> startActivity(Intent(this, dev.chasem.smslauncher.ControllerConfigActivity::class.java))
                    1 -> startActivity(Intent(this, dev.chasem.eclipselauncher.ControllerConfigActivity::class.java))
                    else -> ThorDialogs.builder(this)
                        .setTitle(games[which])
                        .setMessage(when (which) {
                            2 -> "In Galaxy, press Back to open its settings, then choose Controls and Buttons. " +
                                "Pick what each button does; Reset returns the standard layout."
                            3 -> "In the game, choose Settings in the pause menu (the world map's + menu, or a level's pause gear), then the " +
                                "Controller tab. Choose a button and press the one you want for it."
                            else -> "In the game, press Back to open its settings, then choose Controller. Choose a " +
                                "button and press the one you want for it."
                        })
                        .setPositiveButton("OK", null)
                        .show()
                }
            }
            .show()
    }

    private fun showRetroRewindMenuDialog() {
        if (BuildConfig.THOR_BUILD) { openThorRetroRewindUpdates(); return }
        ThorDialogs.builder(this)
            .setTitle("Retro Rewind")
            .setItems(arrayOf("Check for updates", "Reinstall", "Delete")) { _, which ->
                when (which) {
                    0 -> startDeltaUpdate()
                    1 -> showInstallRetroRewindDialog()
                    2 -> showDeleteRetroRewindDialog()
                }
            }
            .show()
    }

    private fun showDeleteRetroRewindDialog() {
        ThorDialogs.builder(this)
            .setTitle("Delete Retro Rewind")
            .setMessage("This removes the installed Retro Rewind files from the app. You'll need to import them again to play.")
            .setPositiveButton("Delete") { _, _ ->
                retroRewindRoot().deleteRecursively()
                refreshChannelButtons()
                statusText.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
                statusText.text = "Retro Rewind deleted."
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun isRetroRewindInstalled(): Boolean =
        File(retroRewindRoot(), "Binaries/Code.pul").isFile

    private fun retroRewindRoot(): File = File(filesDir, MainActivity.RETRO_REWIND_STAGING_SUBDIR)

    /**
     * Starts Retro Rewind - unless the installed copy holds game code this build isn't compiled
     * for, which is what an install made by an older DriftDroid looks like after the app has been
     * updated for a newer Retro Rewind. Launching that would misbehave or crash, so it is brought
     * up to date first (the small per-version updates, not a new 1.9GB download) and the player
     * taps again once that has finished.
     */
    private fun launchRetroRewindOrUpdate() {
        val root = retroRewindRoot()
        retroButton.isEnabled = false
        Thread {
            val supported = RetroRewindCatalog.isSupportedCodePul(RetroRewindCatalog.installedCodePulSha256(root))
            mainHandler.post {
                retroButton.isEnabled = true
                if (isFinishing || isDestroyed) return@post
                if (supported) {
                    launchProduct(MainActivity.PRODUCT_RETRO_REWIND)
                } else if (BuildConfig.THOR_BUILD) {
                    openThorRetroRewindUpdates()
                } else {
                    val installed = RetroRewindCatalog.installedVersion(root)
                    beginImportUi(
                        "Retro Rewind ${installed ?: ""} needs updating for this version of DriftDroid - updating it now...",
                    )
                    RetroRewindInstallService.startDeltaUpdate(this)
                    startStatusPolling()
                }
            }
        }.start()
    }

    /**
     * Plain-language reason the selected disc cannot start right now, mirroring the gates in
     * [launchProduct] and [launchRetroRewindOrUpdate]; null lets the launch animation proceed.
     * It never begins a runtime itself. The real launch action still re-checks everything.
     */
    private fun thorLaunchBlocker(product: Int): String? {
        SaveManagerProfiles.blocker(this)?.let { return it }
        if (product == KAIZO_CASE) {
            refreshNsmbInstalled()
            if (!kaizoInstalled) mainHandler.post { openGames() }
            return if (kaizoInstalled) null else "Set up Kaizo Rebalanced first."
        }
        if (product == GALAXY_CASE) {
            refreshNsmbInstalled()
            return if (galaxyInstalled) null else "Set up Super Mario Galaxy first."
        }
        if (product == ECLIPSE_CASE) {
            refreshNsmbInstalled()
            if (!eclipseInstalled) mainHandler.post { openGames() }
            return if (eclipseInstalled) null else ECLIPSE_NOT_INSTALLED
        }
        if (product == MSMBWII_CASE) {
            refreshNsmbInstalled()
            if (!msmbwiiInstalled) mainHandler.post { openGames() }
            return if (msmbwiiInstalled) null else MSMBWII_NOT_INSTALLED
        }
        if (product == SUNSHINE_CASE) {
            refreshNsmbInstalled()
            if (!sunshineInstalled) mainHandler.post { openGames() }
            return if (sunshineInstalled) null else SUNSHINE_NOT_INSTALLED
        }
        if (product == NSMB_CASE) {
            // Its own processes: Mario Kart data, updates and session gates never apply. The
            // game's entry point validates its imported data itself.
            refreshNsmbInstalled()
            if (!nsmbInstalled) mainHandler.post { openGames() }
            return if (nsmbInstalled) null else NSMB_NOT_INSTALLED
        }
        if (BuildConfig.THOR_BUILD && BuildConfig.THOR_RETRO_REWIND) {
            if (ThorRetroRewindUpdateStatus.snapshot.running) return "A Retro Rewind update is in progress. Please wait for it to finish."
            if (ThorUpdateRestart.requested(this)) return "An update is waiting to finish. Press Y to open Retro Rewind updates."
            if (ThorRetroRewindUpdateService.needsRecovery(this)) return "Retro Rewind needs recovery. Press Y to open updates."
        }
        if (ThorGameSession.isUpdating()) return "Game files are being updated. Please wait."
        if (!isRomImported()) {
            mainHandler.post { openGames() }
            return "Import your Mario Kart Wii disc first."
        }
        if (product == 1) {
            if (!BuildConfig.THOR_RETRO_REWIND) return "Retro Rewind is not part of this build."
            if (!isRetroRewindInstalled()) return "Retro Rewind is not installed. Press Y to install it."
            if (retroCodeSupported == false) return "This Retro Rewind version needs a compatible update. Press Y to open updates."
        }
        return null
    }

    private fun refreshRetroCodeSupport() {
        if (!BuildConfig.THOR_RETRO_REWIND || !isRetroRewindInstalled()) { retroCodeSupported = null; return }
        val root = retroRewindRoot()
        Thread {
            retroCodeSupported = runCatching {
                RetroRewindCatalog.isSupportedCodePul(RetroRewindCatalog.installedCodePulSha256(root))
            }.getOrNull()
        }.apply { isDaemon = true; name = "ThorRetroCodeCheck" }.start()
    }
    /** The Games screen: finds the player's disc images and imports each game's exact disc. */
    private fun openGames() {
        SaveManagerProfiles.blocker(this)?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show(); return }
        if (!isFinishing && !isDestroyed) startActivity(Intent(this, GamesActivity::class.java))
    }

    // --- Finding the player's own disc image on storage (no file picker) ---

    private var romScanPending: Int? = null
    private var romScanRunning = false

    /** Ask for All files access if needed, scan storage, then offer what was found. */
    private fun findRomOnStorage(product: Int) {
        if (romScanRunning || isFinishing || isDestroyed) return
        if (!com.driftdroid.android.rom.RomScanner.hasAccess()) {
            ThorDialogs.builder(this)
                .setTitle("Find your game automatically")
                .setMessage("To find your Mario Kart Wii disc image (.iso or .wbfs) anywhere on this device or its " +
                    "SD card, allow All files access on the next screen. Only disc headers are read; nothing is uploaded.")
                .setPositiveButton("Allow access") { _, _ ->
                    romScanPending = product
                    val uri = Uri.parse("package:$packageName")
                    runCatching { startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, uri)) }
                        .onFailure { runCatching { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) } }
                }
                .setNeutralButton("Choose file") { _, _ -> launchProductWithRom(product, null) }
                .setNegativeButton("Cancel", null)
                .show()
            return
        }
        romScanRunning = true
        statusText.text = "Searching this device for Mario Kart Wii..."
        Thread {
            val found = runCatching { com.driftdroid.android.rom.RomScanner.scan(applicationContext) }.getOrDefault(emptyList())
            mainHandler.post {
                romScanRunning = false
                statusText.text = ""
                if (!isFinishing && !isDestroyed) showRomScanResult(product, found)
            }
        }.apply { isDaemon = true; name = "ThorRomScan" }.start()
    }

    private fun showRomScanResult(product: Int, found: List<com.driftdroid.android.rom.RomScanner.Candidate>) {
        val good = found.filter { it.supported }
        fun gb(file: File) = "%.1f GB".format(file.length() / 1_073_741_824.0)
        when {
            good.size == 1 -> {
                val rom = good[0]
                ThorDialogs.builder(this)
                    .setTitle("Mario Kart Wii found")
                    .setMessage("${rom.file.name}\n${rom.file.parent}\nPAL RMCP01, ${gb(rom.file)}\n\n" +
                        "Install it now? The game is copied into the app once; your file is left as it is.")
                    .setPositiveButton("Install") { _, _ -> launchProductWithRom(product, rom.file) }
                    .setNegativeButton("Not now", null)
                    .show()
            }
            good.size > 1 -> {
                val labels = good.map { "${it.file.name}  (${gb(it.file)})" }.toTypedArray()
                ThorDialogs.builder(this)
                    .setTitle("Choose your Mario Kart Wii disc")
                    .setItems(labels) { _, which -> launchProductWithRom(product, good[which].file) }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
            else -> {
                val rvzKind = com.driftdroid.android.rom.RomScanner.Kind.RVZ_UNSUPPORTED
                val rvz = found.firstOrNull { it.kind == rvzKind && it.gameId == "RMCP01" }
                // Other Mario Kart Wii regions (RMCE01 USA, RMCJ01 Japan, RMCK01 Korea).
                val region = found.firstOrNull { it.gameId.startsWith("RMC") && it.gameId != "RMCP01" }
                val message = when {
                    rvz != null -> "Found ${rvz.file.name}, but it is a compressed RVZ file. Convert it to ISO " +
                        "(in Dolphin: long-press the game, Convert File, format ISO), then search again."
                    region != null -> "Found ${region.file.name}, but it is game ID ${region.gameId}" +
                        (if (region.gameId == "RMCE01") " (USA)" else "") + ". Only the PAL (European) release, " +
                        "RMCP01, is supported. Dump your own PAL copy and search again."
                    else -> "No Mario Kart Wii disc image was found. Copy your own .iso or .wbfs anywhere on this " +
                        "device or its SD card and try again, or choose the file yourself."
                }
                ThorDialogs.builder(this)
                    .setTitle("Mario Kart Wii not found")
                    .setMessage(message)
                    .setPositiveButton("Search again") { _, _ -> findRomOnStorage(product) }
                    .setNeutralButton("Choose file") { _, _ -> launchProductWithRom(product, null) }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        }
    }

    /** Starts the game screen, which extracts [rom] directly (or shows its file picker when null). */
    private fun launchProductWithRom(product: Int, rom: File?) {
        launchProduct(if (product == 1 && BuildConfig.THOR_RETRO_REWIND) MainActivity.PRODUCT_RETRO_REWIND else MainActivity.PRODUCT_BASE,
            rom?.absolutePath)
    }

    private fun launchProduct(product: String, romPath: String? = null) {
        SaveManagerProfiles.blocker(this)?.let {
            Toast.makeText(this, it, Toast.LENGTH_LONG).show()
            return
        }
        if (BuildConfig.THOR_BUILD && (ThorRetroRewindUpdateStatus.snapshot.running ||
                ThorUpdateRestart.requested(this) || ThorRetroRewindUpdateService.needsRecovery(this) || !ThorGameSession.tryBeginRuntime())) {
            openThorRetroRewindUpdates()
            return
        }
        val intent = Intent(this, MainActivity::class.java)
        intent.putExtra(MainActivity.EXTRA_PRODUCT, product)
        if (romPath != null) intent.putExtra(MainActivity.EXTRA_ROM_PATH, romPath)
        startActivity(intent)
    }

    /**
     * A custom setView() layout, not AlertDialog's setItems() - setMessage() and setItems() are
     * mutually exclusive on AlertDialog.Builder (the message view wins and the items list is
     * simply never inflated when both are set), which is exactly why this used to look like "a
     * message pops up and then nothing happens": the three install options were never actually
     * on screen to tap, confirmed directly on-device via a UI dump showing only the message and
     * Cancel button present.
     */
    private fun showInstallRetroRewindDialog() {
        if (BuildConfig.THOR_BUILD) { openThorRetroRewindUpdates(); return }
        val container = LinearLayout(this)
        container.orientation = LinearLayout.VERTICAL
        val pad = dp(20)
        container.setPadding(pad, dp(4), pad, 0)

        val message = TextView(this)
        message.text =
            "Retro Rewind is a separate, optional community mod. The download option below " +
                "checks update.rwfc.net for the newest release this build of DriftDroid can " +
                "actually run and fetches it straight from there (verified by hash before " +
                "install) - or hand over a copy you already have (search \"Retro Rewind " +
                "Mario Kart Wii\" or use the Wheel Wizard tool)."
        message.setTextColor(Color.argb(220, 255, 255, 255))
        message.textSize = 14f
        message.setPadding(0, 0, 0, dp(16))
        container.addView(message)

        // The dialog itself doesn't scroll a custom setView() by default - with the message plus
        // three option buttons this reliably overflowed the dialog's max height on-device (the
        // "Select folder" option was entirely unreachable, "Select .zip" half-hidden under
        // Cancel). Wrapping in a ScrollView, same fix as the main picker screen's overflow.
        val scroll = android.widget.ScrollView(this)
        scroll.addView(container, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        val dialog =
            ThorDialogs.builder(this)
                .setTitle("Install Retro Rewind")
                .setView(scroll)
                .setNegativeButton("Cancel", null)
                .create()

        fun optionButton(label: String, onClick: () -> Unit): Button {
            val button = Button(this)
            button.text = label
            button.setOnClickListener {
                dialog.dismiss()
                onClick()
            }
            val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            params.bottomMargin = dp(8)
            container.addView(button, params)
            return button
        }

        optionButton("Download newest supported release (~1.9GB)") { startLatestInstall() }
        optionButton("Select .zip file I already have") { launchRetroRewindZipPicker() }
        optionButton("Select an already-extracted folder") { launchRetroRewindFolderPicker() }

        dialog.show()
    }

    private fun startDownloadInstall() {
        beginImportUi("Downloading Retro Rewind ${RetroRewindRelease.VERSION}...")
        RetroRewindInstallService.startDownloadInstall(this)
        startStatusPolling()
    }

    /** Installs whatever the live release catalog says is newest AND runnable on this build,
     * rather than only ever the release that was current when the app was compiled. */
    private fun startLatestInstall() {
        beginImportUi("Checking for the newest Retro Rewind...")
        RetroRewindInstallService.startLatestInstall(this)
        startStatusPolling()
    }

    /** Applies the mod's own small per-version update archives to the existing install - a few MB
     * rather than another ~1.9GB. */
    private fun startDeltaUpdate() {
        if (BuildConfig.THOR_BUILD) { openThorRetroRewindUpdates(); return }
        beginImportUi("Checking for Retro Rewind updates...")
        RetroRewindInstallService.startDeltaUpdate(this)
        startStatusPolling()
    }

    private fun openThorRetroRewindUpdates() {
        if (BuildConfig.THOR_RETRO_REWIND) startActivity(Intent(this, ThorRetroRewindUpdatesActivity::class.java))
    }

    private fun launchRetroRewindFolderPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        // FLAG_GRANT_PERSISTABLE_URI_PERMISSION has to be asked for HERE, on the picker intent -
        // without it the grant handed back is transient, and takePersistableUriPermission() on it
        // throws SecurityException ("No persistable permission grants found"), which was thrown
        // straight out of onActivityResult and took the whole app down the instant a folder was
        // picked (reported as "it just boots me out of the app").
        intent.addFlags(
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION,
        )
        startActivityForResult(intent, REQUEST_CODE_PICK_RETRO_REWIND_FOLDER)
    }

    private fun launchRetroRewindZipPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        // Not restricted to "application/zip": many file managers/browsers hand a downloaded zip
        // over with a generic or missing MIME type - the extension check on the picked file (see
        // onActivityResult) is the real gate, same reasoning as RomImportOverlay's ISO/WBFS picker.
        intent.type = "*/*"
        startActivityForResult(intent, REQUEST_CODE_PICK_RETRO_REWIND_ZIP)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        when (requestCode) {
            REQUEST_CODE_PICK_RETRO_REWIND_FOLDER -> handleFolderPicked(data?.data)
            REQUEST_CODE_PICK_RETRO_REWIND_ZIP -> handleZipPicked(data?.data)
            REQUEST_CODE_EXPORT_DIAGNOSTICS -> handleDiagnosticsDestinationPicked(data?.data)
            REQUEST_CODE_EXPORT_MII_DATA -> handleMiiExportDestinationPicked(data?.data)
            REQUEST_CODE_IMPORT_MII_DATA -> handleMiiImportSourcePicked(data?.data)
            REQUEST_CODE_IMPORT_SINGLE_MII -> handleSingleMiiSourcePicked(data?.data)
            ModManager.REQUEST_CODE_PICK_ZIP -> ModManager.onZipPicked(this, data?.data)
            GpuDriverSettings.REQUEST_CODE_PICK_ZIP -> GpuDriverSettings.onZipPicked(this, data?.data)
            IconSettings.REQUEST_CODE_PICK_IMAGE -> IconSettings.onImagePicked(this, data?.data)
        }
    }

    /**
     * The Mii Channel's own database (RFL_DB.dat, distinct from RFL_Res.dat - the read-only face
     * resource seeded from the ISO by nand_fs.cpp's SeedFaceLibResource, never written by the
     * player) - lives at NAND/shared2/menu/FaceLib/RFL_DB.dat under filesDir/WiiCompiled, since
     * ApplicationDataDirectory() / "NAND" (nand_path.h) resolves to that same directory on
     * Android. A fixed-size file (779968 bytes, the standard RFL database size on real Wii/Wii
     * consoles) - checked before import so a wrong/corrupt file can't silently wedge the Mii
     * Channel, with the previous database always backed up first regardless.
     */
    private fun miiDatabaseFile(): File =
        File(filesDir, "WiiCompiled/NAND/shared2/menu/FaceLib/RFL_DB.dat")

    /** Custom setView(), not setItems() - see showInstallRetroRewindDialog's doc comment for why
     * AlertDialog.Builder can't combine setMessage() with setItems() (confirmed directly
     * on-device a second time here: the three Mii options never rendered, same root cause as the
     * earlier Install Retro Rewind bug, just missed in this second dialog). */
    private fun showMiiDataMenuDialog() {
        val container = LinearLayout(this)
        container.orientation = LinearLayout.VERTICAL
        val pad = dp(20)
        container.setPadding(pad, dp(4), pad, 0)

        val message = TextView(this)
        message.text =
            "Export or replace the Mii Channel's saved Miis (RFL_DB.dat), or add a single " +
                "custom Mii (.rcd) into a free slot. Start Mario Kart Wii at least once before " +
                "this file exists."
        message.setTextColor(Color.argb(220, 255, 255, 255))
        message.textSize = 14f
        message.setPadding(0, 0, 0, dp(16))
        container.addView(message)

        val scroll = android.widget.ScrollView(this)
        scroll.addView(container, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        val dialog =
            ThorDialogs.builder(this)
                .setTitle("Mii Data")
                .setView(scroll)
                .setNegativeButton("Cancel", null)
                .create()

        fun optionButton(label: String, onClick: () -> Unit) {
            val button = Button(this)
            button.text = label
            button.setOnClickListener {
                dialog.dismiss()
                onClick()
            }
            val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            params.bottomMargin = dp(8)
            container.addView(button, params)
        }

        optionButton("Export whole database...") { launchMiiExportPicker() }
        optionButton("Replace whole database...") { launchMiiImportPicker() }
        optionButton("Import single Mii (.rcd)...") { launchSingleMiiImportPicker() }

        dialog.show()
    }

    private fun launchSingleMiiImportPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        startActivityForResult(intent, REQUEST_CODE_IMPORT_SINGLE_MII)
    }

    private fun handleSingleMiiSourcePicked(source: Uri?) {
        if (source == null) return
        insertSingleMii(source)
    }

    /**
     * Inserts one standalone Mii (.rcd, the 74-byte Wii Mii exchange format) into a free slot in
     * the existing database - see MiiDatabaseEditor.kt for the format details and where they came
     * from. Backs up the current database first, same as the whole-database Replace path.
     */
    private fun insertSingleMii(source: Uri) {
        try {
            val size = queryFileSize(source)
            if (size >= 0 && size != MiiDatabaseEditor.MII_RECORD_BYTES.toLong()) {
                throw IllegalArgumentException(
                    "not a single-Mii file (expected ${MiiDatabaseEditor.MII_RECORD_BYTES} bytes, got $size)",
                )
            }
            val record =
                contentResolver.openInputStream(source)?.use { it.readBytes() }
                    ?: throw IllegalStateException("couldn't open the picked file")

            val dbFile = miiDatabaseFile()
            if (!dbFile.isFile) {
                throw IllegalStateException("no existing Mii database - start Mario Kart Wii once first")
            }
            val database = dbFile.readBytes()

            when (val result = MiiDatabaseEditor.insertMii(database, record)) {
                is MiiDatabaseEditor.Result.Failure -> throw IllegalStateException(result.message)
                is MiiDatabaseEditor.Result.Success -> {
                    val backup = File(dbFile.parentFile, "RFL_DB.dat.bak")
                    dbFile.copyTo(backup, overwrite = true)
                    dbFile.writeBytes(result.database)
                    statusText.setTextColor(Color.rgb(20, 140, 60))
                    statusText.text = "Mii added to slot ${result.slotIndex + 1} of 100."
                }
            }
        } catch (e: Exception) {
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "Mii import failed: ${e.message ?: "unknown error"}"
        }
    }

    private fun launchMiiExportPicker() {
        if (!miiDatabaseFile().isFile) {
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "No Mii database yet - start Mario Kart Wii once first."
            return
        }
        val stamp = SimpleDateFormat("yyyy-MM-dd_HHmmss", Locale.US).format(Date())
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "application/octet-stream"
        intent.putExtra(Intent.EXTRA_TITLE, "DriftDroid-Mii-$stamp.dat")
        startActivityForResult(intent, REQUEST_CODE_EXPORT_MII_DATA)
    }

    private fun handleMiiExportDestinationPicked(destination: Uri?) {
        if (destination == null) return
        try {
            contentResolver.openOutputStream(destination)?.use { output ->
                miiDatabaseFile().inputStream().use { input -> input.copyTo(output) }
            } ?: throw IllegalStateException("couldn't open the chosen destination")
            statusText.setTextColor(Color.rgb(20, 140, 60))
            statusText.text = "Mii data exported."
        } catch (e: Exception) {
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "Mii export failed: ${e.message ?: "unknown error"}"
        }
    }

    private fun launchMiiImportPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        startActivityForResult(intent, REQUEST_CODE_IMPORT_MII_DATA)
    }

    private fun handleMiiImportSourcePicked(source: Uri?) {
        if (source == null) return
        ThorDialogs.builder(this)
            .setTitle("Replace Mii Data")
            .setMessage(
                "This replaces every Mii in the Mii Channel with the picked file's Miis. The " +
                    "current database is backed up first (RFL_DB.dat.bak) in case this goes wrong.",
            )
            .setPositiveButton("Replace") { _, _ -> importMiiDatabase(source) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun importMiiDatabase(source: Uri): Unit =
        try {
            val picked =
                contentResolver.openInputStream(source)?.use { it.readBytes() }
                    ?: throw IllegalStateException("couldn't open the picked file")
            // Real CRC-16 validation (matches KartPad's own check), not just a size comparison -
            // catches a corrupted-but-right-size file that a size-only check would wave through.
            if (!MiiDatabaseEditor.isValidDatabase(picked)) {
                throw IllegalArgumentException("that doesn't look like a valid Mii database file")
            }
            val dest = miiDatabaseFile()
            if (!dest.isFile) {
                throw IllegalStateException("no existing Mii database - start Mario Kart Wii once first")
            }
            val backup = File(dest.parentFile, "RFL_DB.dat.bak")
            dest.copyTo(backup, overwrite = true)
            dest.writeBytes(picked)
            statusText.setTextColor(Color.rgb(20, 140, 60))
            statusText.text = "Mii data replaced (previous copy saved as RFL_DB.dat.bak)."
            Unit
        } catch (e: Exception) {
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "Mii import failed: ${e.message ?: "unknown error"}"
        }

    /**
     * Bundles the runtime's own log files (see android_jni_bridge.cpp's freopen() of
     * stdout/stderr, and main.cpp's per-run Logs/<product>_<epoch>_pid<pid>/console.log folders -
     * both live directly under filesDir/WiiCompiled since ApplicationDataDirectory() resolves to
     * that same path on Android) into a single zip the user picks a destination for. This exists
     * so troubleshooting doesn't require a USB cable and `adb shell run-as ... cat` - the only way
     * this app's logs have been readable until now.
     */
    private fun launchDiagnosticsExportPicker() {
        val stamp = SimpleDateFormat("yyyy-MM-dd_HHmmss", Locale.US).format(Date())
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "application/zip"
        intent.putExtra(Intent.EXTRA_TITLE, "DriftDroid-diagnostics-$stamp.zip")
        startActivityForResult(intent, REQUEST_CODE_EXPORT_DIAGNOSTICS)
    }

    private fun handleDiagnosticsDestinationPicked(destination: Uri?) {
        if (destination == null) return
        statusText.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        statusText.text = "Exporting diagnostics..."
        val thread =
            Thread {
                val error = writeDiagnosticsZip(destination)
                mainHandler.post {
                    if (error == null) {
                        statusText.setTextColor(Color.rgb(20, 140, 60))
                        statusText.text = "Diagnostics exported."
                    } else {
                        statusText.setTextColor(Color.rgb(190, 30, 30))
                        statusText.text = "Diagnostics export failed: $error"
                    }
                }
            }
        thread.start()
    }

    private fun writeDiagnosticsZip(destination: Uri): String? {
        return try {
            val wiiCompiledDir = File(filesDir, "WiiCompiled")
            // Exclude binary guest snapshots and asset/saves trees. The file
            // selector also refuses symlinks and non-runtime filenames.
            val files = DiagnosticsExportFiles.collect(wiiCompiledDir).take(MAX_LOG_FILES)

            val output =
                contentResolver.openOutputStream(destination)
                    ?: return "couldn't open the chosen destination"
            output.use { rawOutput ->
                ZipOutputStream(rawOutput.buffered()).use { zip ->
                    zip.putNextEntry(ZipEntry("README.txt"))
                    zip.write(
                        buildString {
                            appendLine("DriftDroid runtime diagnostics - review before sharing.")
                            appendLine("Device: ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}")
                            appendLine("API level: ${android.os.Build.VERSION.SDK_INT}")
                            appendLine("Each log is truncated to its last $MAX_LOG_FILE_BYTES bytes.")
                            appendLine("Binary memory snapshots and game archives are excluded.")
                            appendLine("Files included: ${files.size}")
                        }.toByteArray(),
                    )
                    zip.closeEntry()

                    val buffer = ByteArray(32 * 1024)
                    for (file in files) {
                        check(DiagnosticsExportFiles.isAllowed(wiiCompiledDir, file)) { "A diagnostic log changed before export." }
                        val entryName =
                            file.relativeTo(wiiCompiledDir).invariantSeparatorsPath
                        zip.putNextEntry(ZipEntry(entryName))
                        RandomAccessFile(file, "r").use { input ->
                            val size = input.length()
                            val start = maxOf(0, size - MAX_LOG_FILE_BYTES)
                            input.seek(start)
                            var remaining = size - start
                            while (remaining > 0) {
                                val count = input.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                                if (count < 0) break
                                zip.write(buffer, 0, count)
                                remaining -= count
                            }
                        }
                        zip.closeEntry()
                    }
                }
            }
            null
        } catch (e: Exception) {
            e.message ?: "unknown error"
        }
    }

    private fun handleFolderPicked(treeUri: Uri?) {
        if (treeUri == null) return
        // Best-effort: the transient grant this Activity already holds is enough to run the copy
        // (the service is the same UID, same process), so a provider that refuses to persist is
        // not a reason to fail the import - and must never be a reason to crash.
        try {
            contentResolver.takePersistableUriPermission(treeUri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: SecurityException) {
        }
        val picked = DocumentFile.fromTreeUri(this, treeUri)
        if (picked == null) {
            showImportError("Couldn't open that folder - try picking it again.")
            return
        }
        // Walking a SAF tree means a content-provider round trip per entry, which on a big folder
        // on slow storage is far too slow for the main thread (an ANR reads to the player exactly
        // like the crash above). Resolve off-thread, then come back to the UI.
        beginImportUi("Checking the selected folder...")
        Thread {
            val resolved =
                try {
                    resolveRetroRewind6(picked)
                } catch (_: Exception) {
                    null
                }
            mainHandler.post {
                if (isFinishing || isDestroyed) return@post
                if (resolved == null) {
                    endImportUi()
                    showImportError(
                        "That folder doesn't contain RetroRewind6/Binaries/Code.pul - pick the " +
                            "folder you extracted Retro Rewind into (or the RetroRewind6 folder itself).",
                    )
                    return@post
                }
                statusText.text = "Copying Retro Rewind files - this can take a while for ~2GB..."
                RetroRewindInstallService.startFolderInstall(this, resolved.uri)
                startStatusPolling()
            }
        }.start()
    }

    private fun showImportError(message: String) {
        statusText.setTextColor(Color.rgb(190, 30, 30))
        statusText.text = message
    }

    private fun handleZipPicked(uri: Uri?) {
        if (uri == null) return
        val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "selected file"
        if (!name.substringAfterLast('.', "").equals("zip", ignoreCase = true)) {
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "\"$name\" isn't a .zip file - pick the Retro Rewind archive you downloaded."
            return
        }
        beginImportUi("Extracting \"$name\" - this can take a while for ~2GB...")
        RetroRewindInstallService.startZipInstall(this, uri, name)
        startStatusPolling()
    }

    private fun queryDisplayName(uri: Uri): String? =
        try {
            contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
            }
        } catch (_: Exception) {
            null
        }

    /** Mirrors the PC installer's RetroRewindSource.ResolveRetroRewind6: accept either the
     * RetroRewind6 folder itself, its parent, or a one-level-deeper wrapper folder. */
    private fun resolveRetroRewind6(picked: DocumentFile): DocumentFile? {
        val candidates = mutableListOf(picked)
        picked.findFile("RetroRewind6")?.let { candidates.add(it) }
        for (child in picked.listFiles()) {
            if (child.isDirectory) {
                child.findFile("RetroRewind6")?.let { candidates.add(it) }
            }
        }
        return candidates.firstOrNull { candidate ->
            candidate.findFile("Binaries")?.findFile("Code.pul")?.isFile == true
        }
    }

    private fun beginImportUi(statusMessage: String) {
        baseButton.isEnabled = false
        retroButton.isEnabled = false
        progressBar.visibility = View.VISIBLE
        // Indeterminate (spinning), not a 0-100% fill: neither the folder-copy nor the zip-extract
        // path knows the total file/byte count up front without a slow pre-scan, so a fixed
        // progress value just sat frozen at 0% the whole time - confirmed directly as "no progress
        // bar... which is weird" (it was there, just never moving, so it looked broken/absent).
        // The file-count text next to it already gives a live sense of movement.
        progressBar.isIndeterminate = true
        statusText.setTextColor(Color.rgb(0xDD, 0xDD, 0xDD))
        statusText.text = statusMessage
    }

    /** Puts the buttons/progress bar back without writing any status text - for bailing out of an
     * import before the service was ever started (the caller sets its own message). */
    private fun endImportUi() {
        progressBar.isIndeterminate = false
        progressBar.visibility = View.GONE
        baseButton.isEnabled = true
        retroButton.isEnabled = true
    }

    private fun finishImportUi(error: String?, filesCount: Int) {
        endImportUi()
        if (error == null) {
            statusText.setTextColor(Color.rgb(20, 140, 60))
            statusText.text =
                RetroRewindInstallStatus.successMessage ?: "Retro Rewind installed ($filesCount files)."
            refreshChannelButtons()
        } else {
            // Leave no half-copied install behind - isRetroRewindInstalled() only checks for
            // Code.pul, and a partial copy that happened to include it would then look
            // "installed" while actually missing tracks/assets. Only when the install really was
            // being rebuilt, though: a refused version or an update check that couldn't finish
            // has not touched the working install, and deleting it then cost the player 1.9GB.
            if (RetroRewindInstallStatus.installIncomplete) {
                retroRewindRoot().deleteRecursively()
            }
            refreshChannelButtons()
            statusText.setTextColor(Color.rgb(190, 30, 30))
            statusText.text = "Install failed: $error"
            // If the reason was a Retro Rewind release this build can't run, the fix is a newer
            // DriftDroid: look for one now instead of at the next hourly check.
            checkForUpdate(force = true)
        }
    }

    /**
     * The actual copy/extract now runs in [RetroRewindInstallService], a foreground service that
     * outlives this Activity's lifecycle (see the service's own doc comment for why - short
     * version: a plain background Thread here died whenever Android reclaimed this Activity's
     * process while backgrounded, losing the whole ~2GB copy). This just reflects
     * [RetroRewindInstallStatus] into the UI, polling rather than needing a callback since a
     * brand new Activity instance (post-recreation) can start polling and pick up exactly where
     * an old, now-gone instance left off.
     */
    private var statusPoller: Runnable? = null

    private fun startStatusPolling() {
        if (statusPoller != null) return
        val poller =
            object : Runnable {
                override fun run() {
                    if (RetroRewindInstallStatus.finished) {
                        statusPoller = null
                        finishImportUi(RetroRewindInstallStatus.error, RetroRewindInstallStatus.filesDone)
                        return
                    }
                    val totalBytes = RetroRewindInstallStatus.totalBytes
                    statusText.text =
                        if (totalBytes > 0) {
                            val doneMb = RetroRewindInstallStatus.downloadedBytes / (1024 * 1024)
                            val totalMb = totalBytes / (1024 * 1024)
                            "${RetroRewindInstallStatus.label}... ${doneMb}MB / ${totalMb}MB"
                        } else {
                            "${RetroRewindInstallStatus.label}... ${RetroRewindInstallStatus.filesDone} files done"
                        }
                    mainHandler.postDelayed(this, 500)
                }
            }
        statusPoller = poller
        mainHandler.post(poller)
    }

    private fun stopStatusPolling() {
        statusPoller?.let { mainHandler.removeCallbacks(it) }
        statusPoller = null
    }

    private fun queryFileSize(uri: Uri): Long =
        try {
            contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.SIZE)
                if (index >= 0 && cursor.moveToFirst() && !cursor.isNull(index)) cursor.getLong(index) else -1L
            } ?: -1L
        } catch (_: Exception) {
            -1L
        }

    companion object {
        /** The Games screen opens by itself at most once per launch. */
        private var gamesOfferedThisProcess = false
        internal const val EXTRA_FRONTEND_GAME = "wiids_frontend_game"
        internal const val EXTRA_RESTART_PRODUCT = "thor_restart_product"
        private const val REQUEST_CODE_PICK_RETRO_REWIND_FOLDER = 0x52520000 // "RR"
        private const val REQUEST_CODE_PICK_RETRO_REWIND_ZIP = 0x52520001
        private const val REQUEST_CODE_EXPORT_DIAGNOSTICS = 0x52520002
        private const val MAX_LOG_FILES = 12
        private const val MAX_LOG_FILE_BYTES = 4L * 1024 * 1024
        private const val REQUEST_CODE_EXPORT_MII_DATA = 0x52520003
        private const val REQUEST_CODE_IMPORT_MII_DATA = 0x52520004
        private const val REQUEST_CODE_IMPORT_SINGLE_MII = 0x52520005
        /** Existing shelf order is stable; Eclipse follows Sunshine in its GameCube stack. */
        private const val LIBRARY_CASES = 8
        /** Base game first, then its mods: they share one stack on the main shelf. */
        private val LIBRARY_BUNDLES = listOf(listOf(0, 1), listOf(2, 3), listOf(4, 5), listOf(6, 7))
        /** Stable game identity of each library case, in shelf order. */
        private val LIBRARY_GAMES = listOf(FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND, FrontendGame.NSMB, FrontendGame.MSMBWII,
            FrontendGame.SUNSHINE, FrontendGame.ECLIPSE, FrontendGame.GALAXY, FrontendGame.KAIZO)
        private const val GALAXY_CASE = ThorLibraryCompanionView.GALAXY
        private const val KAIZO_CASE = ThorLibraryCompanionView.KAIZO
        private const val NSMB_CASE = ThorLibraryCompanionView.NSMB
        private const val SUNSHINE_CASE = ThorLibraryCompanionView.SUNSHINE
        private const val ECLIPSE_CASE = ThorLibraryCompanionView.ECLIPSE
        private const val MSMBWII_CASE = ThorLibraryCompanionView.MSMBWII
        private const val NSMB_NOT_INSTALLED = "Import your New Super Mario Bros. Wii disc on the Your games screen first."
        private const val SUNSHINE_NOT_INSTALLED = "Import your Super Mario Sunshine disc on the Your games screen first."
        private const val ECLIPSE_NOT_INSTALLED = "Import your patched Super Mario Eclipse 1.1.0 disc on the Your games screen first. Eclipse keeps its own saves."
        private const val MSMBWII_NOT_INSTALLED = "MSMBWii needs your New Super Mario Bros. Wii disc and the MSMBWii mod. Open Your games."
    }
}

/**
 * A gray checkerboard wallpaper, tiled and drifting diagonally forever - replaces the earlier sky
 * gradient background. A Drawable (not a View) so it can just be dropped in as `view.background`;
 * animates itself via Drawable.scheduleSelf, which only works because setting a Drawable as a
 * View's background makes that View its Drawable.Callback automatically (no manual wiring needed,
 * and it stops scheduling redraws on its own once the View is gone/detached).
 */
private class ScrollingCheckerboardDrawable(context: android.content.Context) : Drawable() {
    private val tilePx = (28 * context.resources.displayMetrics.density).toInt().coerceAtLeast(8)
    private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
    private val shader: android.graphics.BitmapShader
    private val localMatrix = android.graphics.Matrix()
    private var offsetPx = 0f
    private var lastFrameUptimeMs = android.os.SystemClock.uptimeMillis()
    private var started = false

    init {
        val size = tilePx * 2
        val bitmap = android.graphics.Bitmap.createBitmap(size, size, android.graphics.Bitmap.Config.ARGB_8888)
        val canvas = android.graphics.Canvas(bitmap)
        val light = android.graphics.Paint().apply { color = Color.rgb(0x3A, 0x3A, 0x3A) }
        val dark = android.graphics.Paint().apply { color = Color.rgb(0x22, 0x22, 0x22) }
        val t = tilePx.toFloat()
        val s = size.toFloat()
        canvas.drawRect(0f, 0f, t, t, light)
        canvas.drawRect(t, 0f, s, t, dark)
        canvas.drawRect(0f, t, t, s, dark)
        canvas.drawRect(t, t, s, s, light)
        shader = android.graphics.BitmapShader(
            bitmap, android.graphics.Shader.TileMode.REPEAT, android.graphics.Shader.TileMode.REPEAT,
        )
        paint.shader = shader
    }

    private val tick =
        object : Runnable {
            override fun run() {
                val now = android.os.SystemClock.uptimeMillis()
                val deltaMs = (now - lastFrameUptimeMs).coerceIn(0, 100)
                lastFrameUptimeMs = now
                offsetPx += deltaMs * SPEED_PX_PER_MS
                invalidateSelf()
                scheduleSelf(this, now + FRAME_INTERVAL_MS)
            }
        }

    override fun draw(canvas: android.graphics.Canvas) {
        localMatrix.setTranslate(offsetPx, offsetPx * 0.6f)
        shader.setLocalMatrix(localMatrix)
        canvas.drawRect(bounds, paint)
        // First draw() (right after the background is actually attached/visible) is what starts
        // the animation loop, once - scheduling from init{} instead would need scheduleSelf's
        // Callback (set by View.setBackground) that doesn't exist yet at construction time; after
        // this, `tick` reschedules itself, so draw() must NOT also reschedule on every frame (that
        // would stack a second, independent chain of ticks on top of tick's own, each triggering
        // more invalidateSelf() calls than the last - confirmed this exact mistake in an earlier
        // draft, caught before it shipped).
        if (!started) {
            started = true
            lastFrameUptimeMs = android.os.SystemClock.uptimeMillis()
            scheduleSelf(tick, lastFrameUptimeMs + FRAME_INTERVAL_MS)
        }
    }

    override fun setAlpha(alpha: Int) {}

    override fun setColorFilter(colorFilter: android.graphics.ColorFilter?) {}

    override fun getOpacity(): Int = android.graphics.PixelFormat.OPAQUE

    companion object {
        private const val FRAME_INTERVAL_MS = 32L // ~30fps - plenty smooth for a slow drift
        private const val SPEED_PX_PER_MS = 0.006f // slow: one tile crossed roughly every 9s
    }
}
