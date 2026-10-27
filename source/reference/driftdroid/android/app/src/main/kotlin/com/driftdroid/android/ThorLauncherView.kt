package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.SweepGradient
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import java.io.File
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Controller-first Thor library: physical Wii cases in a small perspective carousel. The first
 * [caseCount] actions are cases; the rest are Options rows. The selected case stands at the
 * front, square to the viewer; its neighbours turn inwards and step back, and Left/Right glide
 * the row.
 *
 * Every case surface (cover art, inner booklet, tray, spine, edges) is drawn in its own
 * flat coordinates and mapped onto its projected 3D quad with a homography, so corners stay
 * rounded under perspective, faces are back-face culled by their projected winding, and
 * printed artwork is only ever drawn on the face it belongs to (never mirrored).
 *
 * States: BROWSE -> (A) OPEN -> (A) EXTRACT -> COMMITTED. B closes or backs out at any point
 * before the launch is committed. The existing launch action (with all of its installation,
 * retained-runtime, updater and recovery gates) runs exactly once, at the commit point.
 *
 * [groups] bundles a base game with its mods (case indices, base first). The main shelf shows one
 * stack per bundle, the mods' cases peeking out behind the base case; A on a stack of more than
 * one fans its cases out into a sub-shelf, where A opens a case as usual and B steps back.
 */
internal class ThorLauncherView(
    context: Context,
    private val art: ThorMenuArtwork,
    private val chrome: Map<String, Bitmap>,
    private val actions: List<Entry>,
    private val caseCount: Int = 2,
    groups: List<List<Int>> = emptyList(),
    private val onSelectionChanged: (product: Int?, options: Boolean) -> Unit = { _, _ -> },
    private val onPhaseChanged: (phase: Phase, product: Int?, message: String) -> Unit = { _, _, _ -> },
    /** Blocking reason for launching [product] now, or null when the launch may proceed. */
    private val preflight: (product: Int) -> String? = { null },
    /** Y on a case whose Y is not Retro Rewind's Updates: that game's achievements. */
    private val onAchievements: ((product: Int) -> Unit)? = null,
    private val thorMap: File? = null,
    private val status: () -> String,
) : View(context) {
    /**
     * [art] names the optional private `<art>-cover`/`<art>-disc` images and [banner] an in-APK
     * fallback drawable (0: none). [updatesShortcut] lets Y open Retro Rewind updates while this
     * case is highlighted. [discScale] sizes the disc against a 12 cm Wii disc ([GAMECUBE_DISC]).
     */
    data class Entry(val label: String, val description: String, val enabled: () -> Boolean = { true },
                     val art: String = "", val banner: Int = 0, val updatesShortcut: Boolean = true,
                     val discScale: Float = 1f, val casePlastic: CasePlastic = CasePlastic.WII_WHITE,
                     val action: () -> Unit)
    /** Plastic stock follows the base game's packaging; printed sleeves and discs stay untinted. */
    enum class CasePlastic(val color: Int) {
        WII_WHITE(0xffeef0f2.toInt()), WII_RED(0xffc51d2b.toInt()), GAMECUBE_BLACK(0xff292a2d.toInt())
    }
    enum class Phase { BROWSE, OPEN, LAUNCHING, BLOCKED, OPTIONS }
    private enum class State { BROWSE, OPEN, EXTRACT, RETURN_DISC, COMMITTED }

    // ---------------------------------------------------------------- artwork (decoded once)
    private val bounds = ArrayList<Pair<RectF, Int>>()
    private val banners = Array(caseCount) { i -> actions[i].banner.takeIf { it != 0 }?.let { BitmapFactory.decodeResource(resources, it) } }
    private val packagingFolder = ThorLauncherFolders.cases(context, File(context.filesDir, "WiiCompiled/ThorMap"))
    // Read before the cases load, so a manifest rewritten meanwhile still triggers a reload.
    private var caseStamp = caseArtStamp()
    private val covers = Array(caseCount) { i -> art(i, "cover") }
    private val spines = Array(caseCount) { i -> art(i, "spine") }
    /** Disc labels masked once to a round disc with its centre hole; never rebuilt per frame. */
    private val labels = Array(caseCount) { i -> discLabel(art(i, "disc"), i) }
    private fun art(i: Int, kind: String) = actions[i].art.takeIf { it.isNotEmpty() }?.let { ThorLauncherArtwork.bitmap(packagingFolder, "$it-$kind") }
    private fun caseArtStamp() = File(packagingFolder, "artwork.json").let { "${it.lastModified()}:${it.length()}" }

    /** Cases composed after the launcher opened (a disc imported later) replace the old pictures. */
    private fun reloadCaseArt() {
        val stamp = caseArtStamp()
        if (stamp == caseStamp) return
        caseStamp = stamp
        for (i in 0 until caseCount) {
            covers[i] = art(i, "cover")
            spines[i] = art(i, "spine")
            labels[i] = discLabel(art(i, "disc"), i)
        }
        invalidate()
    }
    // Art built in the background (after an import) can finish while the launcher is showing.
    private val caseArtPoll = object : Runnable {
        override fun run() {
            if (!isAttachedToWindow) return
            reloadCaseArt()
            postDelayed(this, 1000L)
        }
    }

    // ---------------------------------------------------------------- scene constants
    private val W = 350f; private val H = 500f; private val T = 34f; private val COVER = 5f
    private val F = 2180f; private val D = 2100f; private val CX = 960f; private val CY = 528f
    private val hubX = 175f; private val hubY = 262f; private val discR = 152f
    /** Disc radius of case [i]; the spindle hole and clear hub ring keep their real size. */
    private fun radius(i: Int) = discR * actions[i].discScale
    private val toLight = normalize(floatArrayOf(-0.38f, -0.52f, -0.76f))

    // ---------------------------------------------------------------- bundles
    /** Each case in exactly one bundle; anything not covered (or covered twice) stands alone. */
    private val bundles: List<List<Int>> = run {
        val seen = HashSet<Int>()
        val valid = groups.map { g -> g.filter { it in 0 until caseCount && seen.add(it) } }.filter { it.isNotEmpty() }
        valid + (0 until caseCount).filter { it !in seen }.map { listOf(it) }
    }.sortedBy { it.first() }
    private val bundleOf = IntArray(caseCount).also { a -> bundles.forEachIndexed { g, m -> m.forEach { a[it] = g } } }
    /** 0 for the base game, 1.. for the mods stacked behind it. */
    private val depthOf = IntArray(caseCount).also { a -> bundles.forEach { m -> m.forEachIndexed { k, i -> a[i] = k } } }
    private fun base(i: Int) = bundles[bundleOf[i]][0]
    private fun stacked(i: Int) = bundles[bundleOf[i]].size > 1

    // ---------------------------------------------------------------- state
    private var options = false
    private var musicPanel = false
    /** Bundle fanned out on the sub-shelf, or -1 on the main shelf. */
    private var expanded = restoredSelection(context, caseCount).let { s ->
        if (restoredBundle(context) && s < caseCount && stacked(s)) bundleOf[s] else -1
    }
    /** A case index (on the main shelf always a bundle's base case), or [caseCount] for Options. */
    private var selection = restoredSelection(context, caseCount).let { if (it < caseCount && expanded < 0) base(it) else it }
    private var state = State.BROWSE
    private var opened = -1                    // case currently opened (or being closed)
    private var stateAt = SystemClock.uptimeMillis()
    private var notice = ""
    private var noticeAt = 0L
    private var committedAt = 0L
    private var hostResumed = true
    private var lastAxis = 0L
    private var downTarget = -1
    private var downWasFront = true
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var lastFrame = 0L
    private var spin = 0f                      // disc spin angle (degrees) during extraction
    private var spinRate = 0f                  // degrees per second

    private class Spring(var value: Float, private val stiffness: Float, damping: Float) {
        private val damp = damping * 2f * sqrt(stiffness)
        var target = value
        var velocity = 0f
        /** Semi-implicit Euler in small substeps; returns true while still moving. */
        fun step(dt: Float): Boolean {
            var left = dt
            while (left > 0f) {
                val h = min(left, 1f / 240f)
                velocity += (-stiffness * (value - target) - damp * velocity) * h
                value += velocity * h
                left -= h
            }
            if (abs(value - target) < 0.0005f && abs(velocity) < 0.002f) { value = target; velocity = 0f; return false }
            return true
        }
    }
    // Lid: slightly under-damped so it swings past and settles like a real hinge.
    private val lid = Array(caseCount) { Spring(0f, 70f, 0.74f) }
    private val focus = Array(caseCount) { i -> Spring(if (selection == i) 1f else 0f, 150f, 0.82f) }
    private val stage = Spring(0f, 60f, 0.9f)  // 0 = two cases side by side, 1 = opened case centred
    private val dim = Spring(0f, 40f, 1f)      // 1 while the disc is being presented
    /** Main shelf position: the (fractional) bundle index currently at the front and centre. */
    private val carousel = Spring(bundleOf[selection.takeIf { it < caseCount } ?: 0].toFloat(), 110f, 0.9f)
    /** 0 = main shelf of stacks, 1 = [fanned]'s cases spread out on the sub-shelf. */
    private val fan = Spring(if (expanded >= 0) 1f else 0f, 90f, 0.9f)
    /** Sub-shelf position: the (fractional) depth within [fanned] at the front. */
    private val sub = Spring((selection.takeIf { it < caseCount }?.let { depthOf[it] } ?: 0).toFloat(), 110f, 0.9f)
    /** The bundle being fanned out, or folding back in once [expanded] is cleared. */
    private var fanned = expanded

    // ---------------------------------------------------------------- reusable drawing objects
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val faceMatrix = Matrix()
    private val src = FloatArray(8)
    private val quad = FloatArray(8)
    private val world = FloatArray(12)
    private val rect = RectF()
    private val path = Path()
    private val sheenPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rimPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        shader = SweepGradient(0f, 0f, intArrayOf(0xffe9eef2.toInt(), 0xff9aa4ad.toInt(), 0xfff8fbfd.toInt(), 0xff8d97a1.toInt(), 0xffe9eef2.toInt()), null)
    }
    private val edgePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        shader = SweepGradient(0f, 0f, intArrayOf(0xffaeb7bf.toInt(), 0xff6f7a84.toInt(), 0xffd7dde2.toInt(), 0xff76818b.toInt(), 0xffaeb7bf.toInt()), null)
    }
    private val sheenShader = LinearGradient(-1f, 0f, 1f, 0f,
        intArrayOf(0x00ffffff, 0x2effffff, 0x5affffff, 0x2effffff, 0x00ffffff), floatArrayOf(0f, 0.38f, 0.5f, 0.62f, 1f), Shader.TileMode.CLAMP)
    private val sleeveShader = LinearGradient(0f, 0f, W, H,
        intArrayOf(0x00ffffff, 0x00ffffff, 0x30ffffff, 0x00ffffff, 0x10ffffff, 0x00ffffff),
        floatArrayOf(0f, 0.32f, 0.4f, 0.5f, 0.66f, 0.72f), Shader.TileMode.CLAMP)
    private val wellShaders = Array(caseCount) { i -> RadialGradient(hubX, hubY, radius(i) + 8f,
        intArrayOf(plastic(i, 1f, 0xffe8ebee.toInt()), plastic(i, 1f, 0xffdfe3e7.toInt()),
            plastic(i, 1f, 0xffc9cfd5.toInt()), plastic(i, 1f)), floatArrayOf(0f, 0.72f, 0.94f, 1f), Shader.TileMode.CLAMP) }
    private val shadeMatrix = Matrix()

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        requestFocus()
        describe()
    }

    // ================================================================ lifecycle hooks
    override fun onAttachedToWindow() {
        super.onAttachedToWindow(); requestFocus(); refreshSelection()
        removeCallbacks(caseArtPoll); postDelayed(caseArtPoll, 1000L)
    }
    override fun onDetachedFromWindow() { removeCallbacks(caseArtPoll); super.onDetachedFromWindow() }

    /** Re-apply details when Android recreates the lower-display Presentation. */
    fun refreshSelection() {
        onSelectionChanged(selection.takeIf { !options && it < caseCount }, options)
        publishPhase()
    }

    /** The launcher became visible again (Home return, game exit, sleep/wake). */
    fun onHostResumed() {
        hostResumed = true
        reloadCaseArt()
        if (state == State.COMMITTED || state == State.EXTRACT || state == State.RETURN_DISC) {
            // Back from the game (or a gate screen): put the disc home and close calmly.
            state = State.BROWSE
            opened = -1
            for (s in lid) s.target = 0f
            stage.target = 0f; dim.target = 0f; dim.value = 0f
            spin = 0f; spinRate = 0f
        }
        thorMap?.let { ThorLauncherAudio.show(context, it) }
        lastFrame = 0L
        publishPhase()
        invalidate()
    }

    /** True from the launch commit until the launcher is shown again. */
    val launchCommitted: Boolean get() = state == State.COMMITTED

    fun onHostPaused() {
        hostResumed = false
        if (state != State.COMMITTED) ThorLauncherAudio.hide()
    }

    override fun onWindowFocusChanged(hasWindowFocus: Boolean) {
        super.onWindowFocusChanged(hasWindowFocus)
        // A cold start or a returning dialog can leave no focused view; physical buttons need one.
        if (hasWindowFocus && !isFocused) requestFocus()
        lastFrame = 0L
        invalidate()
    }

    // ================================================================ drawing
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val now = SystemClock.uptimeMillis()
        val dt = if (lastFrame == 0L) 0f else min(0.05f, (now - lastFrame) / 1000f)
        lastFrame = now
        val moving = advance(now, dt)

        canvas.drawColor(Color.BLACK)
        scale = min(width / 1920f, height / 1080f)
        offsetX = (width - 1920f * scale) / 2f
        offsetY = (height - 1080f * scale) / 2f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        panel(canvas, "background", RectF(0f, 0f, 1920f, 1080f))
        bounds.clear()
        if (options) drawOptions(canvas) else drawLibrary(canvas, now)
        drawChrome(canvas)
        // The launch fade covers everything, chrome included, so the game starts from clean black.
        if (!options) drawVeil(canvas, now)
        canvas.restore()
        if (moving || (now - noticeAt < 4200L && notice.isNotEmpty())) postInvalidateOnAnimation()
    }

    /** Advance springs and timelines; returns true while anything is still moving. */
    private fun advance(now: Long, dt: Float): Boolean {
        var moving = false
        val noticeActive = now - noticeAt < 4200L && notice.isNotEmpty()
        if (noticeActive != noticeShown) { noticeShown = noticeActive; publishPhase() }
        for (i in 0 until caseCount) {
            focus[i].target = if (!options && selection == i && state == State.BROWSE) 1f else if (opened == i) 1f else 0f
            lid[i].target = if (opened == i && state != State.BROWSE) 1f else 0f
            if (focus[i].step(dt)) moving = true
            if (lid[i].step(dt)) moving = true
        }
        // Options keeps the last game at the front; the row glides there with the selection.
        val front = front()
        carousel.target = bundleOf[front].toFloat()
        if (carousel.step(dt)) moving = true
        fan.target = if (expanded >= 0) 1f else 0f
        if (fan.step(dt)) moving = true
        if (fanned >= 0 && bundleOf[front] == fanned) sub.target = depthOf[front].toFloat()
        if (sub.step(dt)) moving = true
        stage.target = if (opened >= 0 && state != State.BROWSE) 1f else 0f
        dim.target = if (state == State.EXTRACT || state == State.COMMITTED) 1f else 0f
        if (stage.step(dt)) moving = true
        if (dim.step(dt)) moving = true
        if (state == State.BROWSE && opened >= 0 && lid[opened].value < 0.002f && stage.value < 0.002f) opened = -1
        when (state) {
            State.EXTRACT -> {
                val t = (now - stateAt) / 1000f
                spinRate = when {
                    t < LIFT_START -> 0f
                    t < SPIN_FULL -> 2880f * smooth((t - LIFT_START) / (SPIN_FULL - LIFT_START)).let { it * it }
                    else -> 2880f
                }
                spin = (spin + spinRate * dt) % 360f
                if (t >= MUSIC_FADE && !musicFading) { musicFading = true; ThorLauncherAudio.fadeForLaunch(900L) }
                if (t >= LAUNCH_SOUND && !launchCue) { launchCue = true; ThorLauncherAudio.play(ThorLauncherAudio.Sound.LAUNCH) }
                if (t >= COMMIT) commit()
                moving = true
            }
            State.RETURN_DISC -> {
                val t = (now - stateAt) / 1000f
                spinRate = max(0f, spinRate - 3200f * dt)
                spin = (spin + spinRate * dt) % 360f
                if (t >= RETURN_TIME) { state = State.OPEN; stateAt = now; spin = 0f; publishPhase() }
                moving = true
            }
            State.COMMITTED -> {
                // The game normally takes over within a moment. If nothing left this screen
                // (a gate kept us here), restore a useful menu rather than a frozen frame.
                if (hostResumed && now - committedAt > 3500L) {
                    state = State.OPEN; stateAt = now; spin = 0f; spinRate = 0f
                    setNotice("The game did not start. Check Options or updates, then try again.")
                    ThorLauncherAudio.play(ThorLauncherAudio.Sound.REJECT)
                    thorMap?.let { ThorLauncherAudio.show(context, it) }
                    publishPhase()
                }
                moving = true
            }
            else -> {}
        }
        return moving
    }

    private var noticeShown = false
    private var musicFading = false
    private var launchCue = false

    private fun drawChrome(c: Canvas) {
        panel(c, "header", RectF(0f, 40f, 1920f, 164f))
        val title = when {
            options && musicPanel -> "Menu Music"
            options -> "Options"
            state != State.BROWSE && opened >= 0 -> actions[opened].label
            // The sub-shelf is titled after its base game ("Mario Kart Wii": the game and its mods).
            expanded >= 0 -> actions[bundles[expanded][0]].label
            else -> "Select Game"
        }
        text(c, title, 140f, 70f, 62f, 1560f)
        panel(c, "footer", RectF(0f, 1000f, 1920f, 1080f))
        val hints = when {
            options && musicPanel -> listOf("Left / Right  Volume", "A  Mute", "B  Done")
            options -> listOf("A  Select", "", "B  Back")
            state == State.OPEN -> listOf("A  Select disc", "Left / Right  Other game", "B  Close case")
            state == State.EXTRACT -> listOf("", "Starting...", "B  Cancel")
            state == State.COMMITTED || state == State.RETURN_DISC -> listOf("", "", "")
            selection == caseCount -> listOf("A  Options", "Up  Game cases", if (expanded >= 0) "B  All games" else "AYN Thor")
            else -> listOf(if (expanded < 0 && stacked(selection)) "A  Choose game" else "A  Open case",
                if (expanded >= 0) "B  All games" else "X  Options",
                if (BuildConfig.THOR_RETRO_REWIND && actions[selection].updatesShortcut) "Y  Updates"
                else if (onAchievements != null) "Y  Achievements" else "AYN Thor")
        }
        text(c, hints[0], 140f, 1015f, 38f, 560f)
        text(c, hints[1], 960f, 1015f, 38f, 560f, true)
        text(c, hints[2], 1780f, 1015f, 38f, 360f, true)
    }

    private fun drawLibrary(c: Canvas, now: Long) {
        // A closed stack is one shelf object: do not let a neighbouring bundle's base case
        // paint over its mod while that stack's own base remains in front. The same order
        // drives hit targets, so an overlapping visible case wins its matching touch target.
        val caseOrder = ThorCasePaintOrder.indices(bundles, FloatArray(caseCount) { caseZ(it) },
            ThorCasePaintOrder.closedShelf(expanded, fan.value, opened))
        // Soft studio light behind the front case, on the original menu backdrop.
        if (selection < caseCount || opened >= 0) {
            paint.shader = RadialGradient(960f, 540f, 700f, intArrayOf(0x52f4f8ff, 0x1fc8dcff, 0x00000000), floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
            c.drawRect(0f, 164f, 1920f, 1000f, paint)
            paint.shader = null
        }
        // The front game's title sits under the carousel and fades as its case opens.
        val labelAlpha = ((1f - stage.value) * 255).toInt().coerceIn(0, 255)
        if (labelAlpha > 8) {
            val front = front()
            val area = RectF(660f, 842f, 1260f, 904f)
            panel(c, if (selection < caseCount) "button_selected" else "button", area, labelAlpha)
            textAlpha(c, actions[front].label, 960f, 848f, 42f, area.width() - 70f, labelAlpha)
            // A stack on the main shelf says how many games it holds.
            if (expanded < 0 && stacked(front)) {
                val badge = RectF(1336f, 848f, 1496f, 898f)
                paint.color = Color.argb(labelAlpha, 47, 143, 224); c.drawRoundRect(badge, 25f, 25f, paint)
                stroke.color = Color.argb(labelAlpha, 255, 255, 255); stroke.strokeWidth = 4f; c.drawRoundRect(badge, 25f, 25f, stroke)
                val count = "${bundles[bundleOf[front]].size} games"
                if (ThorMenuTheme.wii(context)) ThorMenuTheme.text(c, count, badge.centerX(), badge.top + 7f, 30f, badge.width() - 20f, true, 0xffffffff.toInt(), labelAlpha)
                else textAlpha(c, count, badge.centerX(), badge.top + 7f, 30f, badge.width() - 20f, labelAlpha)
            }
            // Chevrons show there are more games to either side.
            paint.color = if (ThorMenuTheme.wii(context)) Color.argb(labelAlpha * 7 / 10, 112, 119, 128)
                else Color.argb(labelAlpha * 7 / 10, 255, 255, 255)
            for (side in intArrayOf(-1, 1)) {
                if (neighbour(front, side) == front) continue
                val x = 960f + side * 336f
                path.reset(); path.moveTo(x + side * 16f, 873f); path.lineTo(x - side * 8f, 855f); path.lineTo(x - side * 8f, 891f); path.close()
                c.drawPath(path, paint)
            }
            // Touch targets follow each projected case, nearest last so it wins an overlap. On the
            // main shelf a stacked mod's case stands for its whole bundle.
            for (i in caseOrder) {
                if (hidden(i) > 0.5f) continue
                val s = F / (caseZ(i) + D)
                val x = CX + caseX(i) * s
                bounds.add(RectF(x - W / 2f * s, 230f, x + W / 2f * s, 830f) to if (expanded < 0) base(i) else i)
            }
            bounds.add(area to front)
            val optionArea = RectF(730f, 922f, 1190f, 982f)
            panel(c, if (selection == caseCount) "button_selected" else "button", optionArea, labelAlpha)
            textAlpha(c, "Options", 960f, 927f, 38f, 400f, labelAlpha)
            bounds.add(optionArea to caseCount)
        }
        // Back-to-front bundles on the main shelf; individual depths while fanned or opened.
        for (i in caseOrder) drawCase(c, i)
        // The case recedes into shade while its disc is presented.
        if (dim.value > 0.01f) { paint.color = Color.argb((dim.value * 120).toInt(), 0, 0, 0); c.drawRect(0f, 164f, 1920f, 1000f, paint) }
        if (state == State.EXTRACT || state == State.COMMITTED || state == State.RETURN_DISC) drawFreeDisc(c, now)

        val message = if (now - noticeAt < 4200L) notice else status()
        if (message.isNotBlank()) {
            val area = RectF(330f, 900f, 1590f, 984f)
            if (stage.value > 0.5f) { panel(c, "panel", area, 235); text(c, message, 960f, 921f, 31f, 1200f, true) }
            else text(c, message, 960f, 868f, 27f, 1590f, true)
        }
        if (state == State.OPEN && opened >= 0) bounds.add(discScreenBounds() to 10 + opened)
    }

    // ================================================================ 3D case
    /** The case at the front of the shelf: the opened one, else the highlight, else the last game. */
    private fun front() = if (opened >= 0) opened else if (selection < caseCount) selection else lastCase

    /** Signed shelf distance of case [i] from the front: by bundle on the main shelf, by depth when fanned. */
    private fun mainD(i: Int) = bundleOf[i] - carousel.value
    private fun subD(i: Int) = depthOf[i] - sub.value
    private fun inFan(i: Int) = fanned >= 0 && bundleOf[i] == fanned
    /** 1 for cases of other bundles while one is fanned out: they slide off and fade. */
    private fun hidden(i: Int) = if (fanned >= 0 && !inFan(i)) fan.value else 0f

    // A mod's case stands behind its base game, shifted right so its edge and cover peek out.
    private val STACK_X = 96f; private val STACK_Z = 150f

    /** Carousel offset at shelf distance [d]: the front centred, neighbours either side, further ones tucked behind. */
    private fun shelfX(d: Float): Float {
        val far = abs(d)
        return Math.signum(d) * (450f * min(far, 1f) + 210f * max(far - 1f, 0f))
    }
    private fun shelfZ(d: Float, focused: Float): Float {
        // The front case comes forward, neighbours step back, further ones further still.
        val far = abs(d)
        return -120f * focused + 240f * min(far, 1f) + 150f * max(far - 1f, 0f)
    }

    private fun caseZ(i: Int): Float {
        val s = stage.value
        val other = opened >= 0 && opened != i
        val main = shelfZ(mainD(i), focus[base(i)].value) + STACK_Z * depthOf[i]
        val fanZ = if (inFan(i)) shelfZ(subD(i), focus[i].value) else main + 300f
        val browseZ = lerp(main, fanZ, if (fanned >= 0) fan.value else 0f)
        val openZ = if (other) 520f else -440f
        return lerp(browseZ, if (opened < 0) browseZ else openZ, s) + (if (opened == i) 90f * dim.value else 0f)
    }

    private fun caseX(i: Int): Float {
        val main = shelfX(mainD(i)) + STACK_X * depthOf[i]
        val fanX = if (inFan(i)) shelfX(subD(i)) else main + (if (bundleOf[i] < fanned) -1250f else 1250f)
        val browse = lerp(main, fanX, if (fanned >= 0) fan.value else 0f)
        if (opened < 0) return browse
        val open = if (opened == i) W / 2f else if (i < opened) -1250f else 1250f
        return lerp(browse, open, stage.value)
    }

    private fun caseYaw(i: Int): Float {
        // Neighbours turn inwards to face the front case (cover-flow); square to the viewer at the
        // front and when open. A stacked mod turns with its base game.
        val mainSide = -mainD(i).coerceIn(-1f, 1f) * (1f - 0.35f * focus[base(i)].value)
        val fanSide = if (inFan(i)) -subD(i).coerceIn(-1f, 1f) * (1f - 0.35f * focus[i].value) else mainSide
        val browse = 48f * lerp(mainSide, fanSide, if (fanned >= 0) fan.value else 0f)
        return Math.toRadians(lerp(browse, 0f, stage.value).toDouble()).toFloat()
    }

    private var curX = 0f; private var curY = 0f; private var curZ = 0f; private var curYaw = 0f; private var curLid = 0f

    /** Local case point (x from spine, y from top, z from front) -> world into [world] at [k]. */
    private fun casePoint(lx: Float, ly: Float, lz: Float, cover: Boolean, k: Int) {
        var x = lx; var z = lz
        if (cover) {
            val a = curLid
            val ca = cos(a); val sa = sin(a)
            val nx = x * ca + z * sa; val nz = -x * sa + z * ca; x = nx; z = nz
        }
        x -= W / 2f; val y = ly - H / 2f; z -= T / 2f
        val cy = cos(curYaw); val sy = sin(curYaw)
        world[k] = curX + x * cy + z * sy
        world[k + 1] = curY + y
        world[k + 2] = curZ - x * sy + z * cy
    }

    private fun project(k: Int, q: Int) {
        val s = F / (world[k + 2] + D)
        quad[q] = CX + world[k] * s
        quad[q + 1] = CY + world[k + 1] * s
    }

    /** Prepare a face from four local corners (TL, TR, BR, BL as seen from outside); null if facing away. */
    private fun face(cover: Boolean, vararg p: Float): Float? {
        for (n in 0..3) { casePoint(p[n * 3], p[n * 3 + 1], p[n * 3 + 2], cover, n * 3); project(n * 3, n * 2) }
        var area = 0f
        for (n in 0..3) { val m = (n + 1) % 4; area += quad[n * 2] * quad[m * 2 + 1] - quad[m * 2] * quad[n * 2 + 1] }
        if (area < 40f) return null
        // Outward normal from the world-space corners, then a soft key light.
        val ax = world[3] - world[0]; val ay = world[4] - world[1]; val az = world[5] - world[2]
        val bx = world[9] - world[0]; val by = world[10] - world[1]; val bz = world[11] - world[2]
        var nx = -(ay * bz - az * by); var ny = -(az * bx - ax * bz); var nz = -(ax * by - ay * bx)
        val len = sqrt(nx * nx + ny * ny + nz * nz).coerceAtLeast(1e-4f); nx /= len; ny /= len; nz /= len
        val diffuse = max(0f, nx * toLight[0] + ny * toLight[1] + nz * toLight[2])
        return 0.58f + 0.42f * diffuse
    }

    private fun begin(c: Canvas, w: Float, h: Float) {
        src[0] = 0f; src[1] = 0f; src[2] = w; src[3] = 0f; src[4] = w; src[5] = h; src[6] = 0f; src[7] = h
        faceMatrix.setPolyToPoly(src, 0, quad, 0, 4)
        c.save()
        c.concat(faceMatrix)
    }

    private fun plastic(i: Int, shade: Float, base: Int = 0xffeef0f2.toInt()): Int {
        val stock = actions[i].casePlastic.color
        // Preserve the existing moulded shading and reflections without bleaching red/black stock.
        val r = (Color.red(base) * Color.red(stock) / 238f * shade).toInt().coerceIn(0, 255)
        val g = (Color.green(base) * Color.green(stock) / 240f * shade).toInt().coerceIn(0, 255)
        val b = (Color.blue(base) * Color.blue(stock) / 242f * (shade * 0.97f + 0.03f)).toInt().coerceIn(0, 255)
        return Color.rgb(r, g, b)
    }

    private fun drawCase(c: Canvas, i: Int) {
        curX = caseX(i); curY = 8f; curZ = caseZ(i); curYaw = caseYaw(i)
        val l = lid[i].value
        curLid = Math.toRadians((l.coerceIn(0f, 1.035f) * 172.0)).toFloat()
        val enabled = actions[i].enabled()
        val fade = (if (opened >= 0 && opened != i) (1f - stage.value).coerceIn(0f, 1f) else 1f) * (1f - hidden(i))
        if (fade < 0.02f) return
        val layer = if (fade < 0.999f) c.saveLayerAlpha(0f, 0f, 1920f, 1080f, (fade * 255).toInt()) else -1
        val focused = focus[i].value
        drawFloorShadow(c, focused)

        // Back tray slab: sides, then the molded inner face.
        face(false, 0f, 0f, T, 0f, 0f, 0f, 0f, H, 0f, 0f, H, T)?.let { s -> begin(c, T, H); spine(c, s, i); c.restore() }
        face(false, W, 0f, 0f, W, 0f, T, W, H, T, W, H, 0f)?.let { s -> begin(c, T, H); edge(c, s, T, H, i); c.restore() }
        face(false, 0f, 0f, T, W, 0f, T, W, 0f, 0f, 0f, 0f, 0f)?.let { s -> begin(c, W, T); edge(c, s, W, T, i); c.restore() }
        face(false, 0f, H, 0f, W, H, 0f, W, H, T, 0f, H, T)?.let { s -> begin(c, W, T); edge(c, s * 0.8f, W, T, i); c.restore() }
        if (l > 0.001f) face(false, 0f, 0f, COVER, W, 0f, COVER, W, H, COVER, 0f, H, COVER)?.let { s ->
            begin(c, W, H); tray(c, s, i, enabled); c.restore()
        }

        // Front cover slab, rotating about the real spine edge.
        face(true, W, 0f, 0f, W, 0f, COVER, W, H, COVER, W, H, 0f)?.let { s -> begin(c, COVER, H); edge(c, s * 1.04f, COVER, H, i); c.restore() }
        face(true, 0f, 0f, COVER, W, 0f, COVER, W, 0f, 0f, 0f, 0f, 0f)?.let { s -> begin(c, W, COVER); edge(c, s, W, COVER, i); c.restore() }
        val outer = face(true, 0f, 0f, 0f, W, 0f, 0f, W, H, 0f, 0f, H, 0f)
        if (outer != null) { begin(c, W, H); coverOuter(c, outer, i, enabled, focused); c.restore() }
        else face(true, W, 0f, COVER, 0f, 0f, COVER, 0f, H, COVER, W, H, COVER)?.let { s ->
            // Inner face listed as seen from inside: the booklet maps left-to-right, unmirrored.
            begin(c, W, H); coverInner(c, s, i, enabled); c.restore()
        }
        if (layer >= 0) c.restoreToCount(layer)
    }

    private fun drawFloorShadow(c: Canvas, focused: Float) {
        // Contact shadow on an implied shelf; widens as the cover swings out to the left.
        casePoint(0f, H, T, false, 0); project(0, 0)
        casePoint(W, H, T, false, 3); project(3, 2)
        val left = min(quad[0], quad[2]); val right = max(quad[0], quad[2]); val y = max(quad[1], quad[3])
        val spread = 1f + curLid / Math.PI.toFloat() * 0.95f
        val cxs = (left + right) / 2f - (right - left) * (spread - 1f) / 2f
        val rx = (right - left) * 0.62f * spread; val ry = 30f + 10f * focused
        paint.shader = RadialGradient(0f, 0f, 1f, intArrayOf(0x66000000, 0x26000000, 0x00000000), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
        c.save(); c.translate(cxs, y + 6f); c.scale(rx, ry); c.drawCircle(0f, 0f, 1f, paint); c.restore()
        paint.shader = null
    }

    private fun spine(c: Canvas, shade: Float, i: Int) {
        rect.set(0f, 0f, T, H)
        paint.color = plastic(i, shade * 0.96f); c.drawRoundRect(rect, 6f, 6f, paint)
        val scan = spines[i]
        if (scan != null) {
            // An upright scan maps directly onto the outward-facing spine quad. Its own printed
            // title and platform marks stay intact, inside the stock-coloured plastic border.
            rect.set(2f, 12f, T - 2f, H - 12f)
            art.roundedImage(c, scan, rect, 1.5f)
            paint.color = Color.argb(((1f - shade) * 190).toInt().coerceIn(0, 200), 10, 14, 22)
            c.drawRoundRect(rect, 1.5f, 1.5f, paint)
            // Restrained clear-sleeve reflection, aligned to the thin face rather than its text.
            paint.color = 0x24ffffff; c.drawRect(2f, 14f, 3.5f, H - 14f, paint)
            paint.color = 0x24000000; c.drawRect(T - 3.5f, 14f, T - 2f, H - 14f, paint)
        } else {
            // Generated cases without a supplied scan retain the original-font label fallback.
            paint.color = plastic(i, shade, 0xfff7f8f9.toInt()); c.drawRect(5f, 14f, T - 5f, H - 14f, paint)
            c.save(); c.rotate(-90f, T / 2f, H / 2f)
            textAlpha(c, actions[i].label, T / 2f, H / 2f - 12f, 22f, H - 80f, (200 * shade).toInt())
            c.restore()
        }
        rect.set(0f, 0f, T, H)
        stroke.color = 0x26000000; stroke.strokeWidth = 1.5f; c.drawRoundRect(rect, 6f, 6f, stroke)
    }

    private fun edge(c: Canvas, shade: Float, w: Float, h: Float, i: Int) {
        paint.color = plastic(i, shade, 0xffe3e7ea.toInt())
        c.drawRect(0f, 0f, w, h, paint)
        // A faint moulding line keeps thin plastic edges from reading as flat grey.
        paint.color = Color.argb(28, 255, 255, 255)
        if (w < h) c.drawRect(w * 0.42f, 3f, w * 0.58f, h - 3f, paint) else c.drawRect(3f, h * 0.42f, w - 3f, h * 0.58f, paint)
    }

    private fun coverOuter(c: Canvas, shade: Float, i: Int, enabled: Boolean, focused: Float) {
        rect.set(0f, 0f, W, H)
        paint.color = plastic(i, shade); c.drawRoundRect(rect, 14f, 14f, paint)
        rect.set(10f, 9f, W - 10f, H - 9f)
        val cover = covers[i]
        if (cover != null) art.roundedImage(c, cover, rect, 4f, if (enabled) 255 else 150)
        else {
            paint.color = plastic(i, shade, 0xfff6f7f8.toInt()); c.drawRoundRect(rect, 4f, 4f, paint)
            banners[i]?.let { art.image(c, it, RectF(14f, 200f, W - 14f, 274f)) }
            text(c, actions[i].label, W / 2f, 292f, 28f, W - 60f, true)
        }
        // Light falls off over the printed sleeve, plus a restrained moving sleeve reflection.
        paint.color = Color.argb(((1f - shade) * 190).toInt().coerceIn(0, 200), 10, 14, 22)
        c.drawRoundRect(rect, 4f, 4f, paint)
        sheenPaint.shader = sleeveShader
        shadeMatrix.setTranslate(-60f + curYaw * 900f + curLid * 60f, 0f); sleeveShader.setLocalMatrix(shadeMatrix)
        rect.set(0f, 0f, W, H); c.drawRoundRect(rect, 14f, 14f, sheenPaint)
        // Focus: a soft blue edge light, never a hard outline.
        if (focused > 0.01f && state == State.BROWSE) {
            stroke.color = Color.argb((110 * focused).toInt(), 70, 170, 255); stroke.strokeWidth = 7f
            rect.inset(-3f, -3f); c.drawRoundRect(rect, 16f, 16f, stroke)
        }
        stroke.color = 0x30000000; stroke.strokeWidth = 1.5f; rect.set(0.5f, 0.5f, W - 0.5f, H - 0.5f); c.drawRoundRect(rect, 14f, 14f, stroke)
    }

    private fun coverInner(c: Canvas, shade: Float, i: Int, enabled: Boolean) {
        rect.set(0f, 0f, W, H)
        paint.color = plastic(i, shade, 0xffeceef0.toInt()); c.drawRoundRect(rect, 14f, 14f, paint)
        stroke.color = 0x1f000000; stroke.strokeWidth = 2f; rect.set(16f, 16f, W - 16f, H - 16f); c.drawRoundRect(rect, 10f, 10f, stroke)
        // The printed booklet (a reduced copy of the supplied cover, an explicit adaptation).
        rect.set(38f, 46f, W - 38f, H - 46f)
        paint.color = 0x33000000; c.drawRoundRect(RectF(rect.left + 4f, rect.top + 6f, rect.right + 4f, rect.bottom + 6f), 4f, 4f, paint)
        val booklet = covers[i]
        if (booklet != null) art.roundedImage(c, booklet, rect, 3f, if (enabled) 245 else 150)
        else { paint.color = 0xfff9fafb.toInt(); c.drawRoundRect(rect, 3f, 3f, paint) }
        paint.color = Color.argb(((1f - shade) * 170).toInt().coerceIn(0, 190), 10, 14, 22); c.drawRoundRect(rect, 3f, 3f, paint)
        // Molded retention clips along the outer edge hold the booklet.
        for (y in floatArrayOf(70f, H - 96f)) {
            paint.color = plastic(i, shade * 0.92f, 0xffdfe3e6.toInt()); c.drawRoundRect(RectF(26f, y, 52f, y + 26f), 5f, 5f, paint)
            paint.color = 0x22000000; c.drawRect(30f, y + 20f, 52f, y + 26f, paint)
        }
    }

    private fun tray(c: Canvas, shade: Float, i: Int, enabled: Boolean) {
        rect.set(0f, 0f, W, H)
        paint.color = plastic(i, shade, 0xffe6e9ec.toInt()); c.drawRoundRect(rect, 14f, 14f, paint)
        stroke.color = 0x1c000000; stroke.strokeWidth = 2f; rect.set(14f, 14f, W - 14f, H - 14f); c.drawRoundRect(rect, 10f, 10f, stroke)
        // Molded disc well with finger notches either side.
        val r = radius(i)
        paint.shader = wellShaders[i]; c.drawCircle(hubX, hubY, r + 8f, paint); paint.shader = null
        paint.color = 0x14000000; c.drawCircle(hubX - r - 4f, hubY, 24f, paint); c.drawCircle(hubX + r + 4f, hubY, 24f, paint)
        stroke.color = 0x24000000; stroke.strokeWidth = 2f; c.drawCircle(hubX, hubY, r + 8f, stroke)
        // Small top and bottom retention clips for inserts.
        paint.color = plastic(i, shade * 0.95f, 0xffdde1e4.toInt())
        c.drawRoundRect(RectF(W / 2f - 24f, 22f, W / 2f + 24f, 34f), 4f, 4f, paint)
        c.drawRoundRect(RectF(W / 2f - 24f, H - 34f, W / 2f + 24f, H - 22f), 4f, 4f, paint)
        // Cover shadow sweeping across the tray while the lid is still near it.
        val lidOpen = curLid / Math.toRadians(172.0).toFloat()
        if (lidOpen < 0.6f) {
            val reach = W * cos(curLid).coerceAtLeast(0f)
            paint.shader = LinearGradient(0f, 0f, max(reach, 1f), 0f, 0x5a000000, 0x00000000, Shader.TileMode.CLAMP)
            paint.alpha = ((1f - lidOpen / 0.6f) * 255).toInt().coerceIn(0, 255)
            c.drawRect(0f, 0f, W, H, paint); paint.shader = null; paint.alpha = 255
        }
        val discHome = state == State.OPEN || state == State.BROWSE || opened != i
        if (discHome) drawDisc(c, hubX, hubY, r, 0f, 0f, i, enabled, 0f)
        hub(c, shade, i)
    }

    private fun hub(c: Canvas, shade: Float, i: Int) {
        // Press-fit rosette: eight teeth around a raised centre.
        paint.color = plastic(i, shade * 0.9f, 0xffd8dde1.toInt()); c.drawCircle(hubX, hubY, 18.5f, paint)
        paint.color = plastic(i, shade, 0xfff4f6f7.toInt())
        for (k in 0 until 8) {
            val a = k * Math.PI.toFloat() / 4f
            c.drawCircle(hubX + cos(a) * 11f, hubY + sin(a) * 11f, 3.8f, paint)
        }
        paint.color = plastic(i, shade * 0.82f, 0xffcfd5da.toInt()); c.drawCircle(hubX, hubY, 6.5f, paint)
        paint.color = plastic(i, shade * 1.18f); c.drawCircle(hubX - 2f, hubY - 2f, 3f, paint)
    }

    // ================================================================ disc
    /** Label side of the disc in its own flat coordinates, spun about the spindle. */
    private fun drawDisc(c: Canvas, x: Float, y: Float, r: Float, spinDeg: Float, rate: Float, i: Int, enabled: Boolean, sheen: Float) {
        val label = labels[i]
        val hole = 0.125f / actions[i].discScale; val ring = 0.27f / actions[i].discScale
        c.save()
        c.rotate(spinDeg, x, y)
        rect.set(x - r, y - r, x + r, y + r)
        paint.alpha = if (enabled) 255 else 170
        if (label != null) c.drawBitmap(label, null, rect, paint)
        // Fast spin: a few faint trailing copies read as rotational motion blur.
        if (label != null && rate > 400f) {
            val trail = min(1f, (rate - 400f) / 2400f)
            paint.alpha = (42 * trail).toInt()
            for (k in 1..2) { c.rotate(-rate / 240f * 0.33f, x, y); c.drawBitmap(label, null, rect, paint) }
        }
        paint.alpha = 255
        c.restore()
        // Metallic rim and clear inner ring stay fixed: they are rotationally symmetric.
        rimPaint.strokeWidth = 3f
        c.save(); c.translate(x, y); c.drawCircle(0f, 0f, r - 1.5f, rimPaint); c.restore()
        stroke.color = 0x55ffffff; stroke.strokeWidth = 2f; c.drawCircle(x, y, r * ring, stroke)
        stroke.color = 0x30000000; stroke.strokeWidth = 1.5f; c.drawCircle(x, y, r * hole, stroke)
        // A soft reflection band; fixed to the light, not to the spinning print.
        if (sheen > 0f) {
            path.reset(); path.addCircle(x, y, r - 1f, Path.Direction.CW); path.addCircle(x, y, r * hole, Path.Direction.CCW)
            c.save(); c.clipPath(path)
            sheenPaint.shader = sheenShader
            shadeMatrix.setScale(r * 1.15f, 1f); shadeMatrix.postRotate(-32f); shadeMatrix.postTranslate(x + (sheen - 0.5f) * r * 1.4f, y)
            sheenShader.setLocalMatrix(shadeMatrix)
            sheenPaint.alpha = 255
            c.drawRect(x - r, y - r, x + r, y + r, sheenPaint)
            c.restore()
        }
    }

    // Extraction timeline (seconds after the disc is chosen).
    private val RELEASE = 0.16f; private val LIFT_START = 0.16f; private val LIFT_END = 0.92f
    private val SPIN_FULL = 1.7f; private val MUSIC_FADE = 0.95f; private val LAUNCH_SOUND = 1.42f
    private val VEIL_START = 1.62f; private val COMMIT = 2.15f; private val RETURN_TIME = 0.55f
    private val discPose = FloatArray(5) // x, y, z, tilt, sheen

    /** Disc pose along the staged path: release, lift towards the viewer, settle centre-stage. */
    private fun pose(t: Float) {
        // Hub position in the opened case.
        val i = opened.coerceAtLeast(0)
        curX = caseX(i); curY = 8f; curZ = caseZ(i); curYaw = caseYaw(i); curLid = 0f
        casePoint(hubX, hubY, COVER, false, 0)
        val hx = world[0]; val hy = world[1]; val hz = world[2]
        val tx = 0f; val ty = -10f; val tz = -1120f
        val release = smooth((t / RELEASE).coerceIn(0f, 1f))
        val u = smooth(((t - LIFT_START) / (LIFT_END - LIFT_START)).coerceIn(0f, 1f))
        // Cubic Bezier: straight off the spindle first, then an arc to centre stage.
        val p0x = hx; val p0y = hy; val p0z = hz - 18f * release
        val p1x = hx; val p1y = hy - 30f; val p1z = hz - 230f
        val p2x = tx + 210f; val p2y = ty - 90f; val p2z = tz + 220f
        val a = (1 - u) * (1 - u) * (1 - u); val b = 3 * (1 - u) * (1 - u) * u; val cc = 3 * (1 - u) * u * u; val d = u * u * u
        val drift = ((t - LIFT_END) / (COMMIT - LIFT_END)).coerceIn(0f, 1f)
        discPose[0] = a * p0x + b * p1x + cc * p2x + d * tx
        discPose[1] = a * p0y + b * p1y + cc * p2y + d * ty
        discPose[2] = a * p0z + b * p1z + cc * p2z + d * tz - 70f * smooth(drift)
        discPose[3] = Math.toRadians((34.0 * u + 4.0 * smooth(drift))).toFloat()  // top tips away
        discPose[4] = (t * 0.55f) % 1.4f - 0.2f
    }

    private fun drawFreeDisc(c: Canvas, now: Long) {
        val i = opened.coerceAtLeast(0)
        val t = when (state) {
            State.RETURN_DISC -> (returnFrom * (1f - smooth(((now - stateAt) / 1000f / RETURN_TIME).coerceIn(0f, 1f))))
            State.COMMITTED -> COMMIT
            else -> (now - stateAt) / 1000f
        }
        pose(t)
        val x = discPose[0]; val y = discPose[1]; val z = discPose[2]; val tilt = discPose[3]
        val ct = cos(tilt); val st = sin(tilt)
        // Underside first, offset along the disc normal: its rim reads as the thin edge.
        val thickness = 6f
        val r = radius(i)
        discQuad(x + 0f, y + st * thickness, z + ct * thickness, ct, st, r)
        begin(c, 2 * r, 2 * r)
        c.save(); c.translate(r, r); c.drawCircle(0f, 0f, r, edgePaint); c.restore()
        c.restore()
        discQuad(x, y, z, ct, st, r)
        // Contact shadow on the tray while the disc is still near it.
        begin(c, 2 * r, 2 * r)
        drawDisc(c, r, r, r, spin, spinRate, i, true, discPose[4])
        c.restore()
    }

    private fun discQuad(x: Float, y: Float, z: Float, ct: Float, st: Float, r: Float) {
        // Square around the disc in its tilted plane: (u, v) -> (u, v*cos, -v*sin) about the X axis.
        val corners = floatArrayOf(-r, -r, r, -r, r, r, -r, r)
        for (n in 0..3) {
            val u = corners[n * 2]; val v = corners[n * 2 + 1]
            world[n * 3] = x + u; world[n * 3 + 1] = y + v * ct; world[n * 3 + 2] = z - v * st
            project(n * 3, n * 2)
        }
    }

    private fun discScreenBounds(): RectF {
        val i = opened.coerceAtLeast(0)
        curX = caseX(i); curY = 8f; curZ = caseZ(i); curYaw = caseYaw(i); curLid = 0f
        val r = radius(i)
        casePoint(hubX - r, hubY - r, COVER, false, 0); project(0, 0)
        casePoint(hubX + r, hubY + r, COVER, false, 3); project(3, 2)
        return RectF(min(quad[0], quad[2]), min(quad[1], quad[3]), max(quad[0], quad[2]), max(quad[1], quad[3]))
    }

    private fun drawVeil(c: Canvas, now: Long) {
        if (state != State.EXTRACT && state != State.COMMITTED) return
        val t = if (state == State.COMMITTED) COMMIT + (now - committedAt) / 1000f else (now - stateAt) / 1000f
        // Light swells from the spinning disc, then the whole screen settles to black for the game.
        val glow = smooth(((t - VEIL_START + 0.35f) / 0.5f).coerceIn(0f, 1f)) * (1f - smooth(((t - COMMIT + 0.15f) / 0.3f).coerceIn(0f, 1f)))
        if (glow > 0.01f) {
            paint.shader = RadialGradient(960f, 530f, 900f, intArrayOf(Color.argb((glow * 165).toInt(), 255, 255, 255), Color.argb((glow * 70).toInt(), 214, 232, 255), 0), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
            c.drawRect(0f, 0f, 1920f, 1080f, paint); paint.shader = null
        }
        val black = smooth(((t - VEIL_START) / (COMMIT - VEIL_START + 0.05f)).coerceIn(0f, 1f))
        paint.color = Color.argb((black * 255).toInt(), 0, 0, 0)
        c.drawRect(0f, 0f, 1920f, 1080f, paint)
    }

    // ================================================================ options + music setting
    private fun drawOptions(c: Canvas) {
        val list = optionEntries()
        // Keep added save/shortcut rows above the status/footer, including seven-row menus.
        val rowStep = minOf(132f, 520f / maxOf(1, (list.size + 1) / 2 - 1))
        // Eight or more rows: rows shorten so they never overlap.
        val rowHeight = minOf(84f, rowStep - 8f)
        list.forEachIndexed { index, entry ->
            val x = 180f + (index % 2) * 810f
            val y = 230f + (index / 2) * rowStep
            row(c, RectF(x, y, x + 750f, y + rowHeight), index, entry.label, entry.description)
        }
        if (musicPanel) drawMusicPanel(c)
        val message = status()
        if (message.isNotBlank()) text(c, message, 960f, 935f, 27f, 1590f, true)
    }

    private fun optionEntries(): List<Entry> = actions.drop(caseCount) + musicEntry
    private val musicEntry = Entry("Menu Music", "Set the volume of the menu theme or mute it.") { musicPanel = true; invalidate() }

    private fun row(c: Canvas, area: RectF, index: Int, label: String, description: String) {
        val focused = selection == index && !musicPanel
        panel(c, if (focused) "button_selected" else "button", area)
        val size = minOf(46f, area.height() * 0.55f)
        text(c, label, area.left + 38f, area.top + 12f * area.height() / 84f, size, area.width() - 76f)
        bounds.add(area to index)
        if (focused) text(c, description, 960f, 880f, 35f, 1560f, true)
    }

    private fun drawMusicPanel(c: Canvas) {
        paint.color = 0x99000000.toInt(); c.drawRect(0f, 164f, 1920f, 1000f, paint)
        val area = RectF(480f, 300f, 1440f, 820f)
        panel(c, "panel", area)
        val muted = ThorLauncherAudio.muted(context)
        val volume = ThorLauncherAudio.volume(context)
        text(c, "Menu Music", 960f, 340f, 58f, 820f, true)
        // Ten rounded Wii-style volume pips, filled in restrained blue.
        for (k in 0 until 10) {
            val x = 560f + k * 82f
            rect.set(x, 480f, x + 64f, 560f)
            paint.color = if (!muted && k < volume) 0xff2f8fe0.toInt() else 0xffd9dee3.toInt()
            c.drawRoundRect(rect, 18f, 18f, paint)
            stroke.color = 0x33000000; stroke.strokeWidth = 2f; c.drawRoundRect(rect, 18f, 18f, stroke)
        }
        text(c, if (muted) "Muted" else "Volume  $volume", 960f, 600f, 44f, 820f, true)
        text(c, if (ThorLauncherAudio.available()) "Menu music is ready" else "Music not staged on this device",
            960f, 690f, 30f, 820f, true)
    }

    // ================================================================ text/chrome helpers
    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, centered: Boolean = false) {
        if (value.isEmpty()) return
        if (ThorMenuTheme.wii(context)) ThorMenuTheme.text(c, value, x, y, size, width, centered)
        else art.text(c, value, x, y, size, width, centered)
    }

    private fun textAlpha(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, alpha: Int) {
        if (alpha <= 0 || value.isEmpty()) return
        if (ThorMenuTheme.wii(context)) { ThorMenuTheme.text(c, value, x, y, size, width, true, alpha = alpha); return }
        if (alpha >= 255) { art.text(c, value, x, y, size, width, true); return }
        val layer = c.saveLayerAlpha(x - width / 2f - 20f, y - 20f, x + width / 2f + 20f, y + size * 1.6f, alpha)
        art.text(c, value, x, y, size, width, true)
        c.restoreToCount(layer)
    }

    private fun panel(c: Canvas, role: String, area: RectF, alpha: Int = 255) {
        if (ThorMenuTheme.wii(context) && ThorMenuTheme.chrome(c, role, area, alpha)) return
        chrome[role]?.let {
            when (role) {
                "button", "button_selected" -> art.button(c, it, area, alpha)
                "panel" -> art.roundedImage(c, it, area, 24f, alpha)
                else -> art.image(c, it, area, alpha)
            }
        }
    }

    // ================================================================ interaction
    private fun setState(next: State) {
        state = next
        stateAt = SystemClock.uptimeMillis()
        publishPhase()
        describe()
        invalidate()
    }

    private fun setNotice(message: String) { notice = message; noticeAt = SystemClock.uptimeMillis(); publishPhase(); invalidate() }

    private fun publishPhase() {
        val product = (if (opened >= 0) opened else selection).takeIf { it < caseCount }
        val active = SystemClock.uptimeMillis() - noticeAt < 4200L && notice.isNotEmpty()
        val phase = when {
            options -> Phase.OPTIONS
            state == State.EXTRACT || state == State.COMMITTED -> Phase.LAUNCHING
            active -> Phase.BLOCKED
            state == State.OPEN || state == State.RETURN_DISC -> Phase.OPEN
            else -> Phase.BROWSE
        }
        onPhaseChanged(phase, product, if (active) notice else "")
    }

    private fun describe() {
        contentDescription = when {
            options -> "Options. Use the directional pad, A to select, B to go back."
            state == State.OPEN -> "${actions[opened.coerceAtLeast(0)].label} case open. A selects the disc, B closes the case."
            state == State.EXTRACT || state == State.COMMITTED -> "Starting ${actions[opened.coerceAtLeast(0)].label}."
            expanded >= 0 -> "${actions[bundles[expanded][0]].label} games. Left and right choose a game, A opens its case, B returns to all games."
            else -> "Select Game. Left and right choose a game, A opens its case or a bundle of games, X opens Options."
        }
    }

    /** The case one step [side] (-1/+1) from [i] on the current shelf, or [i] itself at an end. */
    private fun neighbour(i: Int, side: Int): Int {
        if (expanded >= 0 && bundleOf[i] == expanded) {
            val members = bundles[expanded]
            return members[(depthOf[i] + side).coerceIn(0, members.size - 1)]
        }
        return bundles[(bundleOf[i] + side).coerceIn(0, bundles.size - 1)][0]
    }

    /** A on a stack: its cases fan out with the base game at the front. */
    private fun expand(g: Int) {
        if (fanned != g) { sub.value = 0f; sub.velocity = 0f; sub.target = 0f }
        expanded = g; fanned = g
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.OPEN)
        select(bundles[g][0], sound = false)
        rememberBundle()
    }

    /** B on the sub-shelf: the cases fold back into their stack, which stays highlighted. */
    private fun collapse() {
        val g = expanded
        if (g < 0) return
        expanded = -1
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.CLOSE)
        lastCase = bundles[g][0]
        select(if (selection == caseCount) caseCount else bundles[g][0], sound = false)
        rememberBundle()
    }

    private fun rememberBundle() {
        lastBundle = expanded >= 0
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("bundle", expanded >= 0).apply()
    }

    private fun select(next: Int, sound: Boolean = true) {
        val count = if (options) optionEntries().size else caseCount + 1
        val value = next.coerceIn(0, count - 1)
        if (value != selection && sound) ThorLauncherAudio.play(ThorLauncherAudio.Sound.FOCUS)
        selection = value
        if (!options) remember(value)
        refreshSelection()
        describe()
        invalidate()
    }

    private fun remember(value: Int) {
        if (value == lastSelection) return
        lastSelection = value
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt("selection", value).apply()
    }

    private fun move(direction: Int) {
        if (options) {
            if (musicPanel) { adjustMusic(direction); return }
            select(selection + when (direction) {
                KeyEvent.KEYCODE_DPAD_DOWN -> 2
                KeyEvent.KEYCODE_DPAD_UP -> -2
                KeyEvent.KEYCODE_DPAD_LEFT -> if (selection % 2 == 1) -1 else 0
                else -> if (selection % 2 == 0) 1 else 0
            })
            return
        }
        when (state) {
            State.BROWSE -> select(when (direction) {
                KeyEvent.KEYCODE_DPAD_DOWN -> caseCount
                KeyEvent.KEYCODE_DPAD_UP -> if (selection == caseCount) lastCase else selection
                KeyEvent.KEYCODE_DPAD_LEFT -> if (selection == caseCount) caseCount else neighbour(selection, -1)
                else -> if (selection == caseCount) caseCount else neighbour(selection, 1)
            }.also { if (it < caseCount) lastCase = it })
            State.OPEN -> if (direction == KeyEvent.KEYCODE_DPAD_LEFT || direction == KeyEvent.KEYCODE_DPAD_RIGHT) {
                // Switching games closes this case first; the neighbouring one (on this shelf) is then highlighted.
                val from = opened
                closeCase()
                select(neighbour(from, if (direction == KeyEvent.KEYCODE_DPAD_LEFT) -1 else 1))
                lastCase = selection
            }
            else -> {}
        }
    }
    private var lastCase = selection.takeIf { it < caseCount } ?: 0

    private fun adjustMusic(direction: Int) {
        val v = ThorLauncherAudio.volume(context)
        val next = when (direction) { KeyEvent.KEYCODE_DPAD_LEFT -> v - 1; KeyEvent.KEYCODE_DPAD_RIGHT -> v + 1; else -> v }
        if (next != v && next in 0..10) {
            ThorLauncherAudio.setVolume(context, next)
            if (ThorLauncherAudio.muted(context)) ThorLauncherAudio.setMuted(context, false)
            ThorLauncherAudio.play(ThorLauncherAudio.Sound.FOCUS)
            invalidate()
        }
    }

    private fun openCase(i: Int) {
        if (!actions[i].enabled()) {
            // Missing game data keeps its existing guided setup flow.
            ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
            actions[i].action(); invalidate(); return
        }
        opened = i
        selection = i
        remember(i)
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.OPEN)
        setState(State.OPEN)
    }

    private fun closeCase() {
        if (opened < 0) return
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.CLOSE)
        setState(State.BROWSE)
    }

    private fun chooseDisc() {
        val i = opened
        if (i < 0 || state != State.OPEN) return
        val blocked = preflight(i)
        if (blocked != null) {
            ThorLauncherAudio.play(ThorLauncherAudio.Sound.REJECT)
            setNotice(blocked)
            return
        }
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
        musicFading = false; launchCue = false; spin = 0f; spinRate = 0f
        setState(State.EXTRACT)
    }

    private var returnFrom = 0f
    private fun cancelExtraction() {
        val t = (SystemClock.uptimeMillis() - stateAt) / 1000f
        if (t >= VEIL_START) return // Past the point of no return: the launch completes.
        returnFrom = t.coerceAtMost(LIFT_END)
        if (musicFading) { musicFading = false; thorMap?.let { ThorLauncherAudio.show(context, it) } }
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.CLOSE)
        setState(State.RETURN_DISC)
    }

    /** The one place the existing launch action runs. */
    private fun commit() {
        if (state != State.EXTRACT) return
        val i = opened
        val blocked = preflight(i)
        if (blocked != null) {
            ThorLauncherAudio.play(ThorLauncherAudio.Sound.REJECT)
            returnFrom = LIFT_END
            thorMap?.let { ThorLauncherAudio.show(context, it) }
            setState(State.RETURN_DISC)
            setNotice(blocked)
            return
        }
        committedAt = SystemClock.uptimeMillis()
        setState(State.COMMITTED)
        actions[i].action()
    }

    /**
     * The launch action reported that its game could not be opened (for example a separate
     * runtime that is not installed). Put the disc back in its open case, bring the menu music
     * back and explain, instead of waiting on a black screen. Also used for a closed case whose
     * game is unavailable.
     */
    fun launchFailed(message: String) {
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.REJECT)
        if (state == State.COMMITTED || state == State.EXTRACT) {
            spin = 0f; spinRate = 0f
            musicFading = false
            thorMap?.let { ThorLauncherAudio.show(context, it) }
            setState(State.OPEN)
        }
        setNotice(message)
    }

    private fun activate() {
        if (options) {
            if (musicPanel) {
                ThorLauncherAudio.setMuted(context, !ThorLauncherAudio.muted(context))
                ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT); invalidate(); return
            }
            UiSounds.playClick(context)
            val entry = optionEntries()[selection]
            if (entry.enabled()) entry.action()
            invalidate()
            return
        }
        when (state) {
            State.BROWSE -> when {
                selection == caseCount -> openOptions()
                expanded < 0 && stacked(selection) -> expand(bundleOf[selection])
                else -> openCase(selection)
            }
            State.OPEN -> chooseDisc()
            else -> {}
        }
    }

    private fun openOptions() {
        if (state != State.BROWSE) return
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
        options = true
        select(0, sound = false)
        publishPhase()
    }

    private fun openUpdates() {
        // Retro Rewind updates belong to the Mario Kart cases (and the Options button), not other games.
        val case = if (state == State.OPEN) opened else selection
        if (case < caseCount && !(actions[case].updatesShortcut && BuildConfig.THOR_RETRO_REWIND)) {
            // Every other case: Y shows that game's achievements (browsing the shelf only).
            if (state == State.BROWSE) onAchievements?.let { open ->
                ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
                open(case)
            }
            return
        }
        val entry = actions.firstOrNull { it.label == "Retro Rewind updates" } ?: return
        // Also from an open case: a blocked launch there tells the player to press Y.
        if (!entry.enabled() || (state != State.BROWSE && state != State.OPEN)) return
        ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
        entry.action()
        invalidate()
    }

    fun goBack(): Boolean {
        if (options) {
            if (musicPanel) { musicPanel = false; ThorLauncherAudio.play(ThorLauncherAudio.Sound.CLOSE); invalidate(); return true }
            options = false
            ThorLauncherAudio.play(ThorLauncherAudio.Sound.CLOSE)
            select(caseCount, sound = false)
            publishPhase()
            return true
        }
        return when (state) {
            State.OPEN -> { closeCase(); true }
            State.EXTRACT -> { cancelExtraction(); true }
            State.BROWSE -> if (expanded >= 0) { collapse(); true } else false
            else -> true
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT -> {
                // Held D-pad: one step, then a measured repeat (never a sound per frame).
                if (event.repeatCount == 0 || event.repeatCount % 4 == 0) move(keyCode)
            }
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> if (event.repeatCount == 0) activate()
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> if (event.repeatCount == 0) goBack()
            KeyEvent.KEYCODE_BUTTON_X -> if (!options && event.repeatCount == 0) openOptions() else if (options) return super.onKeyDown(keyCode, event)
            KeyEvent.KEYCODE_BUTTON_Y -> if (!options && event.repeatCount == 0) openUpdates() else if (options) return super.onKeyDown(keyCode, event)
            else -> return super.onKeyDown(keyCode, event)
        }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BACK -> true
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y -> if (!options) true else super.onKeyUp(keyCode, event)
        else -> super.onKeyUp(keyCode, event)
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        val x = event.getAxisValue(MotionEvent.AXIS_HAT_X).takeIf { abs(it) > 0.5f } ?: event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_HAT_Y).takeIf { abs(it) > 0.5f } ?: event.getAxisValue(MotionEvent.AXIS_Y)
        if (abs(x) < 0.5f && abs(y) < 0.5f) { lastAxis = 0; return true }
        val now = SystemClock.uptimeMillis()
        if (lastAxis == 0L || now - lastAxis > 260L) {
            move(if (abs(y) >= abs(x)) {
                if (y < 0) KeyEvent.KEYCODE_DPAD_UP else KeyEvent.KEYCODE_DPAD_DOWN
            } else if (x < 0) KeyEvent.KEYCODE_DPAD_LEFT else KeyEvent.KEYCODE_DPAD_RIGHT)
            lastAxis = now
        }
        return true
    }

    /** Pointer/touch only mirrors the explicit actions: pick a case, pick the visible disc. */
    override fun onTouchEvent(event: MotionEvent): Boolean {
        // The volume panel is controller-operated. Its backing Options rows are not touch
        // targets while covered; treating one as activate() would unexpectedly toggle mute.
        if (options && musicPanel) {
            downTarget = -1
            return true
        }
        val x = (event.x - offsetX) / scale
        val y = (event.y - offsetY) / scale
        val index = bounds.lastOrNull { it.first.contains(x, y) }?.second ?: -1
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downTarget = index
                // A side case in the carousel is first brought to the front, not opened.
                downWasFront = options || index == selection || index >= caseCount
                if (options) {
                    if (index in optionEntries().indices && !musicPanel) select(index)
                } else if (index in 0..caseCount && state == State.BROWSE) select(index)
            }
            MotionEvent.ACTION_UP -> {
                if (index >= 0 && index == downTarget) {
                    performClick()
                    when {
                        // Options have their own row indices; a later row must not be mistaken
                        // for the carousel's encoded disc target (10 + case index).
                        options -> if (index in optionEntries().indices) activate()
                        index >= 10 -> if (state == State.OPEN) chooseDisc()
                        state == State.BROWSE && downWasFront -> activate()
                    }
                }
                downTarget = -1
            }
            MotionEvent.ACTION_CANCEL -> downTarget = -1
        }
        return true
    }

    override fun performClick(): Boolean { super.performClick(); return true }

    // ================================================================ utilities
    private fun discLabel(source: Bitmap?, i: Int): Bitmap? {
        val size = 512
        val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(out)
        val p = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        if (source != null) {
            // Supplied disc art fills its square; crop to the printed disc.
            c.drawBitmap(source, null, RectF(0f, 0f, size.toFloat(), size.toFloat()), p)
        } else {
            p.shader = SweepGradient(256f, 256f, intArrayOf(0xffdfe5ea.toInt(), 0xffb8c2cb.toInt(), 0xfff3f6f8.toInt(), 0xffc3ccd4.toInt(), 0xffdfe5ea.toInt()), null)
            c.drawCircle(256f, 256f, 256f, p); p.shader = null
            banners[i]?.let { c.drawBitmap(it, null, RectF(40f, 200f, 472f, 300f), p) }
        }
        // Keep only the disc: outside the rim and the spindle hole become transparent.
        val clear = Paint(Paint.ANTI_ALIAS_FLAG).apply { xfermode = PorterDuffXfermode(PorterDuff.Mode.CLEAR) }
        val outside = Path().apply { fillType = Path.FillType.INVERSE_WINDING; addCircle(256f, 256f, 254f, Path.Direction.CW) }
        c.drawPath(outside, clear)
        c.drawCircle(256f, 256f, 256f * 0.125f / actions[i].discScale, clear)
        return out
    }

    companion object {
        /**
         * Survives Activity recreation so returning from a game keeps the same highlight. It is
         * also saved, because a separate game (NSMB Wii) can outlive this process.
         */
        private var lastSelection = -1
        /** Whether that highlight was on a bundle's sub-shelf; null until read. */
        private var lastBundle: Boolean? = null
        /** An 8 cm GameCube disc against the 12 cm Wii disc. */
        const val GAMECUBE_DISC = 8f / 12f
        private const val PREFS = "thor_launcher_library"

        private fun restoredSelection(context: Context, caseCount: Int): Int {
            if (lastSelection < 0) lastSelection = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt("selection", 0)
            return lastSelection.coerceIn(0, caseCount)
        }
        private fun restoredBundle(context: Context): Boolean =
            lastBundle ?: context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("bundle", false).also { lastBundle = it }
        private fun smooth(x: Float): Float { val t = x.coerceIn(0f, 1f); return t * t * t * (t * (t * 6f - 15f) + 10f) }
        private fun lerp(a: Float, b: Float, t: Float) = a + (b - a) * t
        private fun normalize(v: FloatArray): FloatArray { val l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); return floatArrayOf(v[0] / l, v[1] / l, v[2] / l) }
    }
}

/** Pure ordering shared by the Canvas painter and touch targets, exercised by host regressions. */
internal object ThorCasePaintOrder {
    fun closedShelf(expanded: Int, fan: Float, opened: Int): Boolean =
        expanded < 0 && kotlin.math.abs(fan) < 0.001f && opened < 0

    fun indices(bundles: List<List<Int>>, depths: FloatArray, closedShelf: Boolean): List<Int> {
        val byDepth = compareByDescending<Int> { depths[it] }.thenBy { it }
        if (!closedShelf) return depths.indices.sortedWith(byDepth)
        return bundles.sortedWith(compareByDescending<List<Int>> { depths[it.first()] }.thenBy { it.first() })
            .flatMap { members -> members.sortedWith(byDepth) }
    }
}
