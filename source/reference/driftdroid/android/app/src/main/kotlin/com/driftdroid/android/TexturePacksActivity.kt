package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.io.IOException

/** Browse and install creator releases by game, with a separate installed-pack view. */
class TexturePacksActivity : Activity() {
    private val artRoot by lazy { File(filesDir, "WiiCompiled/ThorMap") }
    private val companion by lazy { ThorLowerCompanion(this, artRoot, "Texture Packs") }
    private var selected: FrontendGame? = null
    private var managing = false
    private var scrollView: ScrollView? = null
    private var focusedPack: String? = null
    private var showCreatorPages = false
    private lateinit var status: TextView
    private var notice = ""
    private var aButton: Button? = null
    private var observed = operation
    private val handler = Handler(Looper.getMainLooper())
    private val refresh = object : Runnable {
        override fun run() {
            val current = operation
            if (observed != current) {
                val progressOnly = observed.running && current.running
                observed = current
                if (progressOnly) {
                    status.text = current.message
                    companion.setStatus("Texture Packs", current.message)
                } else {
                    notice = current.message
                    render(keepScroll = true)
                }
            }
            handler.postDelayed(this, 250)
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        selected = FrontendGame.fromId(state?.getString("game"))
        managing = state?.getBoolean("managing") == true
        render()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("game", selected?.id)
        outState.putBoolean("managing", managing)
        super.onSaveInstanceState(outState)
    }

    override fun onResume() {
        super.onResume()
        window.insetsController?.apply {
            systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsets.Type.systemBars())
        }
        companion.start()
        handler.post(refresh)
    }

    override fun onPause() {
        handler.removeCallbacks(refresh)
        companion.stop()
        super.onPause()
    }

    private fun render(keepScroll: Boolean = false) {
        val position = if (keepScroll) scrollView?.scrollY ?: 0 else 0
        focusedPack = if (keepScroll) (currentFocus?.tag as? String) ?: focusedPack else null
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(28), dp(18), dp(28), dp(18))
            setBackgroundColor(0xff101923.toInt())
        }
        val game = selected
        layout.addView(label(game?.label ?: "Texture Pack Store", 27f, true))
        status = label(when {
            operation.running -> operation.message
            notice.isNotEmpty() -> notice
            game != null && managing -> "Enabled packs load the next time you start this game."
            game == null -> "Choose a game case to see its texture packs."
            else -> "Choose a pack to download and enable it for your next launch."
        }, 15f)
        layout.addView(status)
        val content = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val scroll = ScrollView(this).apply { isFillViewport = true; addView(content) }
        scrollView = scroll
        layout.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        val backButton = button(when {
            game != null && managing -> "Back to packs"
            game != null -> "All game cases"
            else -> "Back to library"
        }) { back() }
        if (game != null && !managing && TexturePacks.supported(game)) {
            val footer = LinearLayout(this)
            footer.addView(button("Import or manage packs") { openManager(game) },
                LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = dp(6) })
            footer.addView(backButton, LinearLayout.LayoutParams(0, -2, 1f))
            layout.addView(footer)
        } else {
            layout.addView(backButton)
        }
        when {
            game != null && (managing || !TexturePacks.supported(game)) -> showStore(content, game)
            game == null -> showCases(content)
            else -> showCatalog(content, game)
        }
        setContentView(layout)
        companion.setStatus(game?.label ?: "Texture Pack Store",
            if (operation.running) operation.message else if (game == null) "Choose a game case on the upper screen." else "Choose a pack on the upper screen.")
        if (!window.decorView.isInTouchMode) {
            val previous = focusedPack?.let { layout.findViewWithTag<View>(it) }
            if (previous?.requestFocus() != true) content.requestFocus(View.FOCUS_FORWARD)
        }
        if (position > 0) scroll.post { scroll.scrollTo(0, position) }
    }

    private fun column() = LinearLayout.LayoutParams(0, -2, 1f).apply {
        setMargins(dp(4), dp(4), dp(4), dp(6))
    }

    private fun showCatalog(content: LinearLayout, game: FrontendGame) {
        if (game == FrontendGame.ECLIPSE)
            content.addView(label("These packs improve textures shared with Sunshine.", 14f))
        val installed = TexturePacks.installed(this, game).map { it.id }.toSet()
        val active = TexturePacks.enabled(this, game).orEmpty().toSet()
        for (group in TexturePacks.catalog(game).chunked(2)) {
            val row = LinearLayout(this).apply { isBaselineAligned = false }
            for (release in group) {
                val card = LinearLayout(this).apply {
                    orientation = LinearLayout.VERTICAL
                    setPadding(dp(12), dp(4), dp(12), dp(8))
                    background = tile(false)
                }
                val downloaded = release.id in installed
                val enabled = downloaded && release.id in active
                card.addView(label(release.title, 19f, true))
                val details = if (enabled) "Enabled" else if (downloaded) "Installed" else release.downloadSize
                card.addView(label("${release.author} · $details\n${release.description}", 13f))
                card.addView(button(when {
                    enabled -> "Turn off"
                    downloaded -> "Enable"
                    else -> "Download & enable"
                }) {
                    if (downloaded) change(game) { TexturePacks.setEnabled(this, game, release.id, !enabled) }
                    else install(game, { "${release.title} ${release.version}" },
                        { TexturePacks.openDownload(release.url) }, release.bytes, release.sha)
                }.apply {
                    tag = release.id
                    contentDescription = "$text ${release.title} for ${game.label}"
                })
                row.addView(card, column())
            }
            if (group.size == 1) row.addView(View(this), column())
            content.addView(row)
        }
        if (TexturePacks.catalog(game).isEmpty())
            content.addView(label("Bring your own Dolphin-format PNG or DDS texture ZIP.", 15f))
    }

    private fun openManager(game: FrontendGame) {
        selected = game
        managing = true
        notice = ""
        showCreatorPages = false
        render()
    }

    private fun openCase(game: FrontendGame) {
        selected = game
        managing = false
        notice = ""
        showCreatorPages = false
        render()
    }

    private fun showCases(content: LinearLayout) {
        val layers = ThorLauncherFolders.caseLayers(this, artRoot)
        val games = FrontendGame.values().filter { it != FrontendGame.RETRO_REWIND || BuildConfig.THOR_RETRO_REWIND }
        val columns = if (resources.configuration.screenWidthDp >= 720) 6 else 4
        for (group in games.chunked(columns)) {
            val row = LinearLayout(this).apply { isBaselineAligned = false }
            for (game in group) {
                val available = TexturePacks.supported(game) ||
                    game == FrontendGame.MARIO_KART || game == FrontendGame.RETRO_REWIND
                val tile = button(game.label) { openCase(game) }.apply {
                    if (!available) {
                        isEnabled = false
                        alpha = 0.46f
                        contentDescription = "${game.label}, texture packs unavailable"
                    }
                    gravity = Gravity.CENTER
                    maxLines = 3
                    textSize = 12.5f
                    includeFontPadding = false
                    layers.firstNotNullOfOrNull { ThorLauncherArtwork.bitmap(it, "${game.artwork}-cover") }?.let { cover ->
                        val height = dp(78)
                        val width = (height * cover.width.toFloat() / cover.height).toInt().coerceIn(1, dp(130))
                        val art = BitmapDrawable(resources, cover).apply { setBounds(0, 0, width, height) }
                        setCompoundDrawables(null, art, null, null)
                        compoundDrawablePadding = dp(3)
                    }
                }
                row.addView(tile, LinearLayout.LayoutParams(0, dp(138), 1f).apply { setMargins(dp(4), dp(4), dp(4), dp(4)) })
            }
            repeat(columns - group.size) { row.addView(View(this), LinearLayout.LayoutParams(0, 1, 1f)) }
            content.addView(row)
        }
    }

    private fun showStore(content: LinearLayout, game: FrontendGame) {
        if (!TexturePacks.supported(game)) {
            content.addView(label("Pack support is unavailable for this case in this build.", 17f))
            return
        }
        if (game == FrontendGame.MARIO_KART || game == FrontendGame.RETRO_REWIND) {
            content.addView(button("Manage Riivolution packs") {
                if (canRemove()) ModManager.showManagerDialog(this)
            })
        }
        content.addView(button("Import a texture ZIP") {
            runCatching {
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("*/*"), REQUEST_ZIP)
            }.onFailure { toast("Android couldn't open the file picker.") }
        })
        content.addView(label("Installed on this device", 20f, true))
        val packs = TexturePacks.installed(this, game)
        val active = TexturePacks.enabled(this, game)
        if (packs.isEmpty()) content.addView(label("No packs added yet.", 15f))
        TexturePacks.lastIndexedCount(this, game)?.let { count ->
            content.addView(label("Last game launch indexed $count managed textures.", 14f))
        }
        if (active != null) {
            val priority = if (TexturePacks.usesAurora(game)) "Most recently enabled WiiDS packs take priority."
                else "Button and HUD packs stay above store HD packs. The last enabled overlay wins."
            content.addView(label(priority, 14f))
            content.addView(button("Use game defaults") { change(game) { TexturePacks.useGameDefaults(this, game) } })
        }
        for (pack in packs) {
            val enabled = pack.id in active.orEmpty()
            content.addView(button("${pack.title}  |  ${if (enabled) "Enabled" else "Off"}") {
                ThorDialogs.builder(this).setTitle(pack.title)
                    .setPositiveButton(if (enabled) "Turn off" else "Enable") { _, _ ->
                        change(game) { TexturePacks.setEnabled(this, game, pack.id, !enabled) }
                    }
                    .setNeutralButton("Remove") { _, _ -> confirmRemove(game, pack) }
                    .setNegativeButton("Back", null).show()
            })
        }
        content.addView(button(if (showCreatorPages) "Hide creator pages" else "More packs on creator websites") {
            showCreatorPages = !showCreatorPages
            render()
        })
        if (showCreatorPages) addCreatorPages(content, game)
    }

    private fun addCreatorPages(content: LinearLayout, game: FrontendGame) {
        content.addView(label("These open in your browser. Use Import a texture ZIP for compatible Dolphin PNG or DDS packs.", 14f))
        if (game == FrontendGame.SUNSHINE) {
            content.addView(button("Sunshine Ultra HD creator page") {
                openPage("https://github.com/qashto/Super_Mario_Sunshine_UHD_Texture_Pack/releases")
            })
            content.addView(button("Sunshine 4K creator page") {
                openPage("https://www.henrikomagnifico.com/super-mario-sunshine-4k")
            })
        } else if (game == FrontendGame.ECLIPSE) {
            content.addView(label("Eclipse High-Res is about 16 GB installed.", 14f))
            content.addView(button("Eclipse High-Res creator page") {
                openPage("https://github.com/iZePlayzYT/SuperMarioEclipse-TexturePacks/releases")
            })
        } else {
            val page = when (game) {
                FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND ->
                    "https://github.com/GhostlyDark/MKWii-Reloaded/releases"
                FrontendGame.NSMB, FrontendGame.MSMBWII -> "https://gamebanana.com/mods/480785"
                FrontendGame.BRAWL -> "https://emulationking.com/nintendo/wii/games/supersmashbrosbrawl/texturepacks/yami2b/"

                FrontendGame.GALAXY, FrontendGame.KAIZO -> "https://emulationking.com/nintendo/wii/games/supermariogalaxy/texturepacks/cdamjcandrazius/"
                FrontendGame.MKDD, FrontendGame.MKDD_PLUS -> "https://emulationking.com/nintendo/gamecube/games/mariokartdoubledash/texturepacks/giodude/"
                else -> return
            }
            content.addView(button("Browse creator packs") { openPage(page) })
        }
    }

    private fun confirmRemove(game: FrontendGame, pack: TexturePacks.Pack) {
        if (!canRemove()) return
        ThorDialogs.builder(this).setTitle("Remove ${pack.title}?")
            .setMessage("This removes the imported copy from WiiDS.")
            .setPositiveButton("Remove") { _, _ ->
                if (canRemove()) runOperation("Removing ${pack.title}...") { context ->
                    TexturePacks.remove(context, game, pack)
                    "${pack.title} was removed."
                }
            }
            .setNegativeButton("Cancel", null).show()
    }

    private fun canRemove(): Boolean = runCatching { GameStorage.requireIdle(this) }
        .onFailure { toast("Close games and finish any active import or update first.") }.isSuccess

    private fun change(game: FrontendGame, action: () -> Unit) {
        if (TexturePacks.usesAurora(game)) {
            if (canRemove()) runOperation("Updating packs for ${game.label}...") {
                GameStorage.requireIdle(it)
                action()
                "Updated packs for ${game.label}."
            }
            return
        }
        notice = runCatching(action).exceptionOrNull()?.message.orEmpty()
        render(keepScroll = true)
    }

    @Deprecated("Android file picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK) return
        if (requestCode == ModManager.REQUEST_CODE_PICK_ZIP) {
            ModManager.onZipPicked(this, data?.data)
            return
        }
        val game = selected ?: return
        val uri = data?.data ?: return
        if (requestCode != REQUEST_ZIP || !TexturePacks.supported(game)) return
        val resolver = applicationContext.contentResolver
        install(game, {
            val filename = runCatching {
                resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) it.getString(0) else null
                }
            }.getOrNull()
            filename?.substringBeforeLast('.') ?: "Imported texture pack"
        }, { resolver.openInputStream(uri) ?: throw IOException("Android couldn't open this ZIP.") })
    }

    private fun install(game: FrontendGame, title: () -> String, source: () -> java.io.InputStream,
                        bytes: Long? = null, sha: String? = null) {
        val initial = if (bytes == null) "Importing texture pack..." else "Connecting to download for ${game.label}..."
        if (TexturePacks.usesAurora(game) && !canRemove()) return
        runOperation(initial) { context ->
            var shownPercent = -5
            val pack = TexturePacks.install(context, game, title(), source, bytes, sha) { received ->
                if (bytes != null) {
                    val percent = (received * 100 / bytes).toInt()
                    if (percent >= shownPercent + 5 || percent == 100) {
                        shownPercent = percent
                        val message = if (percent == 100) "Checking and installing for ${game.label}..."
                            else "Downloading for ${game.label}... $percent%"
                        operation = Operation(true, message)
                    }
                }
            }
            if (TexturePacks.usesAurora(game)) GameStorage.requireIdle(context)
            TexturePacks.setEnabled(context, game, pack.id, true)
            "${pack.title} is enabled for ${game.label}."
        }
    }

    private fun runOperation(working: String,
                             action: (android.content.Context) -> String) {
        if (operation.running) return
        operation = Operation(true, working)
        observed = operation
        render(keepScroll = true)
        val app = applicationContext
        Thread({
            val message = runCatching { action(app) }
                .getOrElse { "Couldn't finish. ${it.message ?: "Try again."}" }
            operation = Operation(false, message)
        }, "WiiDsTexturePacks").start()
    }

    private fun openPage(url: String) {
        runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            .onFailure { toast("Android couldn't open this creator page.") }
    }

    private fun back() {
        if (operation.running) return
        when {
            managing -> {
                managing = false
                notice = ""
                render()
            }
            selected != null -> { selected = null; notice = ""; render() }
            else -> finish()
        }
    }

    @Deprecated("Android back compatibility")
    override fun onBackPressed() = back()

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            if (event.action == KeyEvent.ACTION_UP) back()
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                aButton = currentFocus as? Button
                aButton?.isPressed = true
            }
            if (event.action == KeyEvent.ACTION_UP) {
                val button = aButton
                aButton = null
                button?.isPressed = false
                if (!event.isCanceled && button?.isEnabled == true && button === currentFocus) button.performClick()
            }
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    private fun dp(value: Int) = (resources.displayMetrics.density * value + .5f).toInt()
    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_LONG).show()
    private fun label(text: String, size: Float, bold: Boolean = false) = TextView(this).apply {
        this.text = text
        textSize = size
        setTextColor(if (bold) Color.WHITE else 0xffd1dbe5.toInt())
        if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
        setPadding(0, dp(7), 0, dp(7))
    }

    private fun button(text: String, action: () -> Unit) = Button(this).apply {
        this.text = text
        isAllCaps = false
        setTextColor(Color.WHITE)
        isEnabled = !operation.running
        isFocusableInTouchMode = true
        setPadding(dp(12), dp(6), dp(12), dp(6))
        minHeight = dp(44)
        background = StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_pressed), tile(true))
            addState(intArrayOf(android.R.attr.state_focused), tile(true))
            addState(intArrayOf(android.R.attr.state_selected), tile(true))
            addState(intArrayOf(), tile(false))
        }
        setOnTouchListener { view, event ->
            if (event.actionMasked == MotionEvent.ACTION_DOWN) view.requestFocus()
            false
        }
        setOnClickListener { action() }
    }

    private fun tile(focused: Boolean) = GradientDrawable().apply {
        cornerRadius = dp(9).toFloat()
        setColor(if (focused) 0xff24586c.toInt() else 0xff203244.toInt())
        setStroke(dp(if (focused) 2 else 1), if (focused) 0xff8ee4f3.toInt() else 0xff354b5f.toInt())
    }

    companion object {
        private const val REQUEST_ZIP = 9872
        private data class Operation(val running: Boolean, val message: String)
        @Volatile private var operation = Operation(false, "")
    }
}
