package com.driftdroid.android

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.graphics.drawable.Drawable
import android.os.SystemClock
import android.text.format.DateFormat
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min

/**
 * The "Wii Home Menu" theme of the library (ThorMenuTheme.Style.WII_CHANNELS): the games as channels in
 * the Wii Menu's 4 x 3 grid, pages turned with the side arrows, and the Wii Menu's bottom bar with its
 * own buttons doing WiiDS jobs:
 *  - the Wii button opens the WiiDS settings (every Options entry, as Wii Settings-style buttons);
 *  - the SD Card button opens the Save manager;
 *  - the clock and date are the device's own;
 *  - the envelope (Wii Message Board) opens Achievements.
 *
 * The lower display follows the highlighted title with its own channel preview and launch controls.
 * A opens the channel from the grid; A again or the lower Start button uses the case shelf launch path.
 */
internal class WiiChannelMenuView(
    context: Context,
    private val actions: List<ThorLauncherView.Entry>,
    private val caseCount: Int,
    private val onSelectionChanged: (product: Int?, options: Boolean) -> Unit,
    private val onPhaseChanged: (phase: ThorLauncherView.Phase, product: Int?, message: String) -> Unit,
    private val preflight: (product: Int) -> String?,
    private val thorMap: File?,
    private val onArtworkChanged: () -> Unit,
) : View(context), LibraryLauncher {
    private enum class State { SPLASH, BROWSE, OPEN, LAUNCHING, OPTIONS }
    private enum class Bar { WII, SD, MAIL }

    private var state = if (splashShown) State.BROWSE else State.SPLASH
    /** order[slot] = case index; the player can rearrange channels (Start on a channel). */
    private val order: IntArray = loadOrder()
    private fun ord(slot: Int) = if (slot in order.indices) order[slot] else slot
    private fun slotOf(case: Int) = order.indexOf(case).takeIf { it >= 0 } ?: case
    private var selection = slotOf(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY_LAST, 0).coerceIn(0, max(0, caseCount - 1)))
    /** The slot a channel was picked up from while moving it, or -1. */
    private var movingFrom = -1
    /** Non-null while the bottom bar has the focus instead of a channel. */
    private var bar: Bar? = null
    private var page = selection / PER_PAGE
    private var pageShown = page.toFloat()
    private var optionSelection = 0
    private var optionPage = 0
    private var notice = ""
    private var noticeAt = 0L
    private var stateAt = SystemClock.uptimeMillis()
    private var lastFrame = 0L
    private var lastAxis = 0L
    private var committed = false
    private var hostResumed = true

    private val pages = max(1, ceil(caseCount / PER_PAGE.toFloat()).toInt())
    private val options = actions.drop(caseCount)
    private val optionPages = max(1, ceil(options.size / OPTIONS_PER_PAGE.toFloat()).toInt())

    private val channelArtwork = WiiChannelArtwork()
    private var channelArt: List<WiiChannelArtwork.Art> = emptyList()
    private var artworkDirty = true
    private var artworkLoading = false
    private var focusedTile = -1
    private var focusedSince = 0L

    private fun loadArt() {
        if (!artworkDirty || artworkLoading) return
        artworkDirty = false
        artworkLoading = true
        Thread({
            val loaded = channelArtwork.load(context.applicationContext, actions.take(caseCount))
            post {
                updateArtAnimations(play = false)
                channelArt = loaded
                artworkLoading = false
                invalidate()
                onArtworkChanged()
            }
        }, "WiiDsChannelArtwork").start()
    }

    fun drawChannelPreview(canvas: Canvas, bounds: RectF, product: Int, loopMillis: Long) {
        val entry = actions.getOrNull(product) ?: return
        channelArtwork.draw(canvas, bounds, entry, channelArt.getOrNull(product),
            ready = true, alpha = 255, motion = 1f, loopMillis = loopMillis, preview = true)
    }

    private fun updateArtAnimations(play: Boolean): Boolean {
        val highlighted = if (play && bar == null && selection in 0 until caseCount) {
            channelArt.getOrNull(ord(selection))?.animation
        } else null

        channelArt.forEach { artwork ->
            val animation = artwork.animation ?: return@forEach
            if (animation !== highlighted && animation.callback === this) {
                animation.stop()
                unscheduleDrawable(animation)
                animation.callback = null
            }
        }
        if (highlighted != null && highlighted.callback !== this) {
            highlighted.callback = this
            highlighted.start()
        }
        return highlighted != null
    }

    override fun verifyDrawable(who: Drawable): Boolean =
        super.verifyDrawable(who) || channelArt.any { it.animation === who }

    override fun invalidateDrawable(drawable: Drawable) {
        if (channelArt.any { it.animation === drawable }) postInvalidateOnAnimation()
        else super.invalidateDrawable(drawable)
    }

    override fun onDetachedFromWindow() {
        updateArtAnimations(play = false)
        super.onDetachedFromWindow()
    }

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility != VISIBLE) updateArtAnimations(play = false)
    }

    // ------------------------------------------------------------- paints
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val type = Paint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG).apply {
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    }
    private val clockType = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create("sans-serif-light", Typeface.NORMAL); textAlign = Paint.Align.CENTER
    }
    private var displayedMinute = Long.MIN_VALUE
    private var clockTime = ""
    private var clockPeriod = ""
    private var clockDate = ""
    private val path = Path()
    private val shelfEdge = Path()
    private val box = RectF()
    private val tileBounds = RectF()
    private var scale = 1f
    private var left = 0f
    private var top = 0f
    private val hits = ArrayList<Pair<RectF, Int>>()   // design-space rectangles: channels >= 0, bar -1..-3, arrows -10/-11, options 1000+

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        requestFocus()
    }

    // ============================================================== LibraryLauncher
    override fun redraw() = invalidate()

    override fun refreshSelection() {
        publishSelection()
        publishPhase()
    }

    override fun onHostResumed() {
        hostResumed = true
        if (state == State.LAUNCHING) setState(State.OPEN)   // back from the game: its channel is still selected
        committed = false
        artworkDirty = true
        displayedMinute = Long.MIN_VALUE
        thorMap?.let { ThorLauncherAudio.show(context, it) }
        lastFrame = 0L
        publishSelection(); publishPhase()
        invalidate()
    }

    override fun onHostPaused() {
        hostResumed = false
        updateArtAnimations(play = false)
        if (!committed) ThorLauncherAudio.hide()
    }

    override val launchCommitted: Boolean get() = committed

    override fun launchFailed(message: String) {
        sound(WiiMenuSounds.Cue.REJECT)
        committed = false
        if (state == State.LAUNCHING) {
            thorMap?.let { ThorLauncherAudio.show(context, it) }
            setState(State.OPEN)
        }
        setNotice(message)
    }

    // ============================================================== actions (also from the lower screen)
    /** The selected channel is already visible on the lower display while browsing. */
    fun lowerButton(name: String) {
        val onGame = state == State.BROWSE && bar == null && movingFrom < 0 && selection in 0 until caseCount
        when (name) {
            "start" -> {
                if (onGame) openChannel(selection)
                if (state == State.OPEN) start()
            }
            "menu" -> if (state == State.OPEN) closeChannel() else if (onGame) requestFocus()
            "prev" -> if (state == State.OPEN || onGame) stepChannel(-1)
            "next" -> if (state == State.OPEN || onGame) stepChannel(1)
        }
    }

    private fun activate() {
        when (state) {
            State.OPTIONS -> {
                val entry = options.getOrNull(optionSelection) ?: return
                UiSounds.playClick(context)
                if (entry.enabled()) entry.action()
                invalidate()
            }
            State.BROWSE -> when (bar) {
                Bar.WII -> openOptions()
                Bar.SD -> runOption("Save manager")
                Bar.MAIL -> runOption("Achievements")
                null -> if (selection < caseCount) openChannel(selection)
            }
            State.OPEN -> start()
            State.LAUNCHING, State.SPLASH -> {}
        }
    }

    private fun runOption(label: String) {
        val entry = options.firstOrNull { it.label == label } ?: return
        sound(WiiMenuSounds.Cue.SELECT)
        if (entry.enabled()) entry.action()
    }

    private fun openChannel(i: Int) {
        if (!actions[ord(i)].enabled()) {
            // A game that is not set up keeps its guided setup flow (Your games), as on the case shelf.
            sound(WiiMenuSounds.Cue.SELECT)
            actions[ord(i)].action(); invalidate(); return
        }
        selection = i; bar = null
        remember(i)
        sound(WiiMenuSounds.Cue.SELECT)
        setState(State.OPEN)
    }

    private fun closeChannel() {
        sound(WiiMenuSounds.Cue.BACK)
        notice = ""
        setState(State.BROWSE)
    }

    /** The one place a game's launch action runs: after the Wii Menu's fade to white. */
    private fun start() {
        val i = selection
        if (state != State.OPEN || i !in 0 until caseCount) return
        val blocked = preflight(ord(i))
        if (blocked != null) {
            sound(WiiMenuSounds.Cue.REJECT)
            setNotice(blocked)
            return
        }
        sound(WiiMenuSounds.Cue.LAUNCH)
        ThorLauncherAudio.fadeForLaunch(FADE_MS)
        setState(State.LAUNCHING)
        postDelayed({
            if (state != State.LAUNCHING || committed) return@postDelayed
            committed = true
            ThorLauncherAudio.hide()
            actions[ord(i)].action()
        }, FADE_MS)
    }

    private fun openOptions() {
        sound(WiiMenuSounds.Cue.SELECT)
        optionSelection = 0; optionPage = 0
        setState(State.OPTIONS)
        publishSelection()
    }

    fun goBack(): Boolean = when (state) {
        State.OPTIONS -> { sound(WiiMenuSounds.Cue.BACK); setState(State.BROWSE); bar = Bar.WII; publishSelection(); true }
        State.OPEN -> { closeChannel(); true }
        State.LAUNCHING, State.SPLASH -> true
        State.BROWSE -> if (bar != null) { bar = null; publishSelection(); invalidate(); true } else false
    }

    private fun move(keyCode: Int) {
        when (state) {
            State.OPTIONS -> moveOption(keyCode)
            State.BROWSE -> moveGrid(keyCode)
            State.OPEN -> when (keyCode) {
                // Like the Wii's channel screen: the side arrows step to the neighbouring channel.
                KeyEvent.KEYCODE_DPAD_LEFT -> stepChannel(-1)
                KeyEvent.KEYCODE_DPAD_RIGHT -> stepChannel(1)
            }
            State.LAUNCHING, State.SPLASH -> {}
        }
    }

    private fun leaveSplash() {
        splashShown = true
        sound(WiiMenuSounds.Cue.SELECT)
        setState(State.BROWSE)
        publishSelection()
    }

    private fun stepChannel(delta: Int) {
        var next = selection
        for (k in 0 until caseCount) {
            next = (next + delta + caseCount) % caseCount
            if (actions[ord(next)].enabled()) break
        }
        if (next == selection) return
        selection = next; page = next / PER_PAGE
        remember(next)
        sound(WiiMenuSounds.Cue.FOCUS)
        publishSelection(); publishPhase(); invalidate()
    }

    private fun moveGrid(keyCode: Int) {
        val current = bar
        if (current != null) {
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_UP -> { bar = null; selection = min(caseCount - 1, page * PER_PAGE + 2 * COLUMNS + if (current == Bar.MAIL) COLUMNS - 1 else 0) }
                KeyEvent.KEYCODE_DPAD_LEFT -> bar = Bar.values()[max(0, current.ordinal - 1)]
                KeyEvent.KEYCODE_DPAD_RIGHT -> bar = Bar.values()[min(Bar.values().size - 1, current.ordinal + 1)]
                else -> return
            }
            sound(WiiMenuSounds.Cue.FOCUS)
            publishSelection(); invalidate(); return
        }
        val inPage = selection - page * PER_PAGE
        val col = inPage % COLUMNS; val row = inPage / COLUMNS
        var next = selection
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> next = if (col > 0) selection - 1 else if (page > 0) (page - 1) * PER_PAGE + row * COLUMNS + COLUMNS - 1 else selection
            KeyEvent.KEYCODE_DPAD_RIGHT -> next = if (col < COLUMNS - 1 && selection + 1 < caseCount) selection + 1
                else if (page < pages - 1) min(caseCount - 1, (page + 1) * PER_PAGE + row * COLUMNS) else selection
            KeyEvent.KEYCODE_DPAD_UP -> next = if (row > 0) selection - COLUMNS else selection
            KeyEvent.KEYCODE_DPAD_DOWN -> {
                if (row < ROWS - 1 && selection + COLUMNS < caseCount) next = selection + COLUMNS
                else { bar = if (col >= COLUMNS / 2) Bar.MAIL else Bar.WII; sound(WiiMenuSounds.Cue.FOCUS); publishSelection(); invalidate(); return }
            }
        }
        if (next == selection) return
        selection = next; page = next / PER_PAGE
        sound(WiiMenuSounds.Cue.FOCUS)
        publishSelection(); invalidate()
    }

    private fun moveOption(keyCode: Int) {
        var next = optionSelection
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> next = if (optionSelection % 2 == 1) optionSelection - 1 else optionSelection - OPTIONS_PER_PAGE + 1
            KeyEvent.KEYCODE_DPAD_RIGHT -> next = if (optionSelection % 2 == 0) optionSelection + 1 else optionSelection + OPTIONS_PER_PAGE - 1
            KeyEvent.KEYCODE_DPAD_UP -> next = optionSelection - 2
            KeyEvent.KEYCODE_DPAD_DOWN -> next = optionSelection + 2
        }
        if (next !in options.indices || next == optionSelection) return
        optionSelection = next; optionPage = next / OPTIONS_PER_PAGE
        sound(WiiMenuSounds.Cue.FOCUS)
        publishSelection(); invalidate()
    }

    private fun setState(next: State) {
        state = next
        stateAt = SystemClock.uptimeMillis()
        publishPhase()
        invalidate()
    }

    private fun setNotice(message: String) {
        notice = message; noticeAt = SystemClock.uptimeMillis()
        publishPhase(); invalidate()
        postDelayed({ publishPhase(); invalidate() }, NOTICE_MS + 50)
    }

    private fun noticeActive() = notice.isNotEmpty() && SystemClock.uptimeMillis() - noticeAt < NOTICE_MS

    private fun publishSelection() {
        if (state == State.OPTIONS) onSelectionChanged(null, true)
        else onSelectionChanged(if (bar == null && state != State.SPLASH) selection.takeIf { it < caseCount }?.let { ord(it) } else null, bar != null)
    }

    private fun publishPhase() {
        val phase = when {
            state == State.OPTIONS -> ThorLauncherView.Phase.OPTIONS
            state == State.LAUNCHING -> ThorLauncherView.Phase.LAUNCHING
            noticeActive() -> ThorLauncherView.Phase.BLOCKED
            state == State.OPEN -> ThorLauncherView.Phase.OPEN
            else -> ThorLauncherView.Phase.BROWSE
        }
        onPhaseChanged(phase, selection.takeIf { it < caseCount && bar == null && state != State.SPLASH }?.let { ord(it) }, if (noticeActive()) notice else "")
    }

    private fun remember(i: Int) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt(KEY_LAST, ord(i)).apply()
    }

    private fun loadOrder(): IntArray {
        val saved = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_ORDER, null)
        val parsed = saved?.split(',')?.mapNotNull { it.toIntOrNull() }?.filter { it in 0 until caseCount }?.distinct() ?: emptyList()
        // Games added since the order was saved go to the end, in shelf order.
        return (parsed + (0 until caseCount).filter { it !in parsed }).toIntArray()
    }

    private fun saveOrder() {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_ORDER, order.joinToString(",")).apply()
    }

    /** Start on a channel picks it up; the D-pad carries it, A or Start puts it down, B puts it back. */
    private fun pickUp() {
        if (state != State.BROWSE || bar != null || selection >= caseCount) return
        movingFrom = selection
        sound(WiiMenuSounds.Cue.PICK)
        setNotice("Move the channel with the D-pad, then press A.")
        invalidate()
    }

    private fun carry(keyCode: Int) {
        val inPage = selection - page * PER_PAGE
        val col = inPage % COLUMNS; val row = inPage / COLUMNS
        val next = when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> if (col > 0 || page > 0) selection - 1 else selection
            KeyEvent.KEYCODE_DPAD_RIGHT -> selection + 1
            KeyEvent.KEYCODE_DPAD_UP -> if (row > 0) selection - COLUMNS else selection
            KeyEvent.KEYCODE_DPAD_DOWN -> if (row < ROWS - 1) selection + COLUMNS else selection
            else -> selection
        }
        if (next == selection || next !in 0 until caseCount) return
        val moved = order[selection]; order[selection] = order[next]; order[next] = moved
        selection = next; page = next / PER_PAGE
        sound(WiiMenuSounds.Cue.FOCUS)
        invalidate()
    }

    private fun putDown(keep: Boolean) {
        if (movingFrom < 0) return
        if (!keep) {
            // Back to where it was: undo by moving it step by step to its first slot.
            val moved = order[selection]
            val list = order.toMutableList(); list.removeAt(selection); list.add(movingFrom, moved)
            list.forEachIndexed { k, v -> order[k] = v }
            selection = movingFrom; page = selection / PER_PAGE
        }
        movingFrom = -1
        saveOrder(); remember(selection)
        notice = ""
        sound(if (keep) WiiMenuSounds.Cue.DROP else WiiMenuSounds.Cue.BACK)
        publishSelection(); publishPhase(); invalidate()
    }

    // ============================================================== input
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (state == State.SPLASH) {
            if (event.repeatCount == 0 && (keyCode == KeyEvent.KEYCODE_BUTTON_A || keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                    keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_BUTTON_START)) leaveSplash()
            return true
        }
        if (movingFrom >= 0) {
            when (keyCode) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT ->
                    if (event.repeatCount == 0 || event.repeatCount % 4 == 0) carry(keyCode)
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_DPAD_CENTER -> if (event.repeatCount == 0) putDown(true)
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> if (event.repeatCount == 0) putDown(false)
            }
            return true
        }
        when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_START -> if (event.repeatCount == 0) pickUp()
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT ->
                if (event.repeatCount == 0 || event.repeatCount % 4 == 0) move(keyCode)
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> if (event.repeatCount == 0) activate()
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> if (event.repeatCount == 0) goBack()
            // X: the Wii button (settings), as the shelf's X opens Options. Y: the Message Board (achievements).
            KeyEvent.KEYCODE_BUTTON_X -> if (event.repeatCount == 0 && state == State.BROWSE) openOptions()
            KeyEvent.KEYCODE_BUTTON_Y -> if (event.repeatCount == 0 && state == State.BROWSE) runOption("Achievements")
            KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1 -> if (event.repeatCount == 0) {
                val direction = if (keyCode == KeyEvent.KEYCODE_BUTTON_L1) -1 else 1
                if (state == State.OPEN) stepChannel(direction)
                else if (state == State.BROWSE && page + direction in 0 until pages) {
                    page += direction
                    selection = page * PER_PAGE
                    bar = null
                    sound(WiiMenuSounds.Cue.PAGE)
                    publishSelection()
                    invalidate()
                }
            }
            else -> return super.onKeyDown(keyCode, event)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BACK -> true
        else -> super.onKeyUp(keyCode, event)
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        val x = event.getAxisValue(MotionEvent.AXIS_HAT_X).takeIf { abs(it) > 0.5f } ?: event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_HAT_Y).takeIf { abs(it) > 0.5f } ?: event.getAxisValue(MotionEvent.AXIS_Y)
        if (abs(x) < 0.5f && abs(y) < 0.5f) { lastAxis = 0; return true }
        val now = SystemClock.uptimeMillis()
        if (lastAxis == 0L || now - lastAxis > 260L) {
            move(if (abs(y) >= abs(x)) { if (y < 0) KeyEvent.KEYCODE_DPAD_UP else KeyEvent.KEYCODE_DPAD_DOWN }
                 else if (x < 0) KeyEvent.KEYCODE_DPAD_LEFT else KeyEvent.KEYCODE_DPAD_RIGHT)
            lastAxis = now
        }
        return true
    }

    private var pressedHit: Int? = null
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (state == State.LAUNCHING) return true
        if (state == State.SPLASH) { if (event.actionMasked == MotionEvent.ACTION_UP) leaveSplash(); return true }
        if (movingFrom >= 0) { if (event.actionMasked == MotionEvent.ACTION_UP) putDown(true); return true }
        val x = (event.x - left) / scale; val y = (event.y - top) / scale
        val hit = hits.lastOrNull { it.first.contains(x, y) }?.second
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> pressedHit = hit
            MotionEvent.ACTION_UP -> {
                val target = pressedHit; pressedHit = null
                if (target != null && target == hit) tapped(target)
            }
            MotionEvent.ACTION_CANCEL -> pressedHit = null
        }
        invalidate()
        return true
    }

    private fun tapped(target: Int) {
        when {
            target >= 1000 -> {   // an option button: first tap selects, a tap on the selected one runs it
                val i = target - 1000
                if (i == optionSelection) activate() else { optionSelection = i; sound(WiiMenuSounds.Cue.FOCUS); publishSelection() }
            }
            target == -10 -> if (state == State.BROWSE && page > 0) { page--; selection = page * PER_PAGE; bar = null; publishSelection() }
            target == -11 -> if (state == State.BROWSE && page < pages - 1) { page++; selection = page * PER_PAGE; bar = null; publishSelection() }
            target == -20 -> goBack()
            target < 0 -> { bar = Bar.values()[-target - 1]; if (state == State.OPEN) setState(State.BROWSE); activate() }
            state == State.OPEN && target == selection -> start()
            state == State.OPEN -> { selection = target; remember(target); sound(WiiMenuSounds.Cue.FOCUS); publishSelection(); publishPhase() }
            target == selection && bar == null -> openChannel(target)
            else -> { selection = target; bar = null; sound(WiiMenuSounds.Cue.FOCUS); publishSelection() }
        }
    }

    override fun onWindowFocusChanged(hasWindowFocus: Boolean) {
        super.onWindowFocusChanged(hasWindowFocus)
        if (hasWindowFocus && !isFocused) requestFocus()
        lastFrame = 0L
        invalidate()
    }

    // ============================================================== drawing (1920 x 1080 design space)
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        loadArt()
        val now = SystemClock.uptimeMillis()
        val dt = if (lastFrame == 0L) 0f else min(0.05f, (now - lastFrame) / 1000f)
        lastFrame = now
        scale = min(width / 1920f, height / 1080f)
        left = (width - 1920f * scale) / 2f; top = (height - 1080f * scale) / 2f
        canvas.drawColor(ThorMenuTheme.tone(Color.BLACK, ThorMenuTheme.surfaceBottom))
        canvas.save()
        canvas.translate(left, top); canvas.scale(scale, scale)
        hits.clear()
        box.set(0f, 0f, 1920f, 1080f)
        ThorMenuTheme.chrome(canvas, "background", box)

        // Page turning slides the grid sideways.
        val target = page.toFloat()
        val motion = com.driftdroid.android.dualscreen.WiiDsSkin.motion()
        val playChannelMotion = motion && hostResumed && isShown && windowVisibility == VISIBLE &&
            state == State.BROWSE && bar == null
        val playsArt = updateArtAnimations(playChannelMotion)
        pageShown = if (motion) pageShown + (target - pageShown) * min(1f, dt * 12f) else target
        if (abs(target - pageShown) < 0.002f) pageShown = target
        val animating = pageShown != target
        val focus = if (playChannelMotion) selection else -1
        if (focus != focusedTile) {
            focusedTile = focus
            focusedSince = now
        }
        val focusElapsed = if (focus >= 0) now - focusedSince else 0L
        val focusProgress = if (focus >= 0) (focusElapsed / 560f).coerceIn(0f, 1f) else 1f
        val focusedProduct = if (focus in 0 until caseCount) ord(focus) else -1
        val kartAnimating = channelArt.getOrNull(focusedProduct)?.kart != null && focusProgress < 1f
        val loopAnimating = !playsArt && focusedProduct >= 0 && WiiChannelArtwork.hasChannelLoop(actions[focusedProduct].art)

        if (state == State.SPLASH) drawSplash(canvas, now)
        else if (state == State.OPTIONS) drawOptions(canvas)
        else {
            drawGrid(canvas, focusProgress, if (loopAnimating) focusElapsed else 0L)
            drawBar(canvas)
            if (!noticeActive()) drawGridHeading(canvas)
        }
        if (noticeActive()) drawNotice(canvas)

        // Starting a game: the Wii Menu's fade to white.
        if (state == State.LAUNCHING) {
            val t = ((now - stateAt) / FADE_MS.toFloat()).coerceIn(0f, 1f)
            fill.shader = null; fill.color = withAlpha(ThorMenuTheme.tone(Color.WHITE, ThorMenuTheme.surfaceBottom), (255 * t).toInt())
            canvas.drawRect(0f, 0f, 1920f, 1080f, fill)
        }
        canvas.restore()
        if (animating || kartAnimating || loopAnimating || state == State.SPLASH || state == State.LAUNCHING) postInvalidateOnAnimation()
        else if (!playsArt) postInvalidateDelayed(1000L)   // the clock
    }

    private fun tileRect(slot: Int, pageOffset: Float, out: RectF) {
        val col = slot % COLUMNS; val row = slot / COLUMNS
        val x = GRID_LEFT + col * (TILE_W + GAP_X) + pageOffset * PAGE_SHIFT
        val y = GRID_TOP + row * (TILE_H + GAP_Y)
        out.set(x, y, x + TILE_W, y + TILE_H)
    }

    private fun drawGridHeading(c: Canvas) {
        text(c, "WiiDS", GRID_LEFT, 19f, 62f, 220f, false, ThorMenuTheme.accentText)
        fill.color = ThorMenuTheme.tone(0xffa9bbc3.toInt(), ThorMenuTheme.border)
        c.drawRect(GRID_LEFT + 168f, 36f, GRID_LEFT + 171f, 69f, fill)
        text(c, "Channels", GRID_LEFT + 194f, 28f, 47f, 360f, false, ThorMenuTheme.TEXT)
        if (bar == null && selection in 0 until caseCount) {
            text(c, actions[ord(selection)].label, 1000f, 28f, 43f, 860f, true, ThorMenuTheme.TEXT)
        }
        if (pages > 1) text(c, "${page + 1} / $pages", 1730f, 28f, 45f, 120f, true, ThorMenuTheme.TEXT)
    }

    private fun drawGrid(c: Canvas, focusProgress: Float, loopMillis: Long) {
        val dim = state == State.OPEN
        val first = kotlin.math.floor(pageShown).toInt()
        val last = kotlin.math.ceil(pageShown).toInt()
        c.save()
        c.clipRect(GRID_LEFT - 18f, GRID_TOP - 32f, 1920f - GRID_LEFT + 18f, 786f)
        for (p in first..last) {
            for (slot in 0 until PER_PAGE) {
                val index = p * PER_PAGE + slot
                tileRect(slot, p - pageShown, tileBounds)
                if (index < caseCount) {
                    val focused = index == selection && bar == null && p == page
                    if (index == selection && movingFrom >= 0) tileBounds.offset(0f, -18f)
                    drawChannel(c, ord(index), RectF(tileBounds), focused, index == pressedHit,
                        if (dim && !focused) 145 else 255, if (focused) focusProgress else 1f,
                        if (focused) loopMillis else 0L)
                    if (p == page) {
                        val hit = RectF(tileBounds)
                        if (hit.intersect(GRID_LEFT - 18f, GRID_TOP - 32f, 1920f - GRID_LEFT + 18f, 786f)) {
                            hits.add(hit to index)
                        }
                    }
                } else drawEmptyTile(c, tileBounds)
            }
        }
        c.restore()
        val arrowY = GRID_TOP + TILE_H * 1.5f + GAP_Y
        if (page < pages - 1) drawArrow(c, 1838f, arrowY, right = true, id = -11)
        if (page > 0) drawArrow(c, 82f, arrowY, right = false, id = -10)
    }

    private fun drawEmptyTile(c: Canvas, r: RectF) {
        fill.color = 0xffffffff.toInt()
        fill.shader = LinearGradient(0f, r.top, 0f, r.bottom, 
            ThorMenuTheme.tone(0xfff3f4f6.toInt(), ThorMenuTheme.panel),
            ThorMenuTheme.tone(0xffe4e7ea.toInt(), ThorMenuTheme.surfaceBottom), Shader.TileMode.CLAMP)
        c.drawRoundRect(r, RADIUS, RADIUS, fill); fill.shader = null
        line.color = ThorMenuTheme.tone(0xffcdd2d7.toInt(), ThorMenuTheme.border); line.strokeWidth = 3f
        c.drawRoundRect(r, RADIUS, RADIUS, line)
    }

    private fun drawChannel(c: Canvas, i: Int, r: RectF, focused: Boolean, pressed: Boolean,
                            alpha: Int, motion: Float, loopMillis: Long) {
        if (pressed) r.inset(5f, 3f)
        else if (focused) r.inset(-7f, -5f)

        box.set(r)
        box.offset(0f, 5f)
        fill.color = withAlpha(ThorMenuTheme.tone(0x280e2835, 0x33000000), alpha)
        c.drawRoundRect(box, RADIUS, RADIUS, fill)
        fill.color = withAlpha(ThorMenuTheme.tone(
            if (focused) 0xffeefaff.toInt() else 0xfff9fcfd.toInt(), ThorMenuTheme.panel), alpha)
        c.drawRoundRect(r, RADIUS, RADIUS, fill)

        box.set(r)
        val artwork = channelArt.getOrNull(i)
        box.inset(3f, 3f)
        val entry = actions[i]
        channelArtwork.draw(c, box, entry, artwork, entry.enabled(), alpha, motion, loopMillis)

        line.color = if (focused) ThorMenuTheme.tone(0xff36a4ce.toInt(), ThorMenuTheme.accent)
            else withAlpha(ThorMenuTheme.tone(0xffb5c4cb.toInt(), ThorMenuTheme.border), alpha)
        line.strokeWidth = if (focused) 6f else 3f
        c.drawRoundRect(r, RADIUS, RADIUS, line)
    }

    private fun drawArrow(c: Canvas, x: Float, y: Float, right: Boolean, id: Int) {
        val direction = if (right) 1f else -1f
        box.set(x - 39f, y - 65f, x + 39f, y + 65f)
        fill.color = if (pressedHit == id) ThorMenuTheme.tone(0xffd5edf7.toInt(), ThorMenuTheme.buttonTop)
            else ThorMenuTheme.tone(0xfff8fcfd.toInt(), ThorMenuTheme.panel)
        c.drawRoundRect(box, 39f, 39f, fill)
        line.color = ThorMenuTheme.tone(0xffa4c4d2.toInt(), ThorMenuTheme.border)
        line.strokeWidth = 3f
        c.drawRoundRect(box, 39f, 39f, line)
        path.reset()
        path.moveTo(x - 8f * direction, y - 24f)
        path.lineTo(x + 12f * direction, y)
        path.lineTo(x - 8f * direction, y + 24f)
        line.color = ThorMenuTheme.tone(0xff2389b1.toInt(), ThorMenuTheme.accentText)
        line.strokeWidth = 8f
        line.strokeCap = Paint.Cap.ROUND
        c.drawPath(path, line)
        line.strokeCap = Paint.Cap.BUTT
        hits.add(RectF(x - 56f, y - 90f, x + 56f, y + 90f) to id)
    }

    private fun drawBar(c: Canvas) {
        // A low curved shelf groups the three WiiDS utilities around the system clock.
        path.reset()
        path.moveTo(0f, 820f)
        path.lineTo(535f, 820f)
        path.cubicTo(615f, 820f, 625f, 875f, 710f, 875f)
        path.lineTo(1210f, 875f)
        path.cubicTo(1295f, 875f, 1305f, 820f, 1385f, 820f)
        path.lineTo(1920f, 820f)
        shelfEdge.set(path)
        fill.color = Color.WHITE
        fill.shader = LinearGradient(0f, 820f, 0f, 1080f,
            ThorMenuTheme.tone(0xfff0f4f6.toInt(), ThorMenuTheme.panel),
            ThorMenuTheme.tone(0xffd5e0e5.toInt(), ThorMenuTheme.surfaceBottom), Shader.TileMode.CLAMP)
        path.lineTo(1920f, 1080f)
        path.lineTo(0f, 1080f)
        path.close()
        c.drawPath(path, fill)
        fill.shader = null
        line.color = ThorMenuTheme.tone(0xff55aacb.toInt(), ThorMenuTheme.accent)
        line.strokeWidth = 5f
        c.drawPath(shelfEdge, line)

        val now = System.currentTimeMillis()
        val minute = now / 60_000L
        if (minute != displayedMinute) {
            val date = Date(now)
            val use24HourClock = DateFormat.is24HourFormat(context)
            val locale = Locale.getDefault()
            clockTime = SimpleDateFormat(if (use24HourClock) "HH:mm" else "h:mm", locale).format(date)
            clockPeriod = if (use24HourClock) "" else SimpleDateFormat("a", locale).format(date)
            clockDate = SimpleDateFormat(DateFormat.getBestDateTimePattern(locale, "EEEMd"), locale).format(date)
            displayedMinute = minute
        }
        val time = clockTime
        val period = clockPeriod
        clockType.color = ThorMenuTheme.tone(0xff4b626e.toInt(), ThorMenuTheme.TEXT)
        clockType.textSize = 106f
        val timeWidth = clockType.measureText(time)
        clockType.textSize = 36f
        val periodWidth = if (period.isEmpty()) 0f else clockType.measureText(period) + 13f
        val clockLeft = 960f - (timeWidth + periodWidth) / 2f
        clockType.textAlign = Paint.Align.LEFT
        c.drawText(period, clockLeft + timeWidth + 13f, 859f, clockType)
        clockType.textSize = 106f
        c.drawText(time, clockLeft, 859f, clockType)
        clockType.textSize = 55f
        clockType.textAlign = Paint.Align.CENTER
        c.drawText(clockDate, 960f, 984f, clockType)

        roundButton(c, 175f, 921f, 69f, bar == Bar.WII, -1) { cx, cy ->
            text(c, "WiiDS", cx, cy - 27f, 52f, 112f, true, ThorMenuTheme.tone(0xff365c70.toInt(), ThorMenuTheme.accentText))
        }
        drawSdCard(c, 392f, 921f, bar == Bar.SD)
        roundButton(c, 1745f, 921f, 69f, bar == Bar.MAIL, -3) { cx, cy -> drawEnvelope(c, cx, cy) }
        text(c, "Settings", 175f, 1004f, 39f, 205f, true, ThorMenuTheme.TEXT)
        text(c, "Saves", 392f, 1004f, 39f, 205f, true, ThorMenuTheme.TEXT)
        text(c, "Achievements", 1745f, 1004f, 39f, 252f, true, ThorMenuTheme.TEXT)
    }

    private inline fun roundButton(c: Canvas, cx: Float, cy: Float, r: Float, focused: Boolean, id: Int, face: (Float, Float) -> Unit) {
        fill.shader = null; fill.color = 0x22000000; c.drawCircle(cx, cy + 6f, r, fill)
        fill.color = 0xffffffff.toInt()
        fill.shader = LinearGradient(0f, cy - r, 0f, cy + r, ThorMenuTheme.buttonTop,
            ThorMenuTheme.tone(0xffdfe3e7.toInt(), ThorMenuTheme.buttonBottom), Shader.TileMode.CLAMP)
        c.drawCircle(cx, cy, r, fill); fill.shader = null
        line.color = if (focused) ThorMenuTheme.accent else ThorMenuTheme.tone(0xff8fd3ee.toInt(), ThorMenuTheme.border); line.strokeWidth = if (focused) 9f else 6f
        c.drawCircle(cx, cy, r - 6f, line)
        if (focused) { line.color = ThorMenuTheme.focusGlow; line.strokeWidth = 22f; c.drawCircle(cx, cy, r + 4f, line) }
        face(cx, cy)
        hits.add(RectF(cx - r, cy - r, cx + r, cy + r) to id)
    }

    private fun drawSdCard(c: Canvas, cx: Float, cy: Float, focused: Boolean) {
        box.set(cx - 38f, cy - 50f, cx + 38f, cy + 50f)
        path.reset()
        path.moveTo(box.left, box.top + 10f); path.lineTo(box.right - 22f, box.top); path.lineTo(box.right, box.top + 22f)
        path.lineTo(box.right, box.bottom); path.lineTo(box.left, box.bottom); path.close()
        fill.color = 0xffffffff.toInt()
        fill.shader = LinearGradient(0f, box.top, 0f, box.bottom, 
            ThorMenuTheme.tone(0xffd9f2fb.toInt(), ThorMenuTheme.buttonTop),
            ThorMenuTheme.tone(0xff8fd3ee.toInt(), ThorMenuTheme.buttonBottom), Shader.TileMode.CLAMP)
        c.drawPath(path, fill); fill.shader = null
        line.color = if (focused) ThorMenuTheme.accent else ThorMenuTheme.tone(0xff5fb7da.toInt(), ThorMenuTheme.border); line.strokeWidth = if (focused) 6f else 3f
        c.drawPath(path, line)
        text(c, "SD", cx, cy - 18f, 40f, 60f, true, ThorMenuTheme.tone(0xff2f8fbf.toInt(), ThorMenuTheme.accentText))
        if (focused) { line.color = ThorMenuTheme.focusGlow; line.strokeWidth = 16f; c.drawRoundRect(box.left - 14f, box.top - 14f, box.right + 14f, box.bottom + 14f, 18f, 18f, line) }
        hits.add(RectF(cx - 60f, cy - 70f, cx + 60f, cy + 70f) to -2)
    }

    private fun drawEnvelope(c: Canvas, cx: Float, cy: Float) {
        box.set(cx - 46f, cy - 30f, cx + 46f, cy + 30f)
        fill.color = ThorMenuTheme.tone(0xffa7adb3.toInt(), 0xffaaaaaa.toInt()); c.drawRoundRect(box, 6f, 6f, fill)
        line.color = 0xffffffff.toInt(); line.strokeWidth = 5f
        c.drawLine(box.left + 4f, box.top + 4f, cx, cy + 6f, line); c.drawLine(box.right - 4f, box.top + 4f, cx, cy + 6f, line)
    }

    private fun drawOptions(c: Canvas) {
        // Wii Settings style: a header plate, then two columns of large rounded buttons, four rows a page.
        box.set(0f, 40f, 1920f, 170f)
        ThorMenuTheme.chrome(c, "header", box)
        text(c, "WiiDS Settings", 960f, 68f, 80f, 1200f, true, ThorMenuTheme.TEXT)
        val first = optionPage * OPTIONS_PER_PAGE
        for (k in 0 until OPTIONS_PER_PAGE) {
            val i = first + k
            val entry = options.getOrNull(i) ?: break
            val col = k % 2; val row = k / 2
            box.set(150f + col * 830f, 210f + row * 140f, 150f + col * 830f + 790f, 210f + row * 140f + 112f)
            ThorMenuTheme.chrome(c, if (i == optionSelection) "button_selected" else "button", box)
            text(c, entry.label, box.centerX(), box.top + 28f, 62f, box.width() - 60f, true,
                if (entry.enabled()) ThorMenuTheme.TEXT else ThorMenuTheme.TEXT_SOFT)
            hits.add(RectF(box) to 1000 + i)
        }
        if (optionPages > 1) text(c, "Page ${optionPage + 1} / $optionPages", 960f, 790f, 40f, 400f, true, ThorMenuTheme.TEXT_SOFT)
        // What the highlighted setting does, on a plate at the foot (Wii Settings shows its help there too).
        options.getOrNull(optionSelection)?.let { entry ->
            box.set(150f, 860f, 1770f, 990f)
            ThorMenuTheme.chrome(c, "panel", box)
            text(c, entry.description, 960f, 898f, 54f, 1540f, true, ThorMenuTheme.TEXT)
        }
        // A "Back" plate, as Wii Settings has.
        box.set(1560f, 60f, 1860f, 150f)
        ThorMenuTheme.chrome(c, "button", box)
        text(c, "Back", box.centerX(), box.top + 18f, 56f, 260f, true, ThorMenuTheme.TEXT)
        hits.add(RectF(box) to -20)
    }

    /**
     * The opening screen, in the place of the Wii's Health and Safety screen: plain text on white, WiiDS's own
     * wording, and the pulsing "Press A" line.
     */
    private fun drawSplash(c: Canvas, now: Long) {
        fill.shader = null; fill.color = ThorMenuTheme.tone(Color.WHITE, ThorMenuTheme.surfaceBottom); c.drawRect(0f, 0f, 1920f, 1080f, fill)
        val t = ((now - stateAt) / 600f).coerceIn(0f, 1f)
        val a = (255 * t).toInt()
        text(c, "WiiDS", 960f, 170f, 150f, 900f, true, withAlpha(ThorMenuTheme.accent, a))
        fill.color = withAlpha(ThorMenuTheme.tone(0xffd2d7dc.toInt(), ThorMenuTheme.border), a); c.drawRect(360f, 380f, 1560f, 384f, fill)
        text(c, "Take a break now and then while you play,", 960f, 440f, 64f, 1500f, true, withAlpha(ThorMenuTheme.TEXT, a))
        text(c, "and keep the room bright.", 960f, 520f, 64f, 1500f, true, withAlpha(ThorMenuTheme.TEXT, a))
        text(c, "Your games, saves and settings are ready.", 960f, 640f, 52f, 1500f, true, withAlpha(ThorMenuTheme.TEXT_SOFT, a))
        val blink = (110 + 145 * pulse()).toInt() * a / 255
        text(c, "Press A to continue.", 960f, 840f, 70f, 1200f, true, withAlpha(ThorMenuTheme.TEXT, blink))
    }

    private fun drawNotice(c: Canvas) {
        val w = min(1500f, type.apply { textSize = 40f }.measureText(notice) + 100f)
        box.set(960f - w / 2, 18f, 960f + w / 2, 84f)
        fill.shader = null; fill.color = withAlpha(ThorMenuTheme.panel, 242); c.drawRoundRect(box, 33f, 33f, fill)
        line.color = ThorMenuTheme.accent; line.strokeWidth = 4f; c.drawRoundRect(box, 33f, 33f, line)
        text(c, notice, 960f, 28f, 50f, w - 60f, true, ThorMenuTheme.TEXT)
    }

    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, center: Boolean, color: Int) =
        ThorMenuTheme.text(c, value, x, y, size, width, center, color)

    private fun sound(cue: WiiMenuSounds.Cue) = WiiMenuSounds.play(context, cue)

    private fun pulse() = (0.5f + 0.5f * kotlin.math.sin(SystemClock.uptimeMillis() / 320.0)).toFloat()

    private fun withAlpha(color: Int, alpha: Int) = (color and 0x00ffffff) or (((color ushr 24) * alpha / 255) shl 24)

    companion object {
        private const val PREFS = "wii_channel_menu"
        private const val KEY_LAST = "last"
        private const val KEY_ORDER = "order"
        /** The opening screen shows once each time WiiDS starts. */
        @Volatile private var splashShown = false
        private const val COLUMNS = 4
        private const val ROWS = 3
        private const val PER_PAGE = COLUMNS * ROWS
        private const val OPTIONS_PER_PAGE = 8
        private const val TILE_W = 376f
        private const val TILE_H = 202f
        private const val GAP_X = 34f
        private const val GAP_Y = 20f
        private const val GRID_LEFT = 157f
        private const val GRID_TOP = 112f
        private const val PAGE_SHIFT = COLUMNS * (TILE_W + GAP_X)
        private const val RADIUS = 31f
        private const val FADE_MS = 520L
        private const val NOTICE_MS = 4200L
    }
}
