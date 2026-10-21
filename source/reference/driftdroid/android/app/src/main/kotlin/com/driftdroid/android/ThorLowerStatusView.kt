package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.view.View
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/** Quiet JNI-free lower-screen companion, using the owner's unchanged Wii art. */
class ThorLowerStatusView @JvmOverloads constructor(
    context: Context,
    folder: File,
    private val productTitle: String = "Mario Kart Wii",
) : View(context) {
    private data class Artwork(val font: ThorMenuArtwork, val images: Map<String, Bitmap>)
    private var artwork: Artwork? = null
    private var title = "Course Map"
    private var message = "Continue on the upper screen."
    private var closed = false
    private var changed: Runnable? = null
    private val bounds = RectF()
    private val cacheKey = folder.absolutePath
    private val loaded: (Artwork?) -> Unit = { art ->
        if (!closed) { artwork = art; redraw() }
    }

    init {
        isFocusable = false
        isClickable = false
        updateDescription()
        requestArtwork(cacheKey, folder, loaded)
    }

    fun setStatus(title: String, message: String) {
        if (closed || (this.title == title && this.message == message)) return
        this.title = title
        this.message = message
        updateDescription()
        redraw()
    }

    fun setOnContentChanged(callback: Runnable?) { changed = callback }

    fun close() {
        closed = true
        changed = null
        waiters[cacheKey]?.remove(loaded)
    }

    private fun updateDescription() { contentDescription = "$productTitle. $title. $message" }
    private fun redraw() { invalidate(); changed?.run() }

    override fun onDraw(canvas: Canvas) = render(canvas, width, height)

    /** Also used by the race View while the verified map is not drawable. */
    fun render(canvas: Canvas, width: Int, height: Int) {
        // This neutral backing is visible only while the private art is loading
        // or absent. Never substitute a system font or fabricated game art.
        canvas.drawColor(Color.rgb(24, 29, 37))
        if (width <= 0 || height <= 0) return
        val art = artwork ?: return
        val w = width.toFloat(); val h = height.toFloat()
        art.images["background"]?.let { image ->
            val scale = maxOf(w / image.width, h / image.height)
            val dx = (w - image.width * scale) * .5f
            val dy = (h - image.height * scale) * .5f
            bounds.set(dx, dy, w - dx, h - dy)
            art.font.image(canvas, image, bounds)
        }
        val unit = minOf(w / 1280f, h / 1080f)
        val headerHeight = minOf(h * .17f, w * 107f / 691f)
        art.images["header"]?.let {
            bounds.set(0f, h * .09f, w, h * .09f + headerHeight)
            art.font.image(canvas, it, bounds)
        }
        art.font.text(canvas, productTitle, w * .5f, h * .09f + (headerHeight - 56f * unit) * .5f,
            56f * unit, w * .84f, true)
        art.images["panel"]?.let {
            bounds.set(w * .07f, h * .35f, w * .93f, h * .70f)
            art.font.roundedImage(canvas, it, bounds, 28f * unit)
        }
        art.font.text(canvas, title, w * .5f, h * .43f, 68f * unit, w * .78f, true)
        val messageHeight = 34f * unit
        val lines = ThorLowerStatus.messageLines(message, w * .78f) { art.font.width(it, messageHeight) }
        for ((index, line) in lines.withIndex()) {
            art.font.text(canvas, line, w * .5f, h * .55f + index * messageHeight * 1.3f,
                messageHeight, w * .78f, true)
        }
        // Original footer pixels are quiet framing, with no actions or toolbar.
        art.images["footer"]?.let {
            val footerHeight = minOf(h * .10f, w * 102f / 662f)
            bounds.set(0f, h - footerHeight, w, h)
            art.font.image(canvas, it, bounds)
        }
    }

    companion object {
        private val main = Handler(Looper.getMainLooper())
        private val worker = Executors.newSingleThreadExecutor { task ->
            Thread(task, "ThorLowerArtwork").apply { isDaemon = true }
        }
        // One app-private art folder per process. Shared immutable pixels avoid
        // re-decoding on every Home/return; all View and font drawing stays on UI.
        private val cached = HashMap<String, Artwork>()
        private val waiters = HashMap<String, MutableList<(Artwork?) -> Unit>>()
        private fun requestArtwork(key: String, folder: File, ready: (Artwork?) -> Unit) {
            cached[key]?.let { ready(it); return }
            waiters[key]?.let { it.add(ready); return }
            waiters[key] = mutableListOf(ready)
            worker.execute {
                val art = runCatching {
                    val font = ThorMenuArtwork(folder)
                    val images = LinkedHashMap<String, Bitmap>()
                    // The original font remains useful even when menu chrome is missing.
                    runCatching {
                        val menu = JSONObject(File(folder, "menu.json").readText())
                        require(menu.getInt("schema") == 1)
                        val roles = menu.getJSONObject("images")
                        for (role in listOf("background", "header", "panel", "footer")) {
                            runCatching { font.bitmap(roles.getJSONObject(role).getString("texture")) }
                                .getOrNull()?.let { images[role] = it }
                        }
                    }
                    Artwork(font, images)
                }.getOrNull()
                main.post {
                    if (art != null) cached[key] = art
                    waiters.remove(key)?.forEach { it(art) }
                }
            }
        }
    }
}
