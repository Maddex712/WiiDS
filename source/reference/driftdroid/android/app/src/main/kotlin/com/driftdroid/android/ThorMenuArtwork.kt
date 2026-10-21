package com.driftdroid.android

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.Rect
import android.graphics.RectF
import org.json.JSONObject
import java.io.File

/** Private, unmodified disc art. No font or game image is bundled in the application. */
internal class ThorMenuArtwork(private val folder: File) {
    private val font = JSONObject(File(folder, "hud.json").readText()).getJSONObject("fonts").getJSONObject("main")
    private val glyphs = font.getJSONObject("glyphs")
    private val atlas = bitmap(font.getString("texture"))
    private val lineHeight = font.getDouble("lineHeight").toFloat()
    private val ascent = font.getDouble("ascent").toFloat()
    private val source = Rect()
    private val target = RectF()
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val cornerPath = Path()
    private val cornerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        xfermode = PorterDuffXfermode(PorterDuff.Mode.DST_OUT)
    }

    fun bitmap(name: String): Bitmap {
        require(File(name).name == name)
        return requireNotNull(BitmapFactory.decodeFile(File(folder, name).path)) { "Missing private menu art: $name" }
    }

    private data class TextBounds(val left: Float, val right: Float) {
        val width get() = right - left
    }

    private fun textBounds(text: String): TextBounds {
        var cursor = 0f
        var left = 0f
        var right = 0f
        for (char in text) {
            val glyph = glyphs.optJSONObject(char.toString()) ?: glyphs.getJSONObject("?")
            val inkLeft = cursor + glyph.getDouble("left").toFloat()
            left = minOf(left, inkLeft)
            right = maxOf(right, inkLeft + glyph.getInt("w"))
            cursor += glyph.getDouble("advance").toFloat()
        }
        return TextBounds(left, maxOf(right, cursor))
    }

    fun width(text: String, height: Float): Float = textBounds(text).width * height / lineHeight

    fun text(canvas: Canvas, text: String, x: Float, y: Float, height: Float, maxWidth: Float = Float.MAX_VALUE, center: Boolean = false) {
        val extents = textBounds(text)
        val requested = extents.width * height / lineHeight
        val fit = if (requested > maxWidth) maxWidth / requested else 1f
        val scale = height / lineHeight * fit
        // Reserve the entire original outlined glyph, including negative left
        // bearings and right overhang, in both Canvas fitting and Android spans.
        var cursor = x - (if (center) requested * fit / 2f else 0f) - extents.left * scale
        for (char in text) {
            val glyph = glyphs.optJSONObject(char.toString()) ?: glyphs.getJSONObject("?")
            val gx = glyph.getInt("x"); val gy = glyph.getInt("y")
            val w = glyph.getInt("w"); val h = glyph.getInt("h")
            source.set(gx, gy, gx + w, gy + h)
            val left = cursor + glyph.getDouble("left").toFloat() * scale
            target.set(left, y, left + w * scale, y + h * scale)
            canvas.drawBitmap(atlas, source, target, paint)
            cursor += glyph.getDouble("advance").toFloat() * scale
        }
    }

    fun image(canvas: Canvas, bitmap: Bitmap, bounds: RectF, alpha: Int = 255) {
        paint.alpha = alpha
        canvas.drawBitmap(bitmap, null, bounds, paint)
        paint.alpha = 255
    }

    /** Original pixels, with an antialiased presentation mask rather than edited art. */
    fun roundedImage(canvas: Canvas, bitmap: Bitmap, bounds: RectF, radius: Float, alpha: Int = 255) {
        rounded(canvas, bounds, radius) { image(canvas, bitmap, bounds, alpha) }
    }

    /** Keep bevel/end-cap width tied to height, independent of the length of the label. */
    fun button(canvas: Canvas, bitmap: Bitmap, bounds: RectF, alpha: Int = 255) {
        // The extracted frame includes transparent padding and its authored shadow.
        // Account for those gutters so the visible bevel also has a soft corner.
        rounded(canvas, bounds, bounds.height() * 0.28f) {
            val cap = minOf(bitmap.width / 4, bitmap.height / 2).coerceAtLeast(1)
            val edge = minOf(bounds.width() / 2f, cap * bounds.height() / bitmap.height)
            paint.alpha = alpha
            fun piece(left: Int, right: Int, x: Float, width: Float) {
                if (width <= 0f) return
                source.set(left, 0, right, bitmap.height)
                target.set(x, bounds.top, x + width, bounds.bottom)
                canvas.drawBitmap(bitmap, source, target, paint)
            }
            piece(0, cap, bounds.left, edge)
            piece(cap, bitmap.width - cap, bounds.left + edge, bounds.width() - 2f * edge)
            piece(bitmap.width - cap, bitmap.width, bounds.right - edge, edge)
            paint.alpha = 255
        }
    }

    private inline fun rounded(canvas: Canvas, bounds: RectF, radius: Float, draw: () -> Unit) {
        if (bounds.isEmpty) return
        val layer = canvas.saveLayer(bounds, null)
        draw()
        // An inverse path removes only the corners, including their soft edge coverage.
        // Raster pixels and their original alpha remain unchanged inside the outline.
        cornerPath.reset()
        cornerPath.fillType = Path.FillType.INVERSE_EVEN_ODD
        cornerPath.addRoundRect(bounds, radius, radius, Path.Direction.CW)
        canvas.drawPath(cornerPath, cornerPaint)
        canvas.restoreToCount(layer)
    }

    /** Word spans keep Android's wrapping, focus, selection and accessibility behavior. */
    fun styledText(value: CharSequence): CharSequence {
        val output = android.text.SpannableString(value)
        output.getSpans(0, output.length, NativeSpan::class.java).forEach { output.removeSpan(it) }
        Regex("\\S+").findAll(value).forEach { word ->
            // Normal words wrap as words. Paths and hashes can break into small runs.
            val chunk = if (word.value.length > 16) 8 else word.value.length
            var start = word.range.first
            while (start <= word.range.last) {
                val end = minOf(start + chunk, word.range.last + 1)
                output.setSpan(NativeSpan(), start, end, android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                start = end
            }
        }
        return output
    }

    private inner class NativeSpan : android.text.style.ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            val height = paint.textSize * 1.42f
            fm?.apply {
                ascent = -kotlin.math.ceil(height * this@ThorMenuArtwork.ascent / lineHeight).toInt()
                descent = kotlin.math.ceil(height * (lineHeight - this@ThorMenuArtwork.ascent) / lineHeight).toInt()
                top = ascent; bottom = descent
            }
            return kotlin.math.ceil(width(text.subSequence(start, end).toString(), height)).toInt()
        }
        override fun draw(canvas: Canvas, text: CharSequence, start: Int, end: Int, x: Float, top: Int, y: Int, bottom: Int, paint: Paint) {
            val height = paint.textSize * 1.42f
            this@ThorMenuArtwork.text(canvas, text.subSequence(start, end).toString(), x,
                y - height * ascent / lineHeight, height)
        }
    }
}
