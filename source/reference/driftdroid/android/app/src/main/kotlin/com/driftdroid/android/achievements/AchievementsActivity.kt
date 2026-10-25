package com.driftdroid.android.achievements

import android.app.Activity
import android.app.Dialog
import android.app.Presentation
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.util.Log
import android.view.Display
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import com.driftdroid.android.FrontendGame
import com.driftdroid.android.ThorAchievementText
import com.driftdroid.android.ThorLauncherAudio
import com.driftdroid.android.ThorMenuArtwork
import org.json.JSONObject
import java.io.File
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * The WiiDS achievement browser: each game's RetroAchievements sets (base set and subsets) with
 * everything this account has already earned, in the launcher's own chrome, font and gold
 * selection. Upper screen: the game, connection, filters and the badge grid grouped by set;
 * lower screen: the focused achievement in full (an overlay without one). Reads the cached RA
 * library on a worker; only Connect/Refresh (or the library's daily refresh) contact RA.
 */
class AchievementsActivity : Activity(), DisplayManager.DisplayListener {
    companion object {
        const val EXTRA_GAME = "wiids.achievements.game"
        const val EXTRA_ACHIEVEMENT = "wiids.achievements.focus"
        private const val TAG = "WiiDSAchievements"
        /** Filter and focus per game while the app runs, so reopening returns to the same view. */
        private val remembered = HashMap<String, Bundle>()
        private val FILTERS = listOf("All", "Earned", "Locked")
        /** Library shelf order. */
        private val GAMES = listOf(FrontendGame.MARIO_KART, FrontendGame.RETRO_REWIND, FrontendGame.NSMB, FrontendGame.MSMBWII,
            FrontendGame.SUNSHINE, FrontendGame.ECLIPSE, FrontendGame.GALAXY, FrontendGame.KAIZO)
        fun gameTitle(id: String): String = when (FrontendGame.fromId(id)) {
            FrontendGame.MARIO_KART -> "Mario Kart Wii"
            FrontendGame.RETRO_REWIND -> "Retro Rewind"
            FrontendGame.NSMB -> "New Super Mario Bros. Wii"
            FrontendGame.MSMBWII -> "MSMBWii: Just a Little Adventure"
            FrontendGame.SUNSHINE -> "Super Mario Sunshine"
            FrontendGame.ECLIPSE -> "Super Mario Eclipse"
            FrontendGame.GALAXY -> "Super Mario Galaxy"
            FrontendGame.KAIZO -> "Kaizo Mario Galaxy Rebalanced"
            null -> "WiiDS"
        }
    }

    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "AchievementBrowser").apply { isDaemon = true } }
    private val badgeWorker = Executors.newSingleThreadExecutor { r -> Thread(r, "AchievementBadges").apply { isDaemon = true } }
    private val main = Handler(Looper.getMainLooper())
    private lateinit var credentials: RaCredentials
    private lateinit var badgeArt: AchievementBadges
    private lateinit var ui: Ui
    private lateinit var browser: BrowserView
    private var displays: DisplayManager? = null
    private var lower: DetailPresentation? = null
    private var closed = false

    // ------------------------------------------------------------------ state
    private var game = FrontendGame.NSMB.id
    private var filter = 0
    private var account: String? = null
    private var library: JSONObject? = null
    private var snapshot = AchievementCatalog.of(null, game, false)
    private var status = ""
    private var busy = false
    private var loaded = false
    /** Focused achievement ID; null with [actionFocus] >= 0 focuses the connection buttons. */
    private var focusId: String? = null
    private var actionFocus = -1
    private var detailOverlay = false
    private val badges = HashMap<String, Bitmap>()
    private val requested = HashSet<String>()
    /** Badge keys shown with the fallback trophy until RA's picture is downloaded. */
    private val waiting = HashSet<String>()

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        credentials = RaCredentials(this)
        badgeArt = AchievementBadges(this)
        game = intent.getStringExtra(EXTRA_GAME)?.takeIf { RaLibrary.known(it) } ?: FrontendGame.NSMB.id
        (saved ?: remembered[game])?.let { state ->
            filter = state.getInt("filter").coerceIn(0, 2)
            focusId = state.getString("focus")
            state.getString("game")?.takeIf { saved != null && RaLibrary.known(it) }?.let { game = it }
        }
        intent.getStringExtra(EXTRA_ACHIEVEMENT)?.let { focusId = it; filter = 0 }
        ui = Ui.load(this)
        browser = BrowserView(this)
        setContentView(browser)
        displays = getSystemService(DisplayManager::class.java)
        load()
    }

    private fun state() = Bundle().apply { putInt("filter", filter); putString("focus", focusId); putString("game", game) }
    override fun onSaveInstanceState(out: Bundle) { super.onSaveInstanceState(out); out.putAll(state()) }

    override fun onResume() {
        super.onResume()
        fullscreen()
        displays?.registerDisplayListener(this, main)
        refreshLower()
    }

    override fun onPause() {
        remembered[game] = state()
        displays?.unregisterDisplayListener(this)
        closeLower()
        super.onPause()
    }

    override fun onDestroy() {
        closed = true
        worker.shutdown(); badgeWorker.shutdownNow()
        super.onDestroy()
    }

    override fun finish() {
        super.finish()
        @Suppress("DEPRECATION") overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) { super.onWindowFocusChanged(hasFocus); if (hasFocus) fullscreen() }

    private fun fullscreen() {
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN or WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.apply {
            systemBarsBehavior = android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(android.view.WindowInsets.Type.systemBars())
        }
    }

    // ------------------------------------------------------------------ lower display
    override fun onDisplayAdded(displayId: Int) = refreshLower()
    override fun onDisplayRemoved(displayId: Int) = refreshLower()
    override fun onDisplayChanged(displayId: Int) = refreshLower()

    private fun refreshLower() {
        if (closed || isFinishing) return
        val upper = windowManager.defaultDisplay.displayId
        var target: Display? = null
        for (display in displays?.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).orEmpty()) {
            if (!display.isValid || display.displayId == upper) continue
            if (target == null || display.name == "Screen-2") target = display
        }
        if (target != null && lower?.isShowing == true && lower?.display?.displayId == target.displayId) return
        closeLower()
        if (target != null) {
            val next = DetailPresentation(target)
            lower = next
            next.setOnDismissListener { if (lower === next) { lower = null; browser.invalidate() } }
            try { next.show() } catch (failure: RuntimeException) { Log.w(TAG, "Lower display unavailable", failure); lower = null }
        }
        if (lower != null) detailOverlay = false
        browser.invalidate()
    }

    private fun closeLower() { val previous = lower; lower = null; previous?.dismiss() }
    private fun dualScreen() = lower?.isShowing == true

    // ------------------------------------------------------------------ data
    private fun visible(): List<AchievementCatalog.Item> = snapshot.items.filter {
        when (filter) { 1 -> it.earned(); 2 -> !it.earned(); else -> true }
    }
    private fun focused(): AchievementCatalog.Item? = visible().firstOrNull { it.id == focusId }

    private fun load() {
        busy = true; changed()
        worker.execute {
            val result = RaLibrary.load(this)
            main.post {
                if (closed) return@post
                account = result.account; library = result.library
                if (result.warning.isNotEmpty()) status = result.warning
                busy = false; loaded = true
                rebuild()
            }
        }
    }

    /** New snapshot for the current game from the loaded library. */
    private fun rebuild() {
        snapshot = AchievementCatalog.of(library, game, account != null)
        keepFocus(); changed()
    }

    /** Keeps the focused achievement when it is still shown, else the first one (or the buttons). */
    private fun keepFocus() {
        val list = visible()
        if (list.none { it.id == focusId }) focusId = list.firstOrNull()?.id
        actionFocus = if (focusId == null) 0 else -1
    }

    private fun changed() {
        requestBadges()
        browser.relayout()
        browser.invalidate()
        lower?.view?.invalidate()
    }

    private fun badgeKey(item: AchievementCatalog.Item) = item.badge + "|" + item.earned()

    /** Badges for the shown list, composed off the UI thread, nearest the focus first. */
    private fun requestBadges() {
        val list = visible().ifEmpty { listOf(AchievementCatalog.placeholder()) }
        val start = max(0, list.indexOfFirst { it.id == focusId })
        val order = list.drop(start) + list.take(start)
        for (item in order.take(600)) {
            val key = badgeKey(item)
            if (badges.containsKey(key) || !requested.add(key)) continue
            badgeWorker.execute {
                val bitmap = runCatching { badgeArt.badge(item, 256) }.getOrNull()
                val ready = badgeArt.pictureReady(item)
                main.post {
                    requested.remove(key)
                    if (!closed && bitmap != null) {
                        badges[key] = bitmap
                        if (ready) waiting.remove(key) else waiting.add(key)
                        browser.invalidate(); lower?.view?.invalidate()
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ actions
    private fun setFilter(value: Int) { filter = (value + 3) % 3; keepFocus(); play(ThorLauncherAudio.Sound.FOCUS); changed() }

    private fun showGame(step: Int) {
        remembered[game] = state()
        val index = GAMES.indexOfFirst { it.id == game }.coerceAtLeast(0)
        game = GAMES[(index + step + GAMES.size) % GAMES.size].id
        filter = remembered[game]?.getInt("filter") ?: 0
        focusId = remembered[game]?.getString("focus")
        detailOverlay = false
        play(ThorLauncherAudio.Sound.FOCUS)
        rebuild()
    }

    /** Connection buttons for the current state, in focus order. */
    private fun actions(): List<Pair<String, () -> Unit>> =
        if (account == null) listOf("Connect RetroAchievements" to ::connect)
        else listOf("Refresh" to ::refresh, "Change connection" to ::connect, "Disconnect" to ::disconnect)

    private fun refresh() {
        if (busy) return
        val refused = RaLibrary.syncNow(this, ::synced)
        if (refused != null) { status = refused; changed(); return }
        busy = true; status = "Refreshing your RetroAchievements progress..."; changed()
    }

    private fun synced(ok: Boolean, message: String, result: JSONObject?) {
        if (closed) return
        busy = false
        status = message
        if (ok && result != null) {
            library = result
            // Only placeholders are redrawn; badges already showing RA's picture stay put.
            badgeArt.forgetImported()
            badges.keys.removeAll(waiting)
            waiting.clear()
        }
        rebuild()
    }

    private fun disconnect() {
        if (busy) return
        busy = true; changed()
        worker.execute {
            try {
                synchronized(RaCredentials::class.java) {
                    val old = try { credentials.read() } catch (broken: java.io.IOException) { null }
                    credentials.clear()
                    RaLibrary.forget(this, old?.get(0))
                }
                badgeArt.forgetImported()
                main.post {
                    if (closed) return@post
                    account = null; library = null; busy = false; focusId = null
                    status = "RetroAchievements connection and saved progress removed."
                    badges.clear()
                    rebuild()
                }
            } catch (failure: Exception) {
                main.post { if (!closed) { busy = false; status = "Could not finish disconnecting. Try again."; changed() } }
            }
        }
    }

    /** The connection form, styled like the launcher. The key never leaves this device unencrypted. */
    private fun connect() {
        if (busy) return
        val dialog = Dialog(this, android.R.style.Theme_Black_NoTitleBar_Fullscreen)
        val pad = (28 * resources.displayMetrics.density).toInt()
        val form = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(pad * 2, pad, pad * 2, pad); gravity = Gravity.CENTER_HORIZONTAL
            background = android.graphics.drawable.GradientDrawable(android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(0xff1c2533.toInt(), 0xff0c1118.toInt()))
        }
        fun label(value: String, size: Float) = TextView(this).apply {
            text = ui.styled(value); textSize = size; setTextColor(Color.WHITE); setPadding(0, 0, 0, pad / 3)
            form.addView(this, LinearLayout.LayoutParams(-1, -2))
        }
        fun field(hint: String) = EditText(this).apply {
            setSingleLine(); this.hint = hint; isSaveEnabled = false; textSize = 22f
            setTextColor(Color.WHITE); setHintTextColor(0xff8e979f.toInt()); setPadding(pad / 2, pad / 3, pad / 2, pad / 3)
            background = android.graphics.drawable.GradientDrawable().apply { setColor(0xff0a0f16.toInt()); setStroke(3, 0xfff2c14e.toInt()); cornerRadius = pad / 2f }
            form.addView(this, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = pad / 2 })
        }
        label("Connect RetroAchievements", 30f)
        label("Enter your RA username (or account ULID) and the Web API key from your RA account control panel. " +
            "It is encrypted on this device. WiiDS reads your achievements for its games and never submits unlocks.", 17f)
        val name = field("RA username or ULID").apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            account?.let { setText(it) }
        }
        val key = field("Web API key").apply {
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
            importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS
        }
        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.END }
        fun button(value: String, primary: Boolean, action: () -> Unit) = Button(this).apply {
            text = ui.styled(value); isAllCaps = false; textSize = 20f; setTextColor(Color.WHITE)
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (primary) 0xff6b5212.toInt() else 0xff1a2230.toInt()); setStroke(4, if (primary) 0xfff2c14e.toInt() else 0xff59636d.toInt())
                cornerRadius = pad.toFloat()
            }
            setPadding(pad, pad / 3, pad, pad / 3)
            setOnClickListener { action() }
            buttons.addView(this, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = pad / 2 })
        }
        button("Cancel", false) { dialog.dismiss() }
        button("Connect and load", true) {
            val user = name.text.toString().trim()
            val secret = key.text.toString().trim()
            try { RaProgress.account(user); if (secret.isEmpty()) throw java.io.IOException("Enter your Web API key.") }
            catch (error: java.io.IOException) { name.error = error.message; return@button }
            busy = true; changed(); dialog.dismiss()
            worker.execute {
                try {
                    synchronized(RaCredentials::class.java) {
                        val old = try { credentials.read() } catch (broken: java.io.IOException) { null } // explicit reconnection repairs it
                        if (old != null && !old[0].equals(user, ignoreCase = true)) RaLibrary.forget(this, old[0])
                        credentials.save(user, secret)
                    }
                    RaLibrary.resetSchedule(this)
                    main.post {
                        if (closed) return@post
                        account = user; library = null; busy = false
                        rebuild(); refresh()
                    }
                } catch (failure: Exception) {
                    main.post { if (!closed) { busy = false; status = "Could not save the connection. Your saved progress was kept."; changed() } }
                }
            }
        }
        form.addView(buttons, LinearLayout.LayoutParams(-1, -2).apply { topMargin = pad / 2 })
        val frame = android.widget.FrameLayout(this).apply {
            setBackgroundColor(0xcc000000.toInt())
            addView(form, android.widget.FrameLayout.LayoutParams((1180 * resources.displayMetrics.widthPixels / 1920f).toInt(), -2, Gravity.CENTER))
        }
        dialog.setContentView(frame)
        dialog.window?.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        dialog.setOnDismissListener { key.setText("") }
        dialog.show()
        name.requestFocus()
    }

    private fun play(sound: ThorLauncherAudio.Sound) = runCatching { ThorLauncherAudio.play(sound) }

    // ------------------------------------------------------------------ input
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val repeat = event.repeatCount
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT ->
                if (repeat == 0 || repeat % 3 == 0) move(keyCode)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> if (repeat == 0) activate()
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> if (repeat == 0) back()
            KeyEvent.KEYCODE_BUTTON_L1 -> if (repeat == 0) setFilter(filter - 1)
            KeyEvent.KEYCODE_BUTTON_R1 -> if (repeat == 0) setFilter(filter + 1)
            KeyEvent.KEYCODE_BUTTON_L2 -> if (repeat == 0) showGame(-1)
            KeyEvent.KEYCODE_BUTTON_R2 -> if (repeat == 0) showGame(1)
            KeyEvent.KEYCODE_BUTTON_Y -> if (repeat == 0) { if (account == null) connect() else refresh() }
            else -> return super.onKeyDown(keyCode, event)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> true
        else -> super.onKeyUp(keyCode, event)
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { back() }

    private fun back() {
        if (detailOverlay) { detailOverlay = false; play(ThorLauncherAudio.Sound.CLOSE); browser.invalidate(); return }
        play(ThorLauncherAudio.Sound.CLOSE)
        finish()
    }

    private fun activate() {
        if (actionFocus >= 0) { actions().getOrNull(actionFocus)?.let { play(ThorLauncherAudio.Sound.SELECT); it.second() }; return }
        if (focused() != null && !dualScreen()) { detailOverlay = !detailOverlay; play(ThorLauncherAudio.Sound.SELECT); browser.invalidate() }
    }

    private fun move(keyCode: Int) {
        if (detailOverlay) return
        val cells = browser.cells.filter { it.item != null }
        val actionCount = actions().size
        val index = cells.indexOfFirst { it.item!!.id == focusId }
        if (actionFocus >= 0 || index < 0) {
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_LEFT -> actionFocus = max(0, actionFocus - 1)
                KeyEvent.KEYCODE_DPAD_RIGHT -> actionFocus = min(actionCount - 1, actionFocus + 1)
                KeyEvent.KEYCODE_DPAD_DOWN -> if (cells.isNotEmpty()) { actionFocus = -1; focusId = cells.first().item!!.id } else return
                else -> return
            }
        } else {
            val here = cells[index]
            val next = when (keyCode) {
                KeyEvent.KEYCODE_DPAD_LEFT -> cells.getOrNull(index - 1)?.takeIf { it.row == here.row }
                KeyEvent.KEYCODE_DPAD_RIGHT -> cells.getOrNull(index + 1)?.takeIf { it.row == here.row }
                KeyEvent.KEYCODE_DPAD_UP -> cells.filter { it.row == here.row - 1 }.let { row -> row.firstOrNull { it.col == here.col } ?: row.lastOrNull() }
                else -> cells.filter { it.row == here.row + 1 }.let { row -> row.firstOrNull { it.col == here.col } ?: row.lastOrNull() }
            }
            when {
                next != null -> focusId = next.item!!.id
                keyCode == KeyEvent.KEYCODE_DPAD_UP && here.row == 0 && actionCount > 0 -> { actionFocus = 0 }
                else -> return
            }
        }
        play(ThorLauncherAudio.Sound.FOCUS)
        browser.revealFocus()
        changed()
    }

    // ------------------------------------------------------------------ drawing
    /** Launcher chrome and font; falls back to drawn panels when the disc art is missing. */
    private class Ui(val font: ThorMenuArtwork?, val chrome: Map<String, Bitmap>, val text: AchievementText) {
        fun styled(value: String): CharSequence = font?.styledText(value) ?: value
        companion object {
            fun load(context: Context): Ui = runCatching {
                val folder = File(context.filesDir, "WiiCompiled/ThorMap")
                val manifest = JSONObject(File(folder, "menu.json").readText())
                require(manifest.getInt("schema") == 1)
                val art = ThorMenuArtwork(folder)
                val images = manifest.getJSONObject("images")
                val chrome = HashMap<String, Bitmap>()
                for (role in listOf("background", "header", "button", "button_selected", "footer", "panel"))
                    runCatching { art.bitmap(images.getJSONObject(role).getString("texture")) }.getOrNull()?.let { chrome[role] = it }
                Ui(art, chrome, ThorAchievementText.create(context))
            }.getOrElse { Ui(null, emptyMap(), AchievementText.Plain()) }
        }
    }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val rect = RectF()

    private fun chrome(c: Canvas, role: String, area: RectF, alpha: Int = 255) {
        val bitmap = ui.chrome[role]
        val font = ui.font
        if (bitmap != null && font != null) {
            when (role) {
                "button", "button_selected" -> font.button(c, bitmap, area, alpha)
                "panel" -> font.roundedImage(c, bitmap, area, 24f, alpha)
                else -> font.image(c, bitmap, area, alpha)
            }
            return
        }
        // Drawn stand-ins in the same family: dark bands, bevelled buttons, gold selection.
        when (role) {
            "background" -> { paint.color = 0xff0b0d12.toInt(); c.drawRect(area, paint); paint.color = 0xff15181f.toInt()
                var y = area.top; while (y < area.bottom) { c.drawRect(area.left, y, area.right, y + 6f, paint); y += 12f } }
            "header", "footer" -> { paint.color = 0xf2eef1f6.toInt(); c.drawRect(area, paint) }
            "panel" -> { paint.color = 0xf0e9edf2.toInt(); c.drawRoundRect(area, 24f, 24f, paint) }
            else -> {
                paint.color = if (role == "button_selected") 0xff6b5212.toInt() else 0xff101318.toInt()
                c.drawRoundRect(area, area.height() * 0.28f, area.height() * 0.28f, paint)
                stroke.strokeWidth = 4f; stroke.color = if (role == "button_selected") 0xfff2c14e.toInt() else 0xff59636d.toInt()
                c.drawRoundRect(area, area.height() * 0.28f, area.height() * 0.28f, stroke)
            }
        }
    }

    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, center: Boolean = false, alpha: Int = 255) {
        if (value.isEmpty()) return
        if (alpha >= 255) { ui.text.draw(c, value, x, y, size, width, center); return }
        val layer = c.saveLayerAlpha((if (center) x - width / 2f else x) - 20f, y - 20f, (if (center) x + width / 2f else x + width) + 20f, y + size * 1.6f, alpha)
        ui.text.draw(c, value, x, y, size, width, center)
        c.restoreToCount(layer)
    }

    private fun fit(value: String, size: Float, width: Float): String {
        if (ui.text.width(value, size) <= width) return value
        var cut = value
        while (cut.isNotEmpty() && ui.text.width("$cut...", size) > width) cut = cut.substringBeforeLast(' ', cut.dropLast(1))
        return "$cut..."
    }

    private fun date(time: Long) = DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(time))

    private fun statusLine(item: AchievementCatalog.Item): String = when {
        !item.earned() -> "Locked  /  ${item.raPoints} points"
        item.hardcore() -> "Earned ${date(item.earnedAt)} (Hardcore)  /  ${item.raPoints} points"
        else -> "Earned ${date(item.earnedAt)}  /  ${item.raPoints} points"
    }

    /** A grid position: a set heading or an achievement card (two per row). */
    private class Cell(val item: AchievementCatalog.Item?, val set: AchievementCatalog.Set?, val top: Float, val col: Int, val row: Int)

    /** The upper screen: header, game switcher, connection, filters, badge grid grouped by set. */
    private inner class BrowserView(context: Context) : View(context) {
        private val hits = ArrayList<Pair<RectF, () -> Unit>>()
        private var scale = 1f; private var offX = 0f; private var offY = 0f
        private var scroll = 0f; private var scrollTarget = 0f
        private val focusRect = RectF(); private var focusShown = false
        private var downX = 0f; private var downY = 0f; private var dragging = false; private var dragStart = 0f
        private val gridTop = 352f; private val gridBottom = 990f; private val rowStep = 168f; private val headStep = 58f
        var cells: List<Cell> = emptyList(); private set
        private var contentHeight = 0f
        // Not focusable: keys reach the Activity directly (the first D-pad press is never spent leaving touch mode).
        init { isFocusable = false; isFocusableInTouchMode = false; defaultFocusHighlightEnabled = false }

        /** Lays the visible achievements out by set; headings only when the game has several sets. */
        fun relayout() {
            val list = visible()
            val out = ArrayList<Cell>()
            var y = 0f; var row = 0
            val headings = snapshot.sets.size > 1
            for (set in snapshot.sets) {
                val members = list.filter { it.setTitle == set.title }
                if (members.isEmpty()) continue
                if (headings) { out += Cell(null, set, y, 0, -1); y += headStep }
                members.forEachIndexed { k, item ->
                    out += Cell(item, set, y, k % 2, row)
                    if (k % 2 == 1 || k == members.size - 1) { y += rowStep; row++ }
                }
            }
            cells = out
            contentHeight = y
            scrollTarget = scrollTarget.coerceIn(0f, maxScroll())
        }

        fun revealFocus() {
            val cell = cells.firstOrNull { it.item?.id == focusId } ?: return
            val heading = cells.lastOrNull { it.item == null && it.top < cell.top }
            val top = if (heading != null && cell.top - heading.top <= headStep + 1f) heading.top else cell.top
            if (top - scrollTarget < 0) scrollTarget = top
            if (cell.top + rowStep - scrollTarget > gridBottom - gridTop) scrollTarget = cell.top + rowStep - (gridBottom - gridTop)
            scrollTarget = scrollTarget.coerceIn(0f, maxScroll())
        }

        private fun maxScroll() = max(0f, contentHeight - (gridBottom - gridTop) + 8f)

        override fun onDraw(c: Canvas) {
            c.drawColor(Color.BLACK)
            scale = min(width / 1920f, height / 1080f); offX = (width - 1920f * scale) / 2f; offY = (height - 1080f * scale) / 2f
            c.save(); c.translate(offX, offY); c.scale(scale, scale)
            hits.clear()
            var moving = false
            chrome(c, "background", RectF(0f, 0f, 1920f, 1080f))
            chrome(c, "header", RectF(0f, 40f, 1920f, 164f))
            text(c, "Achievements", 140f, 70f, 62f, 1000f)
            drawTop(c)
            if (abs(scrollTarget - scroll) > 0.5f) { scroll += (scrollTarget - scroll) * 0.25f; moving = true } else scroll = scrollTarget
            moving = drawGrid(c) || moving
            chrome(c, "footer", RectF(0f, 1000f, 1920f, 1080f))
            val hints = when {
                detailOverlay -> listOf("", "", "B  Close")
                else -> listOf(if (dualScreen()) "D-pad  Choose" else "A  Details",
                    "L / R  Filter     ZL / ZR  Game     " + (if (account == null) "Y  Connect" else "Y  Refresh"), "B  Back")
            }
            text(c, hints[0], 140f, 1015f, 38f, 520f)
            text(c, hints[1], 1010f, 1015f, 38f, 900f, true)
            text(c, hints[2], 1780f, 1015f, 38f, 320f, true)
            if (detailOverlay) focused()?.let { drawOverlay(c, it) }
            c.restore()
            if (moving) postInvalidateOnAnimation()
        }

        private fun drawTop(c: Canvas) {
            // Game switcher: the same game identities as the library shelf.
            val left = RectF(120f, 178f, 180f, 244f); val right = RectF(1040f, 178f, 1100f, 244f)
            text(c, "<", left.centerX(), left.top + 8f, 44f, 60f, true)
            text(c, gameTitle(game), 640f, 186f, 42f, 820f, true)
            text(c, ">", right.centerX(), right.top + 8f, 44f, 60f, true)
            hits += left to { showGame(-1) }
            hits += right to { showGame(1) }
            // Connection buttons.
            val widths = if (account == null) listOf(650f) else listOf(170f, 290f, 170f)
            var x = 1150f
            actions().forEachIndexed { k, (label, action) ->
                val box = RectF(x, 180f, x + widths[k], 244f)
                x += widths[k] + 10f
                chrome(c, if (actionFocus == k) "button_selected" else "button", box, if (busy) 140 else 255)
                text(c, label, box.centerX(), box.top + 14f, 34f, box.width() - 28f, true)
                hits += box to { if (!busy) { actionFocus = k; focusId = null; action() } }
            }
            // Filters with counts, and the account / set summary.
            val all = snapshot.items
            val counts = listOf(all.size, all.count { it.earned() }, all.count { !it.earned() })
            FILTERS.forEachIndexed { k, name ->
                val box = RectF(140f + k * 250f, 262f, 370f + k * 250f, 322f)
                chrome(c, if (filter == k) "button_selected" else "button", box)
                text(c, "$name  ${counts[k]}", box.centerX(), box.top + 11f, 36f, box.width() - 36f, true)
                hits += box to { if (filter != k) setFilter(k) }
            }
            val summary = when {
                account == null -> "Not connected"
                snapshot.syncedAt <= 0 -> "$account  /  not loaded yet"
                else -> "$account  /  updated ${date(snapshot.syncedAt)}"
            }
            text(c, summary, 1150f, 276f, 30f, 650f)
        }

        private fun drawGrid(c: Canvas): Boolean {
            val list = visible()
            c.save(); c.clipRect(0f, gridTop - 12f, 1920f, gridBottom)
            var moving = false
            if (list.isEmpty()) {
                val (title, sub) = when {
                    busy && !loaded -> "Reading your progress..." to ""
                    account == null -> "Connect RetroAchievements" to "See ${gameTitle(game)}'s RetroAchievements with everything you have already earned."
                    snapshot.syncedAt <= 0 -> (if (RaLibrary.running() || busy) "Loading from RetroAchievements..." else "Not loaded yet") to
                        "Choose Refresh to load your achievements."
                    !snapshot.hasSets() -> "No RetroAchievements set for ${gameTitle(game)}" to
                        "RetroAchievements has no set for this game yet. WiiDS checks again on each refresh."
                    filter == 1 -> "Nothing earned here yet." to "Achievements you earn on RetroAchievements appear after a refresh."
                    filter == 2 -> "Everything here is earned." to ""
                    else -> "No achievements to show." to ""
                }
                chrome(c, "panel", RectF(300f, 420f, 1620f, 780f))
                badges[badgeKey(AchievementCatalog.placeholder())]?.let { rect.set(890f, 436f, 1030f, 576f); c.drawBitmap(it, null, rect, paint) }
                text(c, title, 960f, 600f, 40f, 1200f, true)
                text(c, sub, 960f, 664f, 28f, 1200f, true)
            }
            for (cell in cells) {
                val y = gridTop + cell.top - scroll
                if (y + rowStep < gridTop - 20f || y > gridBottom + 20f) continue
                val set = cell.set!!
                val item = cell.item
                if (item == null) {
                    text(c, set.label() + "  " + "${set.earned} / ${set.total}", 140f, y + 6f, 36f, 1200f)
                    paint.color = 0x66f2c14e; c.drawRect(140f, y + 50f, 1780f, y + 52f, paint)
                    continue
                }
                val x = 140f + cell.col * 830f
                val box = RectF(x, y, x + 810f, y + 150f)
                val focusedNow = item.id == focusId && actionFocus < 0
                if (focusedNow) {
                    // The gold selection glides between cards.
                    if (!focusShown) { focusRect.set(box); focusShown = true }
                    val k = 0.32f
                    focusRect.set(focusRect.left + (box.left - focusRect.left) * k, focusRect.top + (box.top - focusRect.top) * k,
                        focusRect.right + (box.right - focusRect.right) * k, focusRect.bottom + (box.bottom - focusRect.bottom) * k)
                    if (abs(focusRect.left - box.left) + abs(focusRect.top - box.top) > 1f) moving = true
                }
                chrome(c, "button", box)
                if (focusedNow) chrome(c, "button_selected", RectF(focusRect))
                badges[badgeKey(item)]?.let { rect.set(box.left + 16f, box.top + 9f, box.left + 148f, box.top + 141f); c.drawBitmap(it, null, rect, paint) }
                val tx = box.left + 168f; val tw = box.right - 28f - tx
                val dim = if (item.earned()) 255 else 205
                text(c, item.title, tx, box.top + 14f, 40f, tw, alpha = dim)
                text(c, fit(item.description, 26f, tw), tx, box.top + 66f, 26f, tw, alpha = dim)
                text(c, statusLine(item), tx, box.top + 104f, 25f, tw, alpha = if (item.earned()) 255 else 170)
                hits += box to {
                    if (focusId == item.id && actionFocus < 0) { if (!dualScreen()) { detailOverlay = true; play(ThorLauncherAudio.Sound.SELECT) } }
                    else { focusId = item.id; actionFocus = -1; play(ThorLauncherAudio.Sound.FOCUS) }
                    changed()
                }
            }
            if (actionFocus >= 0 || list.none { it.id == focusId }) focusShown = false
            c.restore()
            // Scroll position marker for long lists.
            val maxS = maxScroll()
            if (maxS > 0f) {
                val h = (gridBottom - gridTop) * (gridBottom - gridTop) / (maxS + gridBottom - gridTop)
                val top = gridTop + (gridBottom - gridTop - h) * (scroll / maxS)
                paint.color = 0x88f2c14e.toInt(); c.drawRoundRect(1884f, top, 1892f, top + h, 4f, 4f, paint)
            }
            if (status.isNotEmpty() || busy) {
                val line = if (busy && status.isEmpty()) "Working..." else status
                rect.set(140f, 940f, 1780f, 992f); paint.color = 0xe60c1118.toInt(); c.drawRoundRect(rect, 22f, 22f, paint)
                text(c, line, 960f, 948f, 30f, 1600f, true)
            }
            return moving
        }

        /** Without a lower screen: the full details over the grid. */
        private fun drawOverlay(c: Canvas, item: AchievementCatalog.Item) {
            paint.color = 0xb3000000.toInt(); c.drawRect(0f, 164f, 1920f, 1000f, paint)
            drawDetail(c, item, RectF(260f, 200f, 1660f, 970f), true)
            hits.clear(); hits += RectF(0f, 0f, 1920f, 1080f) to { detailOverlay = false; invalidate() }
        }

        override fun onTouchEvent(event: MotionEvent): Boolean {
            val x = (event.x - offX) / scale; val y = (event.y - offY) / scale
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> { downX = x; downY = y; dragging = false; dragStart = scrollTarget }
                MotionEvent.ACTION_MOVE -> if (!detailOverlay && (dragging || abs(y - downY) > 24f) && downY in gridTop..gridBottom) {
                    dragging = true; scrollTarget = (dragStart - (y - downY)).coerceIn(0f, maxScroll()); scroll = scrollTarget; invalidate()
                }
                MotionEvent.ACTION_UP -> if (!dragging) hits.lastOrNull { it.first.contains(x, y) && it.first.contains(downX, downY) }?.second?.invoke()
            }
            return true
        }
    }

    /** One achievement in full: picture, name, state, date, set, author, points and the whole description. */
    private fun drawDetail(c: Canvas, item: AchievementCatalog.Item, box: RectF, upper: Boolean) {
        chrome(c, "panel", box)
        val inner = RectF(box.left + 28f, box.top + 28f, box.right - 28f, box.bottom - 28f)
        paint.color = 0xe6131a25.toInt(); c.drawRoundRect(inner, 20f, 20f, paint)
        val side = min(300f, inner.height() * 0.5f)
        val art = RectF(inner.left + 30f, inner.top + 30f, inner.left + 30f + side, inner.top + 30f + side)
        if (item.earned()) {
            paint.shader = RadialGradient(art.centerX(), art.centerY(), side * 0.75f, intArrayOf(0x66f2c14e, 0x00000000), null, Shader.TileMode.CLAMP)
            c.drawCircle(art.centerX(), art.centerY(), side * 0.75f, paint); paint.shader = null
        }
        badges[badgeKey(item)]?.let { c.drawBitmap(it, null, art, paint) }
        val x = art.right + 36f; val w = inner.right - 30f - x
        var y = inner.top + 30f
        AchievementText.wrap(ui.text, item.title, 50f, w).take(2).forEach { text(c, it, x, y, 50f, w); y += 58f }
        y += 8f
        paint.color = if (item.earned()) 0xfff2c14e.toInt() else 0xff8e979f.toInt()
        c.drawCircle(x + 11f, y + 19f, 11f, paint)
        text(c, (if (item.earned()) "Earned" else "Locked") + "  /  ${item.raPoints} points", x + 34f, y, 36f, w - 34f)
        y += 52f
        val origin = when {
            item.earned() && item.hardcore() -> "Earned in Hardcore on ${item.raHardcoreDate.take(10)} (UTC)"
            item.earned() -> "Earned on ${item.raCasualDate.take(10)} (UTC)"
            else -> "Not yet earned on RetroAchievements"
        }
        AchievementText.wrap(ui.text, origin, 28f, w).forEach { text(c, it, x, y, 28f, w); y += 36f }
        // The full description below the picture, shrinking (never truncating) to fit.
        val descTop = max(art.bottom + 34f, y + 18f)
        val descWidth = inner.width() - 60f
        val provenance = listOf("${item.setTitle}  /  by ${item.raAuthor.ifEmpty { "an unknown author" }}",
            "From RetroAchievements. WiiDS shows your RA progress; it never submits unlocks or adds RA points.")
        val room = inner.bottom - 24f - descTop
        var size = 34f
        var lines: List<String>
        while (true) {
            lines = AchievementText.wrap(ui.text, item.description, size, descWidth)
            val needed = lines.size * size * 1.25f + provenance.size * size * 0.82f * 1.3f + 24f
            if (needed <= room || size <= 18f) break
            size -= 2f
        }
        y = descTop
        lines.forEach { text(c, it, inner.left + 30f, y, size, descWidth); y += size * 1.25f }
        y += 24f
        provenance.forEach { line ->
            AchievementText.wrap(ui.text, line, size * 0.82f, descWidth).forEach { text(c, it, inner.left + 30f, y, size * 0.82f, descWidth, alpha = 215); y += size * 0.82f * 1.3f }
        }
        if (upper) text(c, "B  Close", box.right - 120f, box.bottom - 70f, 32f, 200f, true)
    }

    /** The lower screen: the focused achievement in full, or the game's RetroAchievements summary. */
    private inner class DetailPresentation(display: Display) : Presentation(this@AchievementsActivity, display) {
        lateinit var view: View
        override fun onCreate(savedInstanceState: Bundle?) {
            super.onCreate(savedInstanceState)
            view = object : View(context) {
                override fun onDraw(c: Canvas) {
                    c.drawColor(Color.BLACK)
                    val s = min(width / 1280f, height / 1080f)
                    c.save(); c.translate((width - 1280f * s) / 2f, (height - 1080f * s) / 2f); c.scale(s, s)
                    chrome(c, "background", RectF(0f, 0f, 1280f, 1080f))
                    chrome(c, "header", RectF(0f, 60f, 1280f, 230f))
                    val item = focused()
                    if (item != null && actionFocus < 0) {
                        text(c, "RetroAchievements", 640f, 112f, 60f, 1100f, true)
                        drawDetail(c, item, RectF(54f, 262f, 1226f, 960f), false)
                    } else {
                        text(c, gameTitle(game), 640f, 112f, 60f, 1100f, true)
                        drawSummary(c)
                    }
                    chrome(c, "footer", RectF(0f, 996f, 1280f, 1080f))
                    c.restore()
                }
            }
            setContentView(view)
            window?.apply {
                addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON or WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE)
                @Suppress("DEPRECATION")
                decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT)
            }
        }
    }

    /** Connection state and this game's sets with their earned counts. */
    private fun drawSummary(c: Canvas) {
        chrome(c, "panel", RectF(54f, 270f, 1226f, 900f))
        paint.color = 0xe6131a25.toInt(); rect.set(82f, 298f, 1198f, 872f); c.drawRoundRect(rect, 20f, 20f, paint)
        val lines = ArrayList<Pair<String, Float>>()
        if (account == null) {
            lines += "Bring in your RetroAchievements" to 40f
            lines += "WiiDS shows each game's RetroAchievements sets, with everything you have already earned marked complete." to 30f
            lines += "You need your RA username and the Web API key from your RA account control panel. No password is used; the key is encrypted on this device." to 30f
            lines += "Unlocks earned in WiiDS cannot be sent to RetroAchievements. Your progress comes from your RA account." to 30f
        } else {
            lines += "Connected as $account" to 40f
            if (snapshot.syncedAt <= 0) lines += "Not loaded yet. Choose Refresh." to 30f
            else {
                if (snapshot.sets.isEmpty()) lines += "RetroAchievements has no set for this game yet." to 30f
                for (set in snapshot.sets) lines += "${set.label()}:  ${set.earned} / ${set.total} earned" to 32f
                lines += "Updated ${DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(snapshot.syncedAt))}. Refreshes daily when you open WiiDS, or press Y." to 28f
            }
        }
        var y = 330f
        for ((line, size) in lines) {
            AchievementText.wrap(ui.text, line, size, 1040f).forEach { text(c, it, 110f, y, size, 1060f); y += size * 1.35f }
            y += 14f
        }
    }
}
