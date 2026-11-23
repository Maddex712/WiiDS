package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ImageDecoder
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.graphics.drawable.AnimatedImageDrawable
import com.driftdroid.android.dualscreen.WiiDsMenuArt
import dev.wiids.colosseum.ColosseumStorage
import java.io.File
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/** Draws WiiDS channel title cards using optional marks from the player's own game artwork. */
internal class WiiChannelArtwork {
    private enum class Pattern { TRACK, LAND, ORBIT, LIGHT }

    private data class Theme(
        val top: Int,
        val bottom: Int,
        val text: Int,
        val pattern: Pattern,
        val title: String,
        val subtitle: String = "",
        val mark: String? = null,
        val accent: String = "",
    )

    private data class ChannelMotion(
        val periodMs: Long,
        val driftX: Float,
        val driftY: Float,
        val zoom: Float,
        val logoLift: Float = 0f,
    )

    data class Art(
        val mark: Bitmap? = null,
        val kart: Bitmap? = null,
        val panel: Bitmap? = null,
        val backdrop: Bitmap? = null,
        val animation: AnimatedImageDrawable? = null,
        val sourceCrop: Rect? = null,
        val refined: Boolean = false,
        val photoShade: Int = 0,
        val previewBanner: Bitmap? = null,
    )

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val letters = Paint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG).apply {
        typeface = Typeface.create("sans-serif-condensed", Typeface.BOLD)
        textAlign = Paint.Align.CENTER
    }
    private val shape = Path()
    private val imageBounds = RectF()

    fun load(context: Context, entries: List<ThorLauncherView.Entry>): List<Art> {
        val folders = ThorLauncherFolders.wiiChannels(context)
        val marks = mutableMapOf<String, Bitmap?>()
        return entries.map { entry ->
            val artId = entry.art
            val previewBanner = bitmap(folders, "$artId-banner-hd", limit = 1280)
            val titlePanel = bitmap(folders, "$artId-channel-hd", limit = 1280)
            if (titlePanel != null) return@map Art(panel = titlePanel, previewBanner = previewBanner)

            val hdMark = bitmap(folders, "$artId-mark-hd")
            val hdBackdrop = bitmap(folders, "$artId-backdrop-2x", limit = 1280) ?: bitmap(folders, "$artId-backdrop-hd")
            if (hdMark != null && hdBackdrop != null) {
                // For Retro Rewind the creator's launcher image has an empty window in its center.
                // Its original colored border is the artwork behind the mark here.
                val crop = if (artId == "retro-rewind") sourceCrop(hdBackdrop, 0f, 0f, 0.082f, 1f) else null
                val shade = when (artId) {
                    "retro-rewind" -> 106
                    "mkdd-plus" -> 120
                    "kaizo" -> 24
                    else -> 0
                }
                return@map Art(mark = hdMark, backdrop = hdBackdrop, sourceCrop = crop, refined = true,
                    photoShade = shade, previewBanner = previewBanner)
            }
            val refinedPanel = bitmap(folders, "$artId-tile-2x")
            val hdPanel = refinedPanel ?: bitmap(folders, "$artId-tile-hd")
            val panel = hdPanel ?: bitmap(folders, "$artId-tile")
            if (panel != null) {
                val refinedAnimation = if (refinedPanel != null) animation(folders, "$artId-tile-2x") else null
                val animation = refinedAnimation ?: animation(folders, "$artId-tile-hd") ?: animation(folders, "$artId-tile")
                val crop = hdPanel?.let {
                    when (artId) {
                        "eclipse" -> sourceCrop(it, 0.066f, 0.09f, 0.935f, 0.79f)
                        "pokemon-xg" -> sourceCrop(it, 0.105f, 0.052f, 0.895f, 0.584f)
                        else -> null
                    }
                }
                return@map Art(panel = panel, animation = animation, sourceCrop = crop, previewBanner = previewBanner)
            }
            val mark = themes[artId]?.mark?.let { name ->
                if (!marks.containsKey(name)) {
                    marks[name] = when (name) {
                        "pokemon-colosseum" -> WiiDsMenuArt.colosseum(ColosseumStorage.iso(context))
                        else -> bitmap(folders, name)
                    }
                }
                marks[name]
            }
            Art(mark, if (artId == "original") bitmap(folders, "kart") else null,
                backdrop = bitmap(folders, "$artId-backdrop"), previewBanner = previewBanner)
        }
    }

    private fun sourceCrop(image: Bitmap, left: Float, top: Float, right: Float, bottom: Float) =
        Rect((image.width * left).toInt(), (image.height * top).toInt(),
            (image.width * right).toInt(), (image.height * bottom).toInt())

    private fun bitmap(folders: List<File>, name: String, limit: Int = 1024): Bitmap? =
        folders.firstNotNullOfOrNull { bitmap(it, name, limit) }

    private fun bitmap(folder: File, name: String, limit: Int): Bitmap? {
        return runCatching {
            val parent = folder.canonicalFile
            val file = File(parent, "$name.png")
            if (file.canonicalFile != file || !file.isFile || file.length() !in 8..4_194_304L) return null
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.path, bounds)
            if (bounds.outMimeType != "image/png" || bounds.outWidth !in 16..8192 || bounds.outHeight !in 16..8192) return null
            val largest = maxOf(bounds.outWidth, bounds.outHeight)
            val opts = BitmapFactory.Options().apply {
                inSampleSize = when {
                    largest > limit * 4 -> 8
                    largest > limit * 2 -> 4
                    largest > limit -> 2
                    else -> 1
                }
                inScaled = false
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            BitmapFactory.decodeFile(file.path, opts)?.takeIf { it.width in 16..limit && it.height in 16..limit }
        }.getOrNull()
    }

    private fun animation(folders: List<File>, name: String): AnimatedImageDrawable? =
        folders.firstNotNullOfOrNull { animation(it, name) }

    private fun animation(folder: File, name: String): AnimatedImageDrawable? {
        return runCatching {
            val parent = folder.canonicalFile
            val file = File(parent, "$name.webp")
            if (file.canonicalFile != file || !file.isFile || file.length() !in 12..25_165_824L) return null
            val source = ImageDecoder.createSource(file)
            (ImageDecoder.decodeDrawable(source) { _, info, _ ->
                require(info.isAnimated && info.mimeType == "image/webp" &&
                    info.size.width in 16..512 && info.size.height in 16..512)
            } as? AnimatedImageDrawable)?.apply {
                repeatCount = AnimatedImageDrawable.REPEAT_INFINITE
                val overscan = panelOverscan(intrinsicWidth, intrinsicHeight)
                setBounds(0, -overscan, 360, 180 + overscan)
            }
        }.getOrNull()
    }

    fun draw(canvas: Canvas, area: RectF, entry: ThorLauncherView.Entry, art: Art?, ready: Boolean,
             alpha: Int, motion: Float, loopMillis: Long = 0L, preview: Boolean = false) {
        val theme = themes[entry.art] ?: Theme(0xe7f1f6, 0x94b2c2, 0x25435a, Pattern.LIGHT, entry.label)
        val profile = channelMotion[entry.art]?.takeIf { loopMillis > 0L }
        val angle = profile?.let { 2.0 * PI * (loopMillis % it.periodMs) / it.periodMs } ?: 0.0
        val rise = ((1.0 - cos(angle)) / 2.0).toFloat()
        val logoLift = (profile?.logoLift ?: 0f) * rise
        canvas.save()
        canvas.translate(area.left, area.top)
        canvas.scale(area.width() / 360f, area.height() / 180f)
        if (preview) canvas.clipRect(0f, 0f, 360f, 180f)
        else {
            shape.reset()
            shape.addRoundRect(0f, 0f, 360f, 180f, 24f, 24f, Path.Direction.CW)
            canvas.clipPath(shape)
        }
        val banner = if (preview) art?.previewBanner else null
        if (banner != null) {
            imageBounds.set(0f, 0f, 360f, 180f)
            paint.color = Color.WHITE
            canvas.drawBitmap(banner, null, imageBounds, paint)
            canvas.restore()
            return
        }
        val panel = art?.panel
        if (panel != null) {
            val opacity = if (ready) alpha else alpha * 3 / 5
            val animation = art.animation
            if (!preview && animation?.isRunning == true) {
                if (animation.alpha != opacity) animation.alpha = opacity
                animation.draw(canvas)
            } else {
                canvas.save()
                moveBackdrop(canvas, profile, angle, rise)
                drawPanel(canvas, panel, art.sourceCrop, opacity)
                canvas.restore()
            }
            if (!ready && !preview) drawSetup(canvas, alpha)
            canvas.restore()
            return
        }

        canvas.save()
        moveBackdrop(canvas, profile, angle, rise)
        val backdrop = art?.backdrop
        if (backdrop != null) {
            val crop = art.sourceCrop
            if (crop == null) {
                val fit = maxOf(360f / backdrop.width, 180f / backdrop.height)
                val halfWidth = backdrop.width * fit / 2f
                val halfHeight = backdrop.height * fit / 2f
                imageBounds.set(180f - halfWidth, 90f - halfHeight, 180f + halfWidth, 90f + halfHeight)
            } else {
                imageBounds.set(0f, 0f, 360f, 180f)
            }
            paint.color = Color.WHITE
            paint.alpha = alpha
            canvas.drawBitmap(backdrop, crop, imageBounds, paint)
            if (art.photoShade > 0) {
                paint.color = Color.argb(art.photoShade * alpha / 255, 11, 18, 29)
                canvas.drawRect(0f, 0f, 360f, 180f, paint)
            }
        } else {
            paint.color = Color.WHITE
            paint.shader = LinearGradient(0f, 0f, 100f, 180f, color(theme.top, alpha), color(theme.bottom, alpha), Shader.TileMode.CLAMP)
            canvas.drawRect(0f, 0f, 360f, 180f, paint)
            paint.shader = null
            drawPattern(canvas, theme.pattern, alpha)
        }
        canvas.restore()
        val foregroundAlpha = if (ready) alpha else alpha * 3 / 5

        art?.kart?.let { kart ->
            val x = 98f + 36f * motion
            drawMark(canvas, kart, x, 136f, 133f, 84f, foregroundAlpha)
        }
        val mark = art?.mark
        if (mark != null) {
            // The Plus mark already includes its edition name.
            val accent = if (art.refined && entry.art == "mkdd-plus") "" else theme.accent
            val raised = accent.isNotEmpty() || art.kart != null
            val maxWidth = when {
                art.kart != null -> 230f
                art.refined -> 280f
                else -> 250f
            }
            drawMark(canvas, mark, if (art.kart == null) 180f else 199f, (if (raised) 74f else 90f) - logoLift,
                maxWidth, if (raised) 105f else 120f, foregroundAlpha)
            if (accent.isNotEmpty()) title(canvas, accent, 180f, 151f - logoLift, 26f, 278f, color(theme.text, foregroundAlpha))
        } else {
            val y = if (theme.subtitle.isEmpty()) 111f else 98f
            title(canvas, theme.title, 180f, y - logoLift, 54f, 307f, color(theme.text, foregroundAlpha))
            if (theme.subtitle.isNotEmpty()) title(canvas, theme.subtitle, 180f, 136f - logoLift, 27f, 292f, color(theme.text, foregroundAlpha))
        }
        if (!ready && !preview) drawSetup(canvas, alpha)
        canvas.restore()
    }

    private fun moveBackdrop(c: Canvas, profile: ChannelMotion?, angle: Double, rise: Float) {
        if (profile == null) return
        val x = profile.driftX * sin(angle).toFloat()
        val y = profile.driftY * rise
        val zoom = 1f + maxOf(abs(x) / 180f, abs(y) / 90f) + profile.zoom * rise
        c.translate(x, y)
        c.scale(zoom, zoom, 180f, 90f)
    }

    // The Wii icons are nearly 4:3. Use a small vertical crop to reduce stretching on the wide tile.
    private fun panelOverscan(width: Int, height: Int) = if (width * 100 < height * 155) 14 else 0

    private fun drawPanel(c: Canvas, bitmap: Bitmap, crop: Rect?, alpha: Int) {
        val overscan = panelOverscan(crop?.width() ?: bitmap.width, crop?.height() ?: bitmap.height).toFloat()
        imageBounds.set(0f, -overscan, 360f, 180f + overscan)
        paint.color = Color.WHITE
        paint.alpha = alpha
        c.drawBitmap(bitmap, crop, imageBounds, paint)
    }

    private fun drawSetup(c: Canvas, alpha: Int) {
        paint.color = color(0x172b36, alpha)
        c.drawRoundRect(254f, 141f, 348f, 171f, 10f, 10f, paint)
        title(c, "Set up", 301f, 163f, 21f, 86f, color(0xffffff, alpha))
    }

    private fun drawPattern(c: Canvas, pattern: Pattern, alpha: Int) {
        paint.color = color(0xffffff, alpha / 6)
        when (pattern) {
            Pattern.TRACK -> {
                c.save()
                c.rotate(-18f, 280f, 74f)
                for (row in 0..4) for (col in 0..6) {
                    if ((row + col) % 2 == 0) {
                        val x = 174f + col * 31f
                        val y = 38f + row * 31f
                        c.drawRect(x, y, x + 31f, y + 31f, paint)
                    }
                }
                c.restore()
            }
            Pattern.LAND -> {
                c.drawOval(-90f, 121f, 222f, 258f, paint)
                c.drawOval(135f, 114f, 414f, 275f, paint)
                c.drawOval(248f, 22f, 347f, 42f, paint)
                c.drawOval(269f, 11f, 319f, 42f, paint)
            }
            Pattern.ORBIT -> {
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = 2.5f
                for (index in 0..2) {
                    val span = index * 25f
                    c.drawOval(38f - span, 36f - span, 331f + span, 145f + span, paint)
                }
                paint.style = Paint.Style.FILL
                for (index in 0..11) {
                    c.drawCircle((index * 79 % 360).toFloat(), (index * 43 % 164).toFloat(), 1.5f + index % 3, paint)
                }
            }
            Pattern.LIGHT -> {
                c.save()
                c.rotate(27f, 328f, 44f)
                c.drawRect(262f, -50f, 306f, 265f, paint)
                c.drawRect(318f, -50f, 324f, 265f, paint)
                c.restore()
            }
        }
    }

    private fun drawMark(c: Canvas, bitmap: Bitmap, x: Float, y: Float, width: Float, height: Float, alpha: Int) {
        val fit = min(width / bitmap.width, height / bitmap.height)
        val halfWidth = bitmap.width * fit / 2f
        val halfHeight = bitmap.height * fit / 2f
        imageBounds.set(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight)
        paint.color = Color.WHITE
        paint.alpha = alpha
        c.drawBitmap(bitmap, null, imageBounds, paint)
    }

    private fun title(c: Canvas, value: String, x: Float, baseline: Float, size: Float, width: Float, color: Int) {
        letters.color = color
        letters.textSize = size
        val measured = letters.measureText(value)
        if (measured > width) letters.textSize = size * width / measured
        c.drawText(value, x, baseline, letters)
    }

    private fun color(rgb: Int, alpha: Int) = (alpha shl 24) or rgb

    companion object {
        fun hasChannelLoop(artId: String) = artId in channelMotion

        // Layered cards move the scene independently from the wordmark. Flat title art uses a slow camera move.
        private val channelMotion = mapOf(
            "sunshine" to ChannelMotion(4800L, -6f, 3f, 0.035f),
            "mkdd" to ChannelMotion(4600L, 5f, 0f, 0.025f),
            "pokemon-xd" to ChannelMotion(5600L, 4f, 1f, 0.025f),
            "pokemon-colosseum" to ChannelMotion(5800L, 4f, 0f, 0.026f),
            "retro-rewind" to ChannelMotion(3900L, 12f, -4f, 0.06f, 6f),
            "msmbwii" to ChannelMotion(5100L, -14f, 6f, 0.07f, 7f),
            "eclipse" to ChannelMotion(6200L, 8f, 4f, 0.067f, 4f),
            "kaizo" to ChannelMotion(6800L, 22f, -3f, 0.05f, 5f),
            "mkdd-plus" to ChannelMotion(4300L, -16f, 5f, 0.09f, 5f),
            "project-plus" to ChannelMotion(5300L, 14f, -5f, 0.055f, 6f),
            "pokemon-xg" to ChannelMotion(6100L, -5f, 2f, 0.04f, 5f),
        )
        private val themes = mapOf(
            "original" to Theme(0xf8fbfc, 0xc5dbeb, 0x21425e, Pattern.TRACK, "Mario Kart Wii", mark = "original"),
            "retro-rewind" to Theme(0x133086, 0x0f1744, 0xffef96, Pattern.TRACK, "Retro Rewind", "MARIO KART WII", "retro-rewind"),
            "nsmbw" to Theme(0xa4dfff, 0x4590cb, 0x102e45, Pattern.LAND, "New Super Mario", "BROS. WII", "nsmbw"),
            "msmbwii" to Theme(0x516ba3, 0x19284f, 0xffffff, Pattern.LAND, "MSMBWii", "JUST A LITTLE ADVENTURE", "msmbwii"),
            "sunshine" to Theme(0xfef5ba, 0x65becb, 0x193a58, Pattern.LAND, "Mario Sunshine", mark = "sunshine"),
            "eclipse" to Theme(0x72275d, 0x1b2763, 0xffdb9d, Pattern.ORBIT, "Mario Eclipse", "SUPER MARIO SUNSHINE"),
            "galaxy" to Theme(0x424b9e, 0x101d49, 0xffffff, Pattern.ORBIT, "Super Mario Galaxy", mark = "galaxy"),
            "kaizo" to Theme(0x17656b, 0x121b37, 0xc8fff3, Pattern.ORBIT, "Kaizo Mario Galaxy", "REBALANCED", "kaizo", "Rebalanced"),
            "brawl" to Theme(0x5e7597, 0x18283e, 0xffffff, Pattern.LIGHT, "Super Smash Bros.", "BRAWL", "brawl"),
            "mkdd" to Theme(0xb9e6fa, 0x298795, 0x163747, Pattern.TRACK, "Double Dash!!", "MARIO KART", "mkdd"),
            "mkdd-plus" to Theme(0x3f787f, 0x112b4e, 0xffef99, Pattern.TRACK, "Double Dash!! Plus", "MARIO KART", "mkdd", "PLUS"),
            "project-plus" to Theme(0x307769, 0x132340, 0xc9ffda, Pattern.LIGHT, "Project+", "SUPER SMASH BROS. BRAWL"),
            "pokemon-xd" to Theme(0x6a3f78, 0x2c1742, 0xffeeb8, Pattern.ORBIT, "Pokémon XD", "GALE OF DARKNESS", "pokemon-xd"),
            "pokemon-xg" to Theme(0x233f58, 0x0d1b33, 0xc1f4ff, Pattern.ORBIT, "Pokémon XG", "NEXT GEN", "pokemon-xg"),
            "pokemon-colosseum" to Theme(0x6a3a56, 0x361c35, 0xffe9ad, Pattern.LIGHT, "Pokémon", "COLOSSEUM", "pokemon-colosseum"),
            "city-folk" to Theme(0xa8d7e2, 0x419677, 0x254e37, Pattern.LAND, "Animal Crossing", "LET’S GO TO THE CITY", "city-folk"),
        )
    }
}
