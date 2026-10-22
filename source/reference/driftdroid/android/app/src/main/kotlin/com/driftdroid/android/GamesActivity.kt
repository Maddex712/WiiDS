package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.StateListDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.provider.Settings
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import com.driftdroid.android.rom.GameCatalog
import com.driftdroid.android.rom.GameCatalog.Game
import com.driftdroid.android.rom.GameInstaller
import com.driftdroid.android.rom.EclipseMod
import com.driftdroid.android.rom.GameScanner
import com.driftdroid.android.rom.MsmbwiiMod
import com.driftdroid.android.rom.RomScanner
import com.driftdroid.android.rom.WiiDsDisc

/**
 * Sets up every game from the player's own disc images. It finds them on this device and its SD
 * card, says exactly which disc each game needs and why a found file can't be used, and imports
 * the right one. Drawn without any game artwork, because that comes from the discs themselves.
 */
class GamesActivity : Activity() {
    private class Card(val status: TextView, val progress: ProgressBar, val button: Button)

    private val handler = Handler(Looper.getMainLooper())
    private val cards = LinkedHashMap<Game, Card>()
    private val messages = HashMap<GameCatalog.Disc, String>()
    private var scan: GameScanner.Result? = null
    private var scanning = false
    private var pickFor: Game? = null
    /** Result of the last MSMBWii mod install ("" = success), shown on its card. */
    private var modMessage: String? = null
    private var eclipseMessage: String? = null
    // Galaxy: a disc chosen with Choose file (its path), and the card's "Import again" button.
    private var galaxyChosen: String? = null
    private var galaxyAgain: Button? = null
    private var foreground = false
    private lateinit var accessRow: LinearLayout
    private lateinit var searchButton: Button

    private val poll = object : Runnable {
        override fun run() {
            if (!foreground) return
            val state = GameInstaller.poll()
            if (state.finished && state.disc != null) {
                messages[state.disc] = state.error ?: ""
                GameInstaller.acknowledge()
            }
            val mod = MsmbwiiMod.state
            if (mod.finished) {
                modMessage = mod.error ?: ""
                MsmbwiiMod.acknowledge()
            }
            if (EclipseMod.state.finished) {
                eclipseMessage = EclipseMod.state.error ?: ""
                EclipseMod.acknowledge()
            }
            refresh()
            handler.postDelayed(this, 300)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(buildUi())
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        foreground = true
        accessRow.visibility = if (RomScanner.hasAccess()) View.GONE else View.VISIBLE
        if (scan == null && RomScanner.hasAccess()) startScan()
        if (!GameInstaller.running) GameInstaller.repairArt(this)
        handler.post(poll)
    }

    override fun onPause() {
        foreground = false
        handler.removeCallbacks(poll)
        super.onPause()
    }

    // --- searching ---

    private fun startScan() {
        if (scanning) return
        scanning = true
        messages.clear()
        refresh()
        Thread {
            val result = runCatching { GameScanner.scan(applicationContext) }.getOrNull()
            handler.post {
                scanning = false
                scan = result ?: GameScanner.Result(emptyList(), emptyList())
                refresh()
            }
        }.apply { name = "WiiDsScan"; isDaemon = true }.start()
    }

    private fun askForAccess() {
        val uri = Uri.parse("package:$packageName")
        runCatching { startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, uri)) }
            .onFailure { runCatching { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) } }
    }

    // --- choosing a file directly (no All files access needed) ---

    private fun chooseFile(game: Game) {
        pickFor = game
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        runCatching { startActivityForResult(intent, REQUEST_PICK) }
            .onFailure { messages[game.disc] = "This device has no file chooser. Allow access above instead." }
    }

    private fun chooseModFile(eclipse: Boolean = false) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        runCatching { startActivityForResult(intent, if (eclipse) REQUEST_PICK_ECLIPSE_PATCH else REQUEST_PICK_MOD) }
    }

    @Deprecated("Activity result API is not used by this app")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_PICK_ECLIPSE_PATCH) {
            val uri = data?.data
            if (resultCode == RESULT_OK && uri != null) {
                eclipseMessage = null
                if (!EclipseMod.start(this, uri)) eclipseMessage = BUSY
            }
            refresh()
            return
        }
        if (requestCode == REQUEST_GALAXY_IMPORT) {
            if (resultCode == RESULT_OK) { messages.remove(GameCatalog.Disc.GALAXY_USA); galaxyChosen = null }
            refresh()
            return
        }
        if (requestCode == REQUEST_PICK_MOD) {
            val uri = data?.data
            if (resultCode == RESULT_OK && uri != null) { modMessage = null; MsmbwiiMod.start(this, uri) }
            refresh()
            return
        }
        val game = pickFor ?: return
        pickFor = null
        val uri = data?.data
        if (requestCode != REQUEST_PICK || resultCode != RESULT_OK || uri == null) return
        val source = WiiDsDisc.Source.Document(uri, displayName(uri))
        messages[game.disc] = "Checking ${source.name}..."
        refresh()
        Thread {
            val message = try {
                val info = WiiDsDisc.identify(applicationContext, source)
                when {
                    game.disc.matches(info) -> null
                    game.disc.sameGame(info) -> "${source.name} can't be used. ${game.disc.mismatch(info)}"
                    else -> "${source.name} is ${info.title.ifBlank { info.gameId }} (${info.gameId}), not " +
                        "${game.disc.gameName}."
                }
            } catch (e: Exception) {
                "${source.name} can't be read. ${e.message ?: ""}".trim()
            }
            handler.post {
                if (message != null) messages[game.disc] = message
                else if (game == Game.GALAXY) {
                    galaxyChosen = discPath(source)
                    messages[game.disc] = if (galaxyChosen != null) "${source.name} is the right disc. Set up Galaxy to convert it."
                        else "${source.name} is the right disc, but Galaxy setup can only read it from this device's storage. " +
                            "Copy it there (a ROMs folder is fine) and choose it again."
                }
                else if (!GameInstaller.start(this, game.disc, source)) messages[game.disc] = BUSY
                else messages.remove(game.disc)
                refresh()
            }
        }.apply { name = "WiiDsIdentify"; isDaemon = true }.start()
    }

    private fun displayName(uri: Uri): String = runCatching {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }
    }.getOrNull() ?: "The chosen file"

    // --- state ---

    private fun refresh() {
        val state = GameInstaller.poll()
        val eclipseBusy = EclipseMod.state.running
        val installBusy = state.running || eclipseBusy
        searchButton.isEnabled = !scanning && RomScanner.hasAccess()
        searchButton.text = if (scanning) "Searching..." else "Search again"
        for ((game, card) in cards) {
            val disc = game.disc
            if (game == Game.GALAXY) {
                refreshGalaxy(card)
                continue
            }
            if (game == Game.ECLIPSE && eclipseBusy) {
                val mod = EclipseMod.state
                val percent = if (mod.total > 0) (mod.done * 100 / mod.total).toInt().coerceIn(0, 100) else 0
                card.progress.visibility = View.VISIBLE
                card.progress.progress = percent
                show(card, "${mod.phase}... $percent%. Keep WiiDS open. Sunshine and your saves stay separate.", WHITE)
                button(card, "Cancel", true) { EclipseMod.cancel() }
                continue
            }
            val importing = state.running && state.disc == disc
            card.progress.visibility = if (importing) View.VISIBLE else View.GONE
            if (importing) {
                val percent = if (state.total > 0) (state.done * 100 / state.total).toInt() else 0
                card.progress.progress = percent
                show(card, "Importing... $percent%. This takes a few minutes; keep WiiDS open.", WHITE)
                button(card, "Cancel", true) { GameInstaller.cancel() }
                continue
            }
            val message = messages[disc]
            val found = scan?.matching(disc).orEmpty()
            when {
                game == Game.ECLIPSE && !disc.isInstalled(this) && GameCatalog.Disc.SUNSHINE_USA.isInstalled(this) -> {
                    val failed = !eclipseMessage.isNullOrEmpty()
                    if (EclipseMod.bundled(this)) {
                        show(card, if (failed) "$eclipseMessage You can retry with the included Eclipse 1.1.0 patch." else
                            "Your Sunshine disc is ready. Eclipse Team's 1.1.0 patch is included; your original game and saves are kept.",
                            if (failed) AMBER else GREEN)
                        button(card, if (failed) "Retry Eclipse installation" else "Install Eclipse", !installBusy) {
                            eclipseMessage = null
                            if (!EclipseMod.start(this)) eclipseMessage = BUSY
                        }
                    } else {
                        show(card, if (failed) "$eclipseMessage Choose the raw v1.1.0.xdelta from Eclipse Team's official patch pack to try another copy." else
                            "Your Sunshine disc is ready. Choose patches/v1.1.0.xdelta from Eclipse Team's official 1.1.0 pack, or import an already patched GMSE04 image.",
                            if (failed) AMBER else GREEN)
                        button(card, "Choose Eclipse patch", !installBusy) { chooseModFile(eclipse = true) }
                    }
                }
                message != null && message.isNotEmpty() ->
                    show(card, message, AMBER).also { button(card, "Choose file", !installBusy) { chooseFile(game) } }
                game == Game.MSMBWII && MsmbwiiMod.state.running -> {
                    val done = MsmbwiiMod.state.done
                    card.progress.visibility = View.VISIBLE
                    card.progress.progress = (done * 100 / MsmbwiiMod.SIZE).toInt()
                    show(card, (if (MsmbwiiMod.bundled(this)) "Installing the MSMBWii mod... " else
                        "Getting the MSMBWii mod from ${MsmbwiiMod.SOURCE_NAME}... ") + "${done * 100 / MsmbwiiMod.SIZE}%", WHITE)
                    button(card, "Cancel", true) { MsmbwiiMod.cancel() }
                }
                disc.isInstalled(this) && game == Game.MSMBWII && !GameCatalog.msmbwiiModInstalled(this) -> {
                    val failed = !modMessage.isNullOrEmpty()
                    if (MsmbwiiMod.bundled(this)) {
                        show(card, if (failed) "$modMessage You can retry with the included MSMBWii mod." else
                            "Your disc is ready. The mod is included with WiiDS.", if (failed) AMBER else GREEN)
                        button(card, if (failed) "Retry mod installation" else "Install mod", !installBusy) {
                            modMessage = null
                            if (!MsmbwiiMod.start(this)) modMessage = BUSY
                        }
                    } else if (failed) {
                        show(card, "$modMessage You can also download MSMBWii.zip yourself from ${MsmbwiiMod.SOURCE_NAME} and choose it.", AMBER)
                        button(card, "Choose MSMBWii.zip", !installBusy) { chooseModFile() }
                    } else {
                        show(card, "Your disc is ready. The free mod comes from its official page " +
                            "(MSMBWii.zip, 2.9 MB, ${MsmbwiiMod.SOURCE_NAME}).", GREEN)
                        button(card, "Download mod", !installBusy) {
                            modMessage = null
                            if (!MsmbwiiMod.start(this)) modMessage = BUSY
                        }
                    }
                }
                disc.isInstalled(this) && com.driftdroid.android.rom.WiiDsArt.building == disc -> {
                    show(card, "Imported. Building its menus, sounds and maps from your disc...", GREEN)
                    button(card, "Import again", false) {}
                }
                disc.isInstalled(this) -> {
                    show(card, if (message == "") "Imported. Ready to play." else "Installed. Ready to play.", GREEN)
                    val source = found.firstOrNull()?.source
                    button(card, "Import again", source != null && !installBusy) {
                        if (source != null && !GameInstaller.start(this, disc, source)) messages[disc] = BUSY
                    }
                }
                found.isNotEmpty() -> {
                    val pick = found.first()
                    show(card, "Found ${pick.source.name} (${pick.info.format}, ${gb(pick.source.size)}).", GREEN)
                    button(card, "Import", !installBusy) {
                        if (!GameInstaller.start(this, disc, pick.source)) messages[disc] = BUSY
                    }
                }
                scanning -> show(card, "Searching this device...", MUTED).also { button(card, "Choose file", false) {} }
                else -> {
                    val wrong = scan?.mismatched(disc)?.firstOrNull()
                    when {
                        wrong != null -> show(card, "Found ${wrong.first.source.name}, but it can't be used. ${wrong.second}", AMBER)
                        scan == null -> show(card, "Allow access above so WiiDS can find it, or choose the file.", MUTED)
                        else -> show(card, "Not found on this device yet.", MUTED)
                    }
                    button(card, "Choose file", !installBusy) { chooseFile(game) }
                }
            }
        }
    }

    /**
     * Galaxy plays in its own add-on, which converts and keeps its own game data: this card says
     * whether it is ready and hands the add-on the right disc (GalaxyImportActivity there does the
     * work, keeping saves when it imports again).
     */
    private fun refreshGalaxy(card: Card) {
        if (android.os.Build.VERSION.SDK_INT < 32) { show(card, "Galaxy needs Android 12L or newer (API 32).", AMBER); button(card, "Android update required", false) {}; return }
        val disc = GameCatalog.Disc.GALAXY_USA
        card.progress.visibility = View.GONE
        val message = messages[disc]
        val found = scan?.matching(disc).orEmpty()
        val foundPath = found.firstNotNullOfOrNull { discPath(it.source) }
        val path = galaxyChosen ?: foundPath
        val ready = disc.isInstalled(this)
        when {
            ready -> show(card, "Installed. Ready to play.", GREEN)
            message != null && message.isNotEmpty() -> show(card, message, if (galaxyChosen != null) GREEN else AMBER)
            path != null -> show(card, "Found ${java.io.File(path).name}, the right disc. Set up Galaxy to convert it " +
                "(about 15 minutes; keep the device charging).", GREEN)
            scanning -> show(card, "Searching this device...", MUTED)
            else -> {
                val wrong = scan?.mismatched(disc)?.firstOrNull()
                when {
                    wrong != null -> show(card, "Found ${wrong.first.source.name}, but it can't be used. ${wrong.second}", AMBER)
                    scan == null -> show(card, "Allow access above so WiiDS can find it, or choose the file.", MUTED)
                    else -> show(card, "Not found on this device yet.", MUTED)
                }
            }
        }
        when {
            ready -> button(card, "Play", true) { ThorGameLaunch.launchGalaxy(this) }
            path != null -> button(card, "Set up Galaxy", !GameInstaller.running) { importGalaxy(path) }
            else -> button(card, "Choose file", !GameInstaller.running) { chooseFile(Game.GALAXY) }
        }
        galaxyAgain?.let { again ->
            again.visibility = if (ready && path != null) View.VISIBLE else View.GONE
            again.setOnClickListener { if (path != null) importGalaxy(path) }
        }
    }

    /** A disc WiiDS can name by path (the add-on reads it there with its own All files access). */
    private fun discPath(source: WiiDsDisc.Source): String? = when (source) {
        is WiiDsDisc.Source.Files -> source.parts.first().path
        is WiiDsDisc.Source.Document -> runCatching {
            val id = android.provider.DocumentsContract.getDocumentId(source.uri)
            when {
                id.startsWith("raw:") -> id.removePrefix("raw:")
                id.startsWith("primary:") -> android.os.Environment.getExternalStorageDirectory().path + "/" + id.removePrefix("primary:")
                else -> null
            }
        }.getOrNull()?.takeIf { java.io.File(it).isFile }
    }

    private fun importGalaxy(path: String) {
        val intent = Intent("dev.thor.galaxy.action.IMPORT_DISC")
            .setClassName(packageName, "dev.thor.galaxy.GalaxyImportActivity")
            .putExtra("disc_path", path)
        if (runCatching { startActivityForResult(intent, REQUEST_GALAXY_IMPORT) }.isFailure)
            messages[GameCatalog.Disc.GALAXY_USA] = "Galaxy setup is unavailable on this Android version."
    }

    private fun show(card: Card, text: String, color: Int) {
        if (card.status.text != text) card.status.text = text
        card.status.setTextColor(color)
    }

    private fun button(card: Card, label: String, enabled: Boolean, action: () -> Unit) {
        if (card.button.text != label) card.button.text = label
        card.button.isEnabled = enabled
        card.button.setOnClickListener { action(); refresh() }
    }

    private fun gb(bytes: Long) = "%.1f GB".format(bytes / 1_073_741_824.0)

    // --- layout ---

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    private fun text(value: String, sp: Float, color: Int, bold: Boolean = false) = TextView(this).apply {
        text = value
        textSize = sp
        setTextColor(color)
        if (bold) typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
    }

    private fun rounded(color: Int, stroke: Int = 0) = GradientDrawable().apply {
        cornerRadius = dp(14).toFloat()
        setColor(color)
        if (stroke != 0) setStroke(dp(3), stroke)
    }

    private fun styledButton(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        textSize = 16f
        setTextColor(Color.WHITE)
        background = StateListDrawable().apply {
            addState(intArrayOf(-android.R.attr.state_enabled), rounded(0xFF2A3A52.toInt()))
            addState(intArrayOf(android.R.attr.state_focused), rounded(0xFF2F7BEA.toInt(), Color.WHITE))
            addState(intArrayOf(android.R.attr.state_pressed), rounded(0xFF2F7BEA.toInt()))
            addState(intArrayOf(), rounded(0xFF285CA8.toInt()))
        }
        setPadding(dp(22), dp(10), dp(22), dp(10))
        isFocusable = true
        setOnClickListener { onClick() }
    }

    private fun buildUi(): View {
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(40), dp(28), dp(40), dp(28))
        }
        column.addView(text("Your games", 30f, WHITE, bold = true))
        column.addView(text("Copy your own disc images anywhere on this device or its SD card (a ROMs folder is " +
            "fine). WiiDS finds them, checks each one and imports the right disc. Your files are only read.",
            15f, MUTED).apply { setPadding(0, dp(6), 0, dp(16)) })

        accessRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = rounded(0xFF3A2E12.toInt())
            setPadding(dp(20), dp(14), dp(20), dp(14))
            addView(text("To search for your discs, WiiDS needs All files access. Only disc headers are read, " +
                "and nothing leaves this device.", 15f, WHITE),
                LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
            addView(styledButton("Allow access") { askForAccess() })
        }
        column.addView(accessRow, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(16) })

        val games = Game.values().toList()
        for (row in games.chunked(2)) {
            val line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            row.forEachIndexed { index, game ->
                line.addView(buildCard(game), LinearLayout.LayoutParams(0, -1, 1f).apply {
                    if (index == 0) marginEnd = dp(16)
                })
            }
            column.addView(line, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(16) })
        }

        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        searchButton = styledButton("Search again") { startScan() }
        actions.addView(searchButton)
        actions.addView(styledButton("Back") { finish() }, LinearLayout.LayoutParams(-2, -2).apply { marginStart = dp(12) })
        actions.addView(text("Accepted formats: ${GameCatalog.ACCEPTED_FORMATS}", 13f, MUTED).apply {
            setPadding(dp(20), 0, 0, 0)
        }, LinearLayout.LayoutParams(0, -2, 1f))
        column.addView(actions)

        return ScrollView(this).apply {
            isFillViewport = true
            background = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(0xFF13263F.toInt(), 0xFF08111E.toInt()))
            addView(column)
        }
    }

    private fun buildCard(game: Game): View {
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = rounded(0xFF1A2D47.toInt())
            setPadding(dp(22), dp(18), dp(22), dp(18))
        }
        box.addView(text(game.title, 21f, WHITE, bold = true))
        game.note?.let { box.addView(text(it, 13f, MUTED).apply { setPadding(0, dp(2), 0, 0) }) }
        box.addView(text("Needs: ${game.disc.requirement}", 14f, 0xFFCFE0F5.toInt()).apply { setPadding(0, dp(8), 0, 0) })
        val status = text("", 15f, MUTED).apply { setPadding(0, dp(8), 0, dp(8)) }
        box.addView(status, LinearLayout.LayoutParams(-1, LinearLayout.LayoutParams.WRAP_CONTENT))
        // Pushes the progress bar and button to the card's bottom so neighbouring cards line up.
        box.addView(View(this), LinearLayout.LayoutParams(-1, 0, 1f))
        val progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 100
            visibility = View.GONE
        }
        box.addView(progress, LinearLayout.LayoutParams(-1, dp(10)).apply { bottomMargin = dp(10) })
        val action = styledButton("Choose file") {}
        box.addView(action, LinearLayout.LayoutParams(-2, -2))
        if (game == Game.GALAXY) {
            box.addView(styledButton("Import legacy Galaxy backup") { startActivity(Intent().setClassName(packageName, "dev.thor.galaxy.GalaxyBackupActivity")) }, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(8) })
            galaxyAgain = styledButton("Import again (keeps your saves)") {}.also {
                it.visibility = View.GONE
                box.addView(it, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(8) })
            }
        }
        if (game == Game.ECLIPSE) {
            box.addView(styledButton("Import patched image") { if (!GameInstaller.running && !EclipseMod.state.running) chooseFile(game) },
                LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(8) })
            box.addView(styledButton("Official Eclipse page") {
                runCatching { startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://gamebanana.com/mods/536309"))) }
            }, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(8) })
        }
        cards[game] = Card(status, progress, action)
        return box
    }

    private fun hideSystemBars() {
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        if (Build.VERSION.SDK_INT >= 30) window.insetsController?.apply {
            systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsets.Type.systemBars())
        }
    }

    /** Controller: A presses the focused button, B goes back. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_B -> {
                if (event.action == KeyEvent.ACTION_UP) finish()
                return true
            }
            KeyEvent.KEYCODE_BUTTON_A -> {
                val focus = currentFocus as? Button ?: return super.dispatchKeyEvent(event)
                focus.isPressed = event.action == KeyEvent.ACTION_DOWN
                if (event.action == KeyEvent.ACTION_UP && focus.isEnabled) focus.performClick()
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    companion object {
        private const val REQUEST_PICK = 4710
        private const val REQUEST_PICK_MOD = 4711
        private const val REQUEST_PICK_ECLIPSE_PATCH = 4712
        private const val REQUEST_GALAXY_IMPORT = 4713
        private const val BUSY = "Another disc is being imported. Wait for it to finish."
        private const val WHITE = Color.WHITE
        private const val MUTED = 0xFFA9BBD3.toInt()
        private const val GREEN = 0xFF8EE6A0.toInt()
        private const val AMBER = 0xFFFFC86B.toInt()
    }
}
