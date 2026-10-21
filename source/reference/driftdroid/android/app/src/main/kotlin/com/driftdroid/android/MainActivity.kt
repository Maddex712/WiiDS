package com.driftdroid.android

import android.app.AlarmManager
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Color
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.RelativeLayout
import android.widget.ScrollView
import android.widget.TextView
import com.driftdroid.android.rom.RomImportOverlay
import com.driftdroid.android.dualscreen.ThorDualScreenController
import com.driftdroid.android.dualscreen.RaceMapBridge
import com.driftdroid.android.touch.EditGestureHelper
import com.driftdroid.android.touch.TouchControlsOverlay
import java.io.File
import org.libsdl.app.SDLActivity

/**
 * Extends SDLActivity (not a plain Activity) so SDL's own Java glue owns the whole native
 * lifecycle: creating the SDLSurface/ANativeWindow, calling SDL.setupJNI()/SDL.initialize()
 * (populating the mActivityClass/method-ID globals SDL_VideoInit's touch-input setup needs),
 * and spawning the background thread that dlsym's + calls SDL_main once the surface is ready.
 * Before this, MainActivity was a plain Activity with a debug button that manually re-implemented
 * pieces of that lifecycle (see git history) - fighting SDLActivity's own plumbing kept
 * resurfacing the same class of bug, so this is the "do it the SDL way" version (P5 groundwork).
 *
 * Always started explicitly from ModePickerActivity (the real launcher - see AndroidManifest.xml)
 * with [EXTRA_PRODUCT] set: neither the combined native library (libGameCombined.so - both
 * products, see runtime/cmake/build_combined_android_lib.py, not yet safe to ship - see
 * getLibraries()'s own doc comment) nor a running game has a supported way to reset and switch
 * profiles live - the Original/Retro Rewind choice must be resolved before SDLActivity's own
 * lifecycle (which calls getLibraries()) even starts, and restartIntoProduct() below still fully
 * kills the process rather than trying to switch live.
 */
class MainActivity : SDLActivity() {

    private external fun nativeSetInstallPaths(filesDir: String, dvdRoot: String)
    private external fun nativeSetRetroRewindRoot(retroRewindRoot: String)
    private external fun nativeAddOverlayRoot(overlayRoot: String)
    private external fun nativeSetDetectedWidescreen(widescreen: Boolean)
    private external fun nativeSetActiveProduct(retroRewind: Boolean)
    private external fun nativeToggleSettingsOverlay()

    /** The gear can be hidden (GitHub issue #5); Back then opens the settings sidebar instead. */
    private var settingsButton: Button? = null
    private var nativeSettingsButtonHidden = false

    /** Called from native (settings_overlay.cpp's "Hide the settings button" checkbox). */
    fun onNativeSetSettingsButtonHidden(hidden: Boolean) {
        nativeSettingsButtonHidden = hidden
        runOnUiThread { settingsButton?.visibility = if (hidden) View.GONE else View.VISIBLE }
    }

    /**
     * SDL's key handling swallows KEYCODE_BACK before Android ever calls onBackPressed (it returns
     * true for both the down and up events, see SDLSurface.onKey), so the sidebar has to be opened
     * from here instead. Confirmed on device: the override of onBackPressed never ran.
     */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BACK && event.action == KeyEvent.ACTION_UP) {
            nativeToggleSettingsOverlay()
            return true
        }
        return super.dispatchKeyEvent(event)
    }
    private external fun nativeSetTouchControlsVisibleCache(visible: Boolean)
    private external fun nativeSetDoubleTapAutoHoldCache(enabled: Boolean)
    private external fun nativeReportExternalMediaPlaying(playing: Boolean)
    private external fun nativeReportThermalHeadroom(headroom: Float)

    private var touchControls: TouchControlsOverlay? = null
    private var thorScreens: ThorDualScreenController? = null
    private var thorImportCompanion: ThorLowerCompanion? = null
    private var activityResumed = false
    private var romImportOverlay: RomImportOverlay? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private var resolvedProduct: String = PRODUCT_BASE
    private var motionSteering: MotionSteering? = null
    // Never requests audio focus: any focus request pauses the player's own music app.
    private val externalMediaDetector by lazy { ExternalMediaDetector(this) { nativeReportExternalMediaPlaying(it) } }
    private val thermalMonitor by lazy { ThermalMonitor(this) { nativeReportThermalHeadroom(it) } }

    override fun getLibraries(): Array<String> {
        // Original loads its OWN standalone libWiiCompiled.so, not the combined library - found
        // 2026-09-09 that Original (and only Original, not Retro Rewind) had a real gameplay bug
        // on the combined .so: steering wasn't blocked during the pre-race "3-2-1-GO" countdown,
        // letting the kart move before GO. Confirmed directly on-device that switching Original
        // alone back to its own untouched libWiiCompiled.so (never passes through the combined-lib
        // merge/rename script) fixes it - so this is a leftover profile-selection defect in that
        // merge specific to the "base" profile, not a genuine bug in Original's own translated
        // logic (which runs correctly here, unmerged). The exact defect in the merge itself
        // (some function that should differ per profile - most likely the race-countdown input
        // gate - still being resolved wrong for "base" specifically) has NOT been root-caused yet;
        // this split is the real fix for now, not a temporary diagnostic, until someone does that
        // deeper dispatch-table archaeology.
        if (resolvedProduct == PRODUCT_BASE) {
            return arrayOf("wii", "png16", "WiiCompiled")
        }
        if (BuildConfig.THOR_BUILD) {
            check(BuildConfig.THOR_RETRO_REWIND) { "Retro Rewind is not included in this Thor APK" }
            return arrayOf("wii", "png16", "RetroRewind")
        }
        // Retro Rewind still loads the combined library - confirmed working correctly there
        // (see the memory above for the full multi-session fix history that made this safe:
        // transitive sensitivity closure + source-level symbol aliasing in
        // TranslatedBuildShardEmitter.cs, profile-tagged registry in abi_bridge.cpp, deferred mod
        // registration in recomp_mod_loader.cpp). Still outstanding: the combined-lib CMake custom
        // command doesn't track mkw_combined_product's object content for staleness - always
        // `rm libGameCombined.so` before relinking after any C++ change, or ninja may silently
        // reuse a stale link.
        return arrayOf("wii", "png16", "GameCombined")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Tilt steering has its own listener (MotionSteering.kt); SDL's accelerometer stays off.
        SDLActivity.mIgnoreDeviceAccelerometer = true
        if (BuildConfig.THOR_BUILD && ThorUpdateRestart.requested(this)) {
            ThorUpdateRestart.restart(this)
            return
        }
        check(!BuildConfig.THOR_BUILD || (!ThorRetroRewindUpdateService.needsRecovery(this) &&
            !ThorGameSession.isUpdating() && ThorGameSession.tryBeginRuntime())) {
            "Finish the Retro Rewind update before starting a game"
        }
        resolvedProduct = intent.getStringExtra(EXTRA_PRODUCT) ?: PRODUCT_BASE
        check(!BuildConfig.THOR_BUILD || resolvedProduct == PRODUCT_BASE || BuildConfig.THOR_RETRO_REWIND) {
            "Retro Rewind requires a Thor APK packaged with -IncludeRetroRewind"
        }

        // A different product may already be running in this process from an earlier launch (e.g.
        // the user backgrounded the game, returned to ModePickerActivity, and picked the OTHER
        // mode without fully closing the app) - the guest engine has no supported way to reset
        // itself and switch profiles live, so the only correct fix is a full process restart into
        // the new choice.
        if (sLoadedProduct != null && sLoadedProduct != resolvedProduct) {
            restartIntoProduct(resolvedProduct)
            return
        }
        sLoadedProduct = resolvedProduct
        // Must run before any System.loadLibrary() call below - network_deferred.cpp reads this
        // env var once, at process/native-library load time.
        PrivateServerSettings.configureLaunch(this)
        android.system.Os.setenv("MKW_THOR_DISPLAY",
            if (BuildConfig.THOR_BUILD && Build.MODEL.equals("AYN Thor", ignoreCase = true)) "1" else "0", true)
        if (BuildConfig.THOR_BUILD && Build.MODEL.equals("AYN Thor", ignoreCase = true)) {
            // Odin mode already reports the printed Nintendo button letters.
            // SDL's SOUTH/EAST names then mean A/B, not their physical positions.
            val letterMode = android.provider.Settings.System.getInt(contentResolver, "flip_button_layout", 0) == 0
            android.system.Os.setenv("MKW_THOR_LABEL_BUTTONS", if (letterMode) "1" else "0", true)
        }
        loadNativeLibraries(resolvedProduct)
        // RR lower maps require the successfully read archive's exact identity and hash.
        RaceMapBridge.nativeEnable(BuildConfig.THOR_BUILD)

        // Both of these must happen before super.onCreate(), which is what starts SDLActivity's
        // own lifecycle towards eventually calling SDL_main - by the time the guest boots (audio
        // init in particular), dsp_coef.bin and wii_bootstrap/ must already be on disk, or
        // RuntimeMain fails outright (confirmed on a clean install: SDL_main returning a nonzero
        // exit code makes SDLActivity's own Java glue call finish(), which looks exactly like an
        // ordinary close, not a crash - see runtime/src/hle/audio/ax_mix.cpp and nand_path.h for
        // why the runtime looks in the app's files dir for these instead of an executable
        // directory, which doesn't exist on Android).
        ensureBundledAssets()
        acquireMulticastLock()

        // The native side (dvd.cpp's WaitForAndroidDvdRoot, Android-only) polls this same path
        // rather than failing immediately if it isn't a valid extracted DATA folder yet, since
        // RomImportOverlay below fills it in concurrently - there is no ordering dependency
        // between the two beyond that path string.
        val dvdRoot = File(filesDir, "WiiCompiled/DiscData")
        nativeSetInstallPaths(filesDir.absolutePath, dvdRoot.absolutePath)
        if (resolvedProduct == PRODUCT_RETRO_REWIND) {
            nativeSetRetroRewindRoot(File(filesDir, RETRO_REWIND_STAGING_SUBDIR).absolutePath)
        }
        for (root in ModManager.enabledOverlayRoots(this)) {
            nativeAddOverlayRoot(root)
        }
        // Only ever used as a FIRST-RUN default (native side only writes it when no explicit
        // [video] widescreen key exists in Config.toml yet, i.e. the user has never touched the
        // settings sidebar's "Aspect Ratio" picker) - reported directly: on a real 4:3-screened
        // device (Retroid Pocket Nova), the game was always defaulting to 16:9 regardless of the
        // actual screen shape, mis-positioning HUD/menu elements toward the center instead of the
        // corners. `resources.displayMetrics` reflects the real physical screen here since this
        // Activity is locked to sensorLandscape (AndroidManifest.xml).
        val metrics = resources.displayMetrics
        val landscapeAspect = maxOf(metrics.widthPixels, metrics.heightPixels).toFloat() /
            minOf(metrics.widthPixels, metrics.heightPixels).toFloat()
        // Halfway between a true 4:3 (1.333) and true 16:9 (1.778) screen.
        nativeSetDetectedWidescreen(landscapeAspect >= 1.55f)
        // A no-op on the separate-library builds (getLibraries() below) - only the combined
        // library's RuntimeProduct provider (combined_product.cpp) does anything with this, and
        // it must run before super.onCreate() starts SDL's thread towards main(), which is when
        // TranslatedFunctionRegistry::Finalize() locks in which generated dispatch table is active.
        nativeSetActiveProduct(resolvedProduct == PRODUCT_RETRO_REWIND)

        super.onCreate(savedInstanceState)

        // Without this, Android is free to apply its default (often battery-biased) governor
        // behavior to this process even while it's pegging the CPU/GPU every frame - this is a
        // direct request for the OS to favor sustained throughput over battery/thermal headroom
        // for as long as this window is visible. No-ops harmlessly on hardware that doesn't
        // support it (see PowerManager.setSustainedPerformanceMode's own docs).
        window.setSustainedPerformanceMode(true)

        // Many phones default a new window to 60Hz even on 90/120Hz-capable displays unless the
        // app explicitly opts into a higher refresh rate mode - without this, the in-game "Race
        // frame interpolation" setting above 60 FPS has nothing to actually present onto and the
        // display stays locked at 60 regardless (confirmed on-device: selecting 90 FPS there had
        // no visible effect until this was added).
        applyConfiguredFrameRate()

        // The settings gear and touch controls are gameplay UI - showing them over the ROM
        // picker would let a player fiddle with in-game settings before there's a game to apply
        // them to. Only add them once a game is actually present (either already imported from a
        // previous run, or - once extraction exists - right after RomImportOverlay finishes).
        if (RomImportOverlay.isRomAlreadyImported(dvdRoot)) {
            showGameUi()
        } else {
            if (BuildConfig.THOR_BUILD && Build.MODEL.equals("AYN Thor", ignoreCase = true)) {
                thorImportCompanion = ThorLowerCompanion(this, File(filesDir, "WiiCompiled/ThorMap")).also {
                    it.setStatus("Disc setup", "Select your own PAL Mario Kart Wii disc on the upper screen. Import progress and any errors appear there.")
                    if (activityResumed) it.start()
                }
            }
            // The Thor launcher's storage scan may already have found the disc image.
            val scanned = intent.getStringExtra(EXTRA_ROM_PATH)?.let { File(it) }?.takeIf { it.isFile }
            intent.removeExtra(EXTRA_ROM_PATH)
            romImportOverlay = RomImportOverlay.attach(this, mLayout, dvdRoot, { showGameUi() }, scanned)
        }
    }

    private fun loadNativeLibraries(product: String) {
        // See getLibraries() above.
        System.loadLibrary("wii")
        System.loadLibrary("png16")
        if (product == PRODUCT_BASE) {
            System.loadLibrary("WiiCompiled")
        } else if (BuildConfig.THOR_BUILD) {
            check(BuildConfig.THOR_RETRO_REWIND) { "Retro Rewind is not included in this Thor APK" }
            System.loadLibrary("RetroRewind")
        } else {
            System.loadLibrary("GameCombined")
        }
    }

    /** Picks the display mode that matches what the game actually produces - Android does not do
     * this by default, even on a 90/120Hz-capable screen. Deliberately not the fastest mode: the
     * game renders 60fps unless frame interpolation is on, and holding a 120Hz panel at 120Hz
     * keeps the whole display pipeline running twice as fast for frames that are then discarded,
     * which is heat for nothing on a device that throttles. */
    private fun requestRefreshRateFor(targetFps: Int) {
        val display =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) display else windowManager.defaultDisplay
        val currentMode = display?.mode ?: return
        val sameSizeModes =
            display.supportedModes.filter {
                it.physicalWidth == currentMode.physicalWidth &&
                    it.physicalHeight == currentMode.physicalHeight
            }
        if (sameSizeModes.isEmpty()) {
            return
        }
        // The slowest mode that still covers the target, so 60fps content gets a 60Hz panel;
        // fall back to the fastest when nothing reaches the target (e.g. 120fps on a 60Hz screen).
        val mode =
            sameSizeModes.filter { it.refreshRate >= targetFps - REFRESH_RATE_EPSILON }
                .minByOrNull { it.refreshRate }
                ?: sameSizeModes.maxByOrNull { it.refreshRate }
                ?: return
        val attributes = window.attributes
        attributes.preferredDisplayModeId = mode.modeId
        attributes.preferredRefreshRate = mode.refreshRate
        window.attributes = attributes
    }

    private fun applyConfiguredFrameRate() {
        val target = configuredTargetFps()
        SDLActivity.mContentFrameRate = target.toFloat()
        requestRefreshRateFor(target)
        // A live native settings change need not resize/recreate SDLSurface,
        // so surfaceChanged's initial vote alone leaves 120 interpolation at
        // 60 Hz. Vote on the upper game surface when the settings close too.
        val surface = mSurface?.holder?.surface
        if (Build.VERSION.SDK_INT >= 30 && surface?.isValid == true) {
            runCatching {
                if (Build.VERSION.SDK_INT >= 31) {
                    surface.setFrameRate(target.toFloat(), Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                        Surface.CHANGE_FRAME_RATE_ALWAYS)
                } else {
                    surface.setFrameRate(target.toFloat(), Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)
                }
            }.onFailure { Log.w("ThorDisplay", "Upper surface frame-rate vote failed", it) }
        }
        Log.i("ThorDisplay", "Upper output target=$target Hz, interpolation=${target > 60}")
    }

    /** Reads video.frame_interpolation_fps straight out of Config.toml, which the runtime owns.
     * 0 (the default) means "no interpolation", i.e. the game's own 60fps. */
    private fun configuredTargetFps(): Int {
        val config = File(File(filesDir, "WiiCompiled"), "Config.toml")
        val configured =
            runCatching {
                config.takeIf { it.isFile }
                    ?.readLines()
                    ?.firstOrNull { it.trimStart().startsWith("frame_interpolation_fps") }
                    ?.substringAfter('=')
                    ?.substringBefore('#')?.trim()
                    ?.toIntOrNull()
            }.getOrNull() ?: 0
        if (!BuildConfig.THOR_BUILD || !Build.MODEL.equals("AYN Thor", ignoreCase = true)) {
            return if (configured > 0) configured else 60
        }
        return when (configured) { 120, 180 -> 120; else -> 60 }
    }

    override fun onNewIntent(newIntent: Intent) {
        super.onNewIntent(newIntent)
        // SDL owns one process-wide native runtime. A launcher selection must
        // return to this activity, not initialize a second SDLActivity over it.
        // Resolve profile changes before replacing the current launch intent.
        val product = newIntent.getStringExtra(EXTRA_PRODUCT) ?: resolvedProduct
        check(!BuildConfig.THOR_BUILD || product == PRODUCT_BASE || BuildConfig.THOR_RETRO_REWIND) {
            "Retro Rewind requires a Thor APK packaged with -IncludeRetroRewind"
        }
        if (product != resolvedProduct) {
            restartIntoProduct(product)
            return
        }
        intent = newIntent
        // Android delivers onResume next; SDL's normal lifecycle restores its
        // native thread/surface and our lower presentation there.
        Log.i("ThorLifecycle", "Returning to existing $resolvedProduct runtime")
    }

    /** Thor uses the separate foreground handoff; upstream keeps its existing restart path. */
    private fun restartIntoProduct(product: String) {
        if (BuildConfig.THOR_BUILD) {
            ThorUpdateRestart.restartProduct(this, product)
            return
        }
        val relaunch = Intent(this, MainActivity::class.java).putExtra(EXTRA_PRODUCT, product)
        relaunch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        val pending =
            PendingIntent.getActivity(
                this,
                0,
                relaunch,
                PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_ONE_SHOT or PendingIntent.FLAG_IMMUTABLE,
            )
        val alarmManager = getSystemService(Context.ALARM_SERVICE) as AlarmManager
        alarmManager.set(AlarmManager.RTC, System.currentTimeMillis() + 100, pending)
        Process.killProcess(Process.myPid())
    }

    /** Called by the safe native JNI dispatcher; the normal Activity lifecycle pauses SDL. */
    /** Called from native (the settings sidebar's "Main Menu" button). Brings the existing
     * launcher to the front without finishing this Activity, so the paused runtime is retained
     * and choosing the same game resumes it. May arrive off the UI thread. */
    fun returnToMainMenu() {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            startActivity(Intent(this, ModePickerActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
        }
    }

    fun openRetroRewindUpdates() {
        runOnUiThread {
            if (BuildConfig.THOR_BUILD && !isFinishing && !isDestroyed) {
                if (BuildConfig.THOR_RETRO_REWIND) {
                    startActivity(Intent(this, ThorRetroRewindUpdatesActivity::class.java))
                } else {
                    ThorDialogs.builder(this).setTitle("Retro Rewind updates")
                        .setMessage("Retro Rewind is not included in this build. Build and install the Thor version with both games to use mod updates.")
                        .setPositiveButton("OK", null).show()
                }
            }
        }
    }

    override fun onDestroy() {
        thorImportCompanion?.stop()
        thorScreens?.stop()
        multicastLock?.release()
        multicastLock = null
        super.onDestroy()
    }

    // Android drops incoming broadcast/multicast UDP packets by default to save battery - without
    // this, the LAN host-discovery broadcasts the game's own network code (runtime/src/hle/net/)
    // relies on would never actually reach the socket, even though the socket itself opens fine.
    // Held for the whole activity lifetime rather than only during a match, since there's no
    // native hook yet that knows when the guest actually enters local-play vs. any other screen.
    private fun acquireMulticastLock() {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
        val lock = wifiManager.createMulticastLock("WiiCompiledLocalPlay")
        lock.setReferenceCounted(false)
        lock.acquire()
        multicastLock = lock
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == RomImportOverlay.REQUEST_CODE_PICK_ROM) {
            romImportOverlay?.onFilePicked(data?.data)
            return
        }
        touchControls?.onActivityResult(requestCode, data?.data)
    }

    private fun showGameUi() {
        // The importer companion never calls JNI. Dismiss it before the native map owns the lower window.
        thorImportCompanion?.stop()
        thorImportCompanion = null
        val gearButton = addSettingsButton()
        settingsButton = gearButton
        gearButton.visibility = if (nativeSettingsButtonHidden) View.GONE else View.VISIBLE
        // Thor uses its physical controls. Never create a touch pad in this build,
        // including when the lower display is disconnected or settings change.
        if (!BuildConfig.THOR_BUILD) touchControls = TouchControlsOverlay.attach(this, mLayout)
        nativeSetTouchControlsVisibleCache(touchControls?.isUserVisible() ?: false)
        nativeSetDoubleTapAutoHoldCache(touchControls?.isDoubleTapAutoHoldEnabled() ?: false)
        touchControls?.onEditModeChanged = { editing -> gearButton.alpha = if (editing) 1f else 0.55f }
        // Only enable automatically on the device this layout was requested for. The
        // controller discovers the lower display; display IDs can change after reconnects.
        if (Build.MODEL.equals("AYN Thor", ignoreCase = true)) {
            val retro = resolvedProduct == PRODUCT_RETRO_REWIND
            thorScreens = ThorDualScreenController(this, retro, if (retro) RetroRewindRelease.CODE_PUL_SHA256 else "")
            if (activityResumed) thorScreens?.start()
        }
        // No floating button (pulled per direct request: "remove steering with motion the button
        // pls for now") - Motion Steering now lives in the settings sidebar's Controller page
        // instead, matching KartPad's own menu placement and dialog exactly.
        motionSteering = MotionSteering(this)
        if (motionSteering?.enabled == true) {
            touchControls?.setMotionSteeringActive(true)
            motionSteering?.start()
        }
    }

    override fun onPause() {
        activityResumed = false
        thorImportCompanion?.stop()
        thorScreens?.stop()
        super.onPause()
        if (motionSteering?.enabled == true) motionSteering?.stop()
        externalMediaDetector.stop()
        thermalMonitor.stop()
    }

    override fun onResume() {
        super.onResume()
        activityResumed = true
        thorImportCompanion?.start()
        thorScreens?.start()
        applyThorFullscreen()
        if (motionSteering?.enabled == true) motionSteering?.start()
        externalMediaDetector.start()
        thermalMonitor.start()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) applyThorFullscreen()
    }

    private fun applyThorFullscreen() {
        if (!BuildConfig.THOR_BUILD) return
        if (Build.VERSION.SDK_INT >= 30) {
            // Thor's Android 13 retained visible inset requests with SDL's legacy
            // systemUiVisibility flags. Set the corresponding modern requests too.
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN)
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.apply {
                systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                hide(WindowInsets.Type.systemBars())
            }
        } else {
            SDLActivity.setWindowStyle(true)
        }
    }

    /** Called from native (settings_overlay.cpp's "Motion Steering" sidebar button, via
     * AndroidShowMotionSteeringDialog). May arrive off the UI thread. */
    fun onNativeShowMotionSteeringDialog() {
        runOnUiThread { showMotionSteeringOptionsDialog() }
    }

    /**
     * Action-list dialog matching KartPad's own Motion Steering menu exactly (dev.kartpad.android's
     * KartPadActivity.showMotionSteering): a "Turn On & Recenter"/"Turn Off" action, "Recenter Now"
     * (only while on), an invert toggle, and a sensitivity CYCLE (0.5x -> 1x -> 2x -> 0.5x) rather
     * than a slider, plus "Continue Playing" to dismiss. Every action except "Continue Playing"
     * reopens the dialog afterward so the updated state is immediately visible, same as KartPad.
     */
    private fun showMotionSteeringOptionsDialog() {
        val steering = motionSteering ?: return
        val available = steering.sensorAvailable
        val state =
            when {
                !available -> "Unavailable on this device"
                steering.enabled -> "On"
                else -> "Off"
            }
        val actions =
            if (!available) {
                arrayOf("Continue Playing")
            } else if (steering.enabled) {
                arrayOf(
                    "Turn Off",
                    "Recenter Now",
                    if (steering.inverted) "Use Standard Direction" else "Invert Direction",
                    "Cycle Sensitivity",
                    "Continue Playing",
                )
            } else {
                arrayOf(
                    "Turn On & Recenter",
                    if (steering.inverted) "Use Standard Direction" else "Invert Direction",
                    "Cycle Sensitivity",
                    "Continue Playing",
                )
            }

        val content = LinearLayout(this)
        content.orientation = LinearLayout.VERTICAL
        val pad = (24 * resources.displayMetrics.density).toInt()
        content.setPadding(pad, (4 * resources.displayMetrics.density).toInt(), pad, (8 * resources.displayMetrics.density).toInt())

        val label = TextView(this)
        label.text =
            if (available) {
                "Tilt the device like a steering wheel. Current state: $state. " +
                    "Sensitivity: ${steering.sensitivity}x. Physical controllers take priority."
            } else {
                "Motion data is unavailable on this device or emulator. Touch and " +
                    "physical-controller steering remain available."
            }
        content.addView(label)

        val dialog =
            AlertDialog.Builder(this)
                .setTitle("Motion Steering")
                .setView(ScrollView(this).apply { addView(content) })
                .create()

        fun applyEnabled(value: Boolean) {
            steering.enabled = value
            touchControls?.setMotionSteeringActive(value)
            if (value) steering.start() else steering.stop()
        }

        actions.forEach { action ->
            content.addView(
                Button(this).apply {
                    text = action
                    contentDescription = action
                    setOnClickListener {
                        dialog.dismiss()
                        when (action) {
                            "Turn Off" -> applyEnabled(false)
                            "Recenter Now" -> steering.recenter()
                            "Invert Direction" -> steering.inverted = true
                            "Use Standard Direction" -> steering.inverted = false
                            "Cycle Sensitivity" ->
                                steering.sensitivity =
                                    when (steering.sensitivity) {
                                        0.5f -> 1f
                                        1f -> 2f
                                        else -> 0.5f
                                    }
                            "Turn On & Recenter" -> {
                                applyEnabled(true)
                                steering.recenter()
                            }
                        }
                        if (action != "Continue Playing") {
                            mLayout.post { showMotionSteeringOptionsDialog() }
                        }
                    }
                },
            )
        }
        dialog.show()
    }

    /** Called from native (android_jni_bridge.cpp's AndroidNotifyGamepadConnectionChanged), which
     * aurora-main/lib/window.cpp drives from real SDL_EVENT_GAMEPAD_ADDED/REMOVED - the touch
     * overlay's own virtual gamepad is filtered out there already. May arrive on the SDL_main
     * thread, not the UI thread. */
    fun onNativeGamepadConnectionChanged(connected: Boolean) {
        runOnUiThread { touchControls?.setControllerConnected(connected) }
    }

    /** Called from native (the in-game settings menu's "Touch controls" checkbox,
     * settings_overlay.cpp, via AndroidSetTouchOverlayVisible). May arrive off the UI thread. */
    fun onNativeSetTouchOverlayVisible(visible: Boolean) {
        runOnUiThread { touchControls?.setUserVisible(visible) }
    }

    /** Called from native (the settings sidebar's "Double-tap A to auto-hold acceleration"
     * checkbox, settings_overlay.cpp, via AndroidSetDoubleTapAutoHold). May arrive off the UI
     * thread. */
    fun onNativeSetDoubleTapAutoHold(enabled: Boolean) {
        runOnUiThread { touchControls?.setDoubleTapAutoHoldEnabled(enabled) }
    }

    /** Called from native (the settings sidebar's "Edit Touch Layout" button, settings_overlay.cpp
     * via AndroidStartTouchLayoutEdit). May arrive off the UI thread. Replaces the old "hold the
     * gear button for 5 seconds" gesture now that the sidebar has an explicit entry point. */
    fun onNativeStartTouchLayoutEdit() {
        runOnUiThread { touchControls?.setEditMode(true) }
    }

    /** Called from native (SetTopBarVisible in settings_overlay.cpp, via
     * AndroidNotifySettingsVisibilityChanged) whenever the settings sidebar opens/closes - touch
     * controls hide while it's open since they'd otherwise sit underneath it (confirmed directly
     * on-device) and gameplay input is already blocked while it's up regardless. May arrive off
     * the UI thread. */
    fun onNativeSettingsVisibilityChanged(visible: Boolean) {
        runOnUiThread {
            touchControls?.setSettingsOpen(visible)
            if (!visible) applyConfiguredFrameRate()
        }
    }

    // Desktop's CMake build copies these next to the built executable (see
    // runtime/cmake/PublicProducts.cmake); Android has no equivalent "next to the executable"
    // location, so they ride along as APK assets instead and get copied out here, once, the same
    // way the first-run installer would on desktop. Idempotent and cheap after the first launch.
    private fun ensureBundledAssets() {
        val wiiCompiledDir = File(filesDir, "WiiCompiled")
        copyAssetIfMissing("dsp_coef.bin", File(wiiCompiledDir, "dsp_coef.bin"))
        // Refreshed whenever an app update ships a different bundle, since CAs rotate.
        copyAssetIfChanged("cacert.pem", File(wiiCompiledDir, "cacert.pem"))
        for (relative in BUNDLED_BOOTSTRAP_FILES) {
            copyAssetIfMissing(relative, File(wiiCompiledDir, relative))
        }
    }

    private fun copyAssetIfChanged(assetPath: String, dest: File) {
        val bundled = assets.open(assetPath).use { it.readBytes() }
        if (dest.exists() && dest.length() == bundled.size.toLong() && dest.readBytes().contentEquals(bundled)) {
            return
        }
        dest.parentFile?.mkdirs()
        dest.writeBytes(bundled)
    }

    private fun copyAssetIfMissing(assetPath: String, dest: File) {
        if (dest.exists()) {
            return
        }
        dest.parentFile?.mkdirs()
        assets.open(assetPath).use { input -> dest.outputStream().use { output -> input.copyTo(output) } }
    }

    companion object {
        const val EXTRA_PRODUCT = "product"
        /** Absolute path of a disc image found by the launcher's storage scan. */
        const val EXTRA_ROM_PATH = "rom_path"
        const val PRODUCT_BASE = "base"
        const val PRODUCT_RETRO_REWIND = "retro_rewind"

        // Set once per process by whichever product's onCreate runs first - see the process-
        // restart guard above. Not an instance field: it must survive across MainActivity
        // instances within the same still-alive process.
        private var sLoadedProduct: String? = null

        // Staging path for the Retro Rewind asset pack (tracks/textures - distinct from Code.pul,
        // which only carries the mod's patched game LOGIC and is already compiled into
        // libGameCombined.so). Populated by ModePickerActivity's folder-import flow, mirroring how
        // the PC version's Retro Rewind support works (WiiCompiled.Setup/RetroRewindSource.cs only
        // ever resolves a folder the user already has - it never packages or downloads one).
        const val RETRO_REWIND_STAGING_SUBDIR = "WiiCompiled/RetroRewind6"

        // Mirrors runtime/assets/wii/ (see nand_path.h's BootstrapPayloadPath) - Dolphin's own
        // free, synthetic WiiConnect24 bootstrap tree, not real user data (see
        // THIRD-PARTY-NOTICES.md).
        private val BUNDLED_BOOTSTRAP_FILES =
            listOf(
                "wii_bootstrap/README.md",
                "wii_bootstrap/shared2/wc24/misc.bin",
                "wii_bootstrap/shared2/wc24/nwc24dl.bin",
                "wii_bootstrap/shared2/wc24/nwc24fl.bin",
                "wii_bootstrap/shared2/wc24/nwc24fls.bin",
                "wii_bootstrap/shared2/wc24/nwc24msg.cbk",
                "wii_bootstrap/shared2/wc24/nwc24msg.cfg",
                "wii_bootstrap/shared2/wc24/mbox/Readme.txt",
                "wii_bootstrap/shared2/wc24/mbox/wc24recv.ctl",
                "wii_bootstrap/shared2/wc24/mbox/wc24recv.mbx",
                "wii_bootstrap/shared2/wc24/mbox/wc24send.ctl",
                "wii_bootstrap/shared2/wc24/mbox/wc24send.mbx",
            )

        private const val GEAR_PREFS_NAME = "settings_gear"
        private const val GEAR_KEY_X = "x"
        private const val GEAR_KEY_Y = "y"
        // A curved-edge screen's curve runs the FULL LENGTH of the phone's two long physical
        // edges - which, in landscape, is the entire top AND bottom edge of the screen, not just
        // the corners (confirmed by direct user feedback: the original ALIGN_PARENT_TOP/END
        // corner placement was hard to press). That means no y-inset near the top edge is fully
        // clear of the curve regardless of x - staying at x=0.90 (same column as the R touch
        // control) to gain y-clearance just overlapped R instead (confirmed on-device
        // screenshot). Top-CENTER instead: still near the top (some curve exposure is
        // unavoidable without leaving the top edge entirely) but far from every other touch
        // control on either side, and corners - where two curved edges meet - are usually the
        // single hardest spot to press precisely, so center is a real improvement over the
        // original corner placement even before dragging. Draggable below regardless, for
        // whatever exact spot ends up working best on this specific device.
        private const val GEAR_DEFAULT_X_FRACTION = 0.55f
        private const val GEAR_DEFAULT_Y_FRACTION = 0.08f

        // Reported refresh rates are floats like 59.94 or 120.00021.
        private const val REFRESH_RATE_EPSILON = 1.0f
    }

    // Desktop opens the (already fully built) in-game settings overlay with F10 - there is no
    // keyboard here, so this is the touch equivalent. A plain native Android view instead of an
    // Aurora/ImGui-drawn one on purpose: it must keep working even while Aurora's own render loop
    // is doing something unexpected, and it never competes with ImGui for the touch event. Long
    // press enters touch-control layout editing (drag/resize/hide) instead - see
    // TouchControlsOverlay. Draggable (via the same EditGestureHelper every other on-screen
    // control uses) and defaults away from the extreme corner, since a curved-edge screen's curve
    // sits right there and made it hard to press precisely.
    private fun addSettingsButton(): Button {
        val prefs: SharedPreferences = getSharedPreferences(GEAR_PREFS_NAME, Context.MODE_PRIVATE)
        val button = Button(this)
        button.text = "⚙"
        button.alpha = 0.55f
        button.setTextColor(Color.WHITE)
        // Circular instead of the default square button background - requested directly, and
        // pairs with the settings sidebar now being the single entry point for both opening
        // settings and (via its own "Edit Touch Layout" button) touch layout editing.
        val circleBackground =
            android.graphics.drawable.GradientDrawable().apply {
                shape = android.graphics.drawable.GradientDrawable.OVAL
                setColor(Color.argb(120, 0, 0, 0))
            }
        button.background = circleBackground

        // Edit mode is now entered via the settings sidebar's "Edit Touch Layout" button
        // (onNativeStartTouchLayoutEdit), not by holding this button - the old 5-second-hold
        // gesture is gone. While edit mode IS active (entered from the sidebar), touches here
        // still drag the gear itself out of the way of whatever's being edited.
        val editGesture =
            EditGestureHelper(this, button) { moved ->
                if (moved) {
                    val parentWidth = mLayout.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels
                    val parentHeight = mLayout.height.takeIf { it > 0 } ?: resources.displayMetrics.heightPixels
                    // Clamped to stay fully on-screen - a drag that ends past the edge (e.g. a
                    // fast fling, or a gesture interrupted mid-drag by something else grabbing
                    // focus, such as an incoming call) previously persisted an out-of-range
                    // fraction that placed the gear permanently off-screen on every future
                    // launch, with no way to get it back short of clearing app data - confirmed
                    // directly on-device.
                    val xFraction = ((button.x + button.width / 2f) / parentWidth).coerceIn(0.05f, 0.95f)
                    val yFraction = ((button.y + button.height / 2f) / parentHeight).coerceIn(0.05f, 0.95f)
                    prefs.edit().putFloat(GEAR_KEY_X, xFraction).putFloat(GEAR_KEY_Y, yFraction).apply()
                }
            }
        button.setOnTouchListener { _, event ->
            if (touchControls?.isEditMode() == true) {
                editGesture.onTouchEvent(event)
                true
            } else {
                false
            }
        }
        button.setOnClickListener { nativeToggleSettingsOverlay() }

        // Bigger than the original 48dp - confirmed on-device, that was hard to hit precisely
        // with a finger.
        val sizePx = (64 * resources.displayMetrics.density).toInt()
        val params = RelativeLayout.LayoutParams(sizePx, sizePx)
        mLayout.addView(button, params)
        button.textSize = 22f

        // Positioned in pixels (not gravity/margins) so drag-to-reposition above can move it
        // freely - fractions convert once the layout has a real size, same approach
        // TouchControlsOverlay uses for its own controls.
        mLayout.post {
            val parentWidth = mLayout.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels
            val parentHeight = mLayout.height.takeIf { it > 0 } ?: resources.displayMetrics.heightPixels
            val xFraction = prefs.getFloat(GEAR_KEY_X, GEAR_DEFAULT_X_FRACTION)
            val yFraction = prefs.getFloat(GEAR_KEY_Y, GEAR_DEFAULT_Y_FRACTION)
            button.x = xFraction * parentWidth - sizePx / 2f
            button.y = yFraction * parentHeight - sizePx / 2f
        }
        return button
    }
}
