package com.driftdroid.android

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.RectF
import android.os.SystemClock
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import org.json.JSONObject
import java.io.File
import java.util.Locale

/** Original disc artwork/glyphs, with stable physical-button targets and bounded text. */
internal class ThorRetroRewindUpdatesView(
    context: Context, folder: File,
    private val check: () -> Unit,
    private val update: () -> Unit,
    private val back: () -> Unit,
) : View(context) {
    private val art = ThorMenuArtwork(folder)
    private val chrome = JSONObject(File(folder, "menu.json").readText()).getJSONObject("images")
    private val images = listOf("background", "header", "footer", "panel", "button", "button_selected")
        .associateWith { art.bitmap(chrome.getJSONObject(it).getString("texture")) }
    private var snapshot = ThorRetroRewindUpdateStatus.snapshot
    private var selection = 0
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var lastMove = 0L
    private val buttons = (0..2).map { RectF(180f + it * 540f, 814f, 660f + it * 540f, 904f) }

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        requestFocus()
    }

    fun update(next: ThorRetroRewindUpdateStatus.Snapshot) {
        snapshot = next
        contentDescription = "Retro Rewind updates. Installed ${next.currentVersion ?: "unknown"}. " +
            "Latest ${next.latestVersion ?: "unknown"}. ${next.message}. A select, B back."
        invalidate()
    }

    private fun image(c: Canvas, role: String, area: RectF) {
        val bitmap = images.getValue(role)
        when (role) {
            "button", "button_selected" -> art.button(c, bitmap, area)
            "panel" -> art.roundedImage(c, bitmap, area, 28f)
            else -> art.image(c, bitmap, area)
        }
    }
    private fun text(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, center: Boolean = false) =
        art.text(c, value, x, y, size, width, center)

    override fun onWindowFocusChanged(hasWindowFocus: Boolean) {
        super.onWindowFocusChanged(hasWindowFocus)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Color.BLACK)
        scale = minOf(width / 1920f, height / 1080f)
        offsetX = (width - 1920f * scale) / 2f
        offsetY = (height - 1080f * scale) / 2f
        canvas.save()
        canvas.translate(offsetX, offsetY)
        canvas.scale(scale, scale)
        image(canvas, "background", RectF(0f, 0f, 1920f, 1080f))
        image(canvas, "header", RectF(0f, 40f, 1920f, 164f))
        text(canvas, "Retro Rewind updates", 140f, 70f, 62f, 1640f)
        if (hasWindowFocus()) drawContent(canvas)
        image(canvas, "footer", RectF(0f, 1000f, 1920f, 1080f))
        text(canvas, "A  Select", 140f, 1015f, 38f, 500f)
        text(canvas, "B  Back", 1600f, 1015f, 38f, 500f)
        canvas.restore()
    }

    private fun drawContent(c: Canvas) {
        image(c, "panel", RectF(180f, 216f, 1740f, 770f))
        val info = listOf("Installed" to (snapshot.currentVersion ?: "Not detected"),
            "Latest release" to (snapshot.latestVersion ?: "Not checked"),
            "Compatible release" to (snapshot.compatibleVersion ?: "Not checked"))
        info.forEachIndexed { index, (label, value) ->
            val y = 250f + index * 65f
            text(c, label, 240f, y, 36f, 520f)
            text(c, value, 820f, y, 39f, 850f)
        }
        // Nothing installed yet: this page installs Retro Rewind rather than updating it.
        val fresh = snapshot.currentVersion == null
        val headline = when (snapshot.state.toString()) {
            "CHECKING" -> "Checking for updates"
            "READY" -> when {
                !snapshot.canUpdate -> "Retro Rewind updates"
                snapshot.currentVersion == null -> "Ready to install"
                else -> "An update is ready"
            }
            "UP_TO_DATE" -> "You're up to date"
            "BLOCKED" -> when {
                ThorRetroRewindUpdateService.needsRecovery(context) -> "Recover the installation"
                snapshot.currentVersion == null -> "Retro Rewind can't be installed yet"
                else -> "A compatible build is needed"
            }
            "ERROR" -> if (fresh) "The installation didn't finish" else "The update didn't finish"
            "DOWNLOADING" -> if (fresh) "Downloading Retro Rewind" else "Downloading update"
            "VERIFYING" -> "Verifying download"
            "STAGING" -> if (fresh) "Installing Retro Rewind" else "Preparing update"
            "COMMITTING" -> if (fresh) "Finishing installation" else "Finishing update"
            "COMPLETE" -> "Retro Rewind is ready"
            "CANCELLED" -> if (fresh) "Installation paused" else "Update paused"
            else -> "Retro Rewind updates"
        }
        text(c, headline, 240f, 460f, 46f, 1440f)
        wrap(nativeMessage(snapshot.message), 1400f, 34f).take(3).forEachIndexed { index, line ->
            text(c, line, 240f, 530f + index * 44f, 34f, 1440f)
        }
        if (snapshot.totalBytes > 0 && snapshot.state in setOf(
                ThorRetroRewindUpdateStatus.State.DOWNLOADING, ThorRetroRewindUpdateStatus.State.VERIFYING)) {
            val track = RectF(240f, 675f, 1680f, 705f)
            image(c, "button", track)
            val fraction = (snapshot.downloadedBytes.toDouble() / snapshot.totalBytes).coerceIn(0.0, 1.0).toFloat()
            if (fraction > 0f) {
                c.save(); c.clipRect(track.left, track.top, track.left + track.width() * fraction, track.bottom)
                image(c, "button_selected", track); c.restore()
            }
            text(c, "${megabytes(snapshot.downloadedBytes)} / ${megabytes(snapshot.totalBytes)} MB", 960f, 718f, 30f, 1400f, true)
        } else if (snapshot.filesDone > 0) {
            text(c, "${snapshot.filesDone} files prepared", 240f, 700f, 32f, 1400f)
        }
        val labels = listOf(if (snapshot.state.toString() in setOf("ERROR", "CANCELLED")) "Retry check" else "Check for updates",
            if (snapshot.running) "Cancel" else if (ThorRetroRewindUpdateService.needsRecovery(context)) "Recover"
                else if (snapshot.currentVersion == null) "Install" else "Update now", "Back")
        buttons.forEachIndexed { index, area ->
            image(c, if (selection == index && enabled(index)) "button_selected" else "button", area)
            text(c, labels[index], area.centerX(), area.top + 18f, 40f, area.width() - 60f, true)
        }
        text(c, when {
            snapshot.running -> if (snapshot.currentVersion == null) "You can leave this page while Retro Rewind installs."
                else "You can leave this page while the update continues."
            snapshot.state.toString() == "CANCELLED" -> "Check again to resume a saved download."
            snapshot.currentVersion == null -> "Downloads from Retro Rewind's official server."
            else -> "Updates preserve saved progress and custom files."
        }, 960f, 935f, 30f, 1600f, true)
    }

    private fun enabled(index: Int): Boolean = when (index) {
        0 -> !snapshot.running
        1 -> if (snapshot.running) snapshot.state.toString() != "COMMITTING" else snapshot.canUpdate ||
            ThorRetroRewindUpdateService.needsRecovery(context)
        else -> true
    }
    private fun activate(index: Int) {
        if (!enabled(index)) return
        when (index) { 0 -> check(); 1 -> update(); else -> back() }
    }
    private fun move(delta: Int) { selection = (selection + delta + buttons.size) % buttons.size; invalidate() }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_UP -> { move(-1); true }
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN -> { move(1); true }
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> true
            else -> super.onKeyDown(keyCode, event)
        }
    }
    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean = when (keyCode) {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER -> { activate(selection); true }
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BACK -> { back(); true }
        else -> super.onKeyUp(keyCode, event)
    }
    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        val x = event.getAxisValue(MotionEvent.AXIS_HAT_X).takeIf { kotlin.math.abs(it) > 0.5f }
            ?: event.getAxisValue(MotionEvent.AXIS_X)
        val y = event.getAxisValue(MotionEvent.AXIS_HAT_Y).takeIf { kotlin.math.abs(it) > 0.5f }
            ?: event.getAxisValue(MotionEvent.AXIS_Y)
        val value = if (kotlin.math.abs(x) > kotlin.math.abs(y)) x else y
        if (kotlin.math.abs(value) > 0.55f && SystemClock.uptimeMillis() - lastMove > 180L) {
            move(if (value > 0) 1 else -1); lastMove = SystemClock.uptimeMillis(); return true
        }
        return super.onGenericMotionEvent(event)
    }
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val index = buttons.indexOfFirst { it.contains((event.x - offsetX) / scale, (event.y - offsetY) / scale) }
        if (index < 0) return true
        if (event.action == MotionEvent.ACTION_DOWN) { selection = index; invalidate() }
        if (event.action == MotionEvent.ACTION_UP) { performClick(); activate(index) }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }

    private fun wrap(value: String, width: Float, height: Float): List<String> {
        val lines = mutableListOf<String>(); var line = ""
        value.replace('\n', ' ').split(Regex("\\s+")).filter { it.isNotEmpty() }.forEach { word ->
            val candidate = if (line.isEmpty()) word else "$line $word"
            if (line.isNotEmpty() && art.width(candidate, height) > width) { lines += line; line = word }
            else line = candidate
        }
        if (line.isNotEmpty()) lines += line
        return lines
    }
    private fun megabytes(bytes: Long): String = String.format(Locale.ROOT, "%.1f", bytes / 1048576.0)

    companion object {
        fun nativeMessage(value: String): String = value.replace("\u2026", "...")
            .replace('\u2014', '-').replace('\u2013', '-').replace('\u2019', '\'').replace('\u2018', '\'')
    }
}
