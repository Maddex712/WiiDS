package com.driftdroid.android

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface

/**
 * The Thor menus' look. [Style.MARIO_KART] is the original: the Mario Kart Wii menu art and font
 * read from the player's own disc. [Style.WII_MENU] redraws the same chrome roles (background,
 * header, footer, button, button_selected, panel) and the text in the light grey, white and Wii
 * blue of the Wii Menu, with the system font; layouts, cases and discs stay the same.
 */
internal object ThorMenuTheme {
    enum class Style(val label: String) { MARIO_KART("Mario Kart Wii"), WII_MENU("Wii Menu") }

    private const val PREFS = "thor_menu_theme"
    private const val KEY = "style"

    @Volatile private var cached: Style? = null

    fun current(context: Context): Style = cached ?: runCatching {
        Style.valueOf(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY, null) ?: "")
    }.getOrDefault(Style.MARIO_KART).also { cached = it }

    fun next(context: Context): Style {
        val styles = Style.values()
        val next = styles[(current(context).ordinal + 1) % styles.size]
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY, next.name).apply()
        cached = next
        return next
    }

    fun wii(context: Context) = current(context) == Style.WII_MENU

    // ------------------------------------------------------------ Wii Menu drawing
    const val WII_BLUE = 0xff34beed.toInt()
    const val TEXT = 0xff4f5459.toInt()
    const val TEXT_SOFT = 0xff8a9097.toInt()

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val type = Paint(Paint.ANTI_ALIAS_FLAG or Paint.SUBPIXEL_TEXT_FLAG).apply {
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    }
    private val path = Path()
    private val box = RectF()

    private fun alpha(color: Int, alpha: Int) = (color and 0x00ffffff) or (((color ushr 24) * alpha / 255) shl 24)

    /** A vertical gradient fill; the paint's own colour is reset so its alpha cannot dim the shader. */
    private fun gradient(top: Float, bottom: Float, from: Int, to: Int) {
        fill.color = 0xffffffff.toInt()
        fill.shader = LinearGradient(0f, top, 0f, bottom, from, to, Shader.TileMode.CLAMP)
    }

    /** Draws chrome [role] into [area]; false for a role this style leaves to the original art. */
    fun chrome(c: Canvas, role: String, area: RectF, alpha: Int = 255): Boolean {
        when (role) {
            "background" -> {
                gradient(area.top, area.bottom, alpha(0xfff8f9fb.toInt(), alpha), alpha(0xffdce1e6.toInt(), alpha))
                c.drawRect(area, fill); fill.shader = null
                // The Wii Menu's faint horizontal pinstripes.
                fill.color = alpha(0x0d5a6470, alpha)
                var y = area.top
                while (y < area.bottom) { c.drawRect(area.left, y, area.right, y + 2f, fill); y += 8f }
            }
            "header" -> {
                fill.color = alpha(0x14000000, alpha)
                c.drawRect(area.left, area.bottom - 2f, area.right, area.bottom + 10f, fill)
                gradient(area.top, area.bottom, alpha(0xffffffff.toInt(), alpha), alpha(0xfff0f3f6.toInt(), alpha))
                c.drawRect(area, fill); fill.shader = null
                fill.color = alpha(WII_BLUE, alpha)
                c.drawRect(area.left, area.bottom - 6f, area.right, area.bottom, fill)
            }
            "footer" -> {
                // The Wii Menu's grey bottom bar: low at the sides, raised in a soft curve in the middle.
                val w = area.width(); val lowTop = area.top + area.height() * 0.32f
                path.reset()
                path.moveTo(area.left, lowTop)
                path.lineTo(area.left + w * 0.24f, lowTop)
                path.cubicTo(area.left + w * 0.31f, lowTop, area.left + w * 0.30f, area.top, area.left + w * 0.37f, area.top)
                path.lineTo(area.left + w * 0.63f, area.top)
                path.cubicTo(area.left + w * 0.70f, area.top, area.left + w * 0.69f, lowTop, area.left + w * 0.76f, lowTop)
                path.lineTo(area.right, lowTop)
                path.lineTo(area.right, area.bottom); path.lineTo(area.left, area.bottom); path.close()
                gradient(area.top, area.bottom, alpha(0xffe9ecef.toInt(), alpha), alpha(0xffbfc5cb.toInt(), alpha))
                c.drawPath(path, fill); fill.shader = null
                line.color = alpha(0xffffffff.toInt(), alpha); line.strokeWidth = 3f
                c.drawPath(path, line)
            }
            "button", "button_selected" -> {
                val r = area.height() * 0.32f
                box.set(area); box.offset(0f, 4f)
                fill.color = alpha(0x1f000000, alpha); c.drawRoundRect(box, r, r, fill)
                gradient(area.top, area.bottom, alpha(0xffffffff.toInt(), alpha), alpha(0xffeaedf0.toInt(), alpha))
                c.drawRoundRect(area, r, r, fill); fill.shader = null
                if (role == "button_selected") {
                    line.color = alpha(0x5534beed, alpha); line.strokeWidth = 16f; c.drawRoundRect(area, r, r, line)
                    line.color = alpha(WII_BLUE, alpha); line.strokeWidth = 6f; c.drawRoundRect(area, r, r, line)
                } else {
                    line.color = alpha(0xffbfc6cc.toInt(), alpha); line.strokeWidth = 3f; c.drawRoundRect(area, r, r, line)
                }
            }
            "panel" -> {
                box.set(area); box.offset(0f, 6f)
                fill.color = alpha(0x1a000000, alpha); c.drawRoundRect(box, 28f, 28f, fill)
                fill.color = alpha(0xffffffff.toInt(), alpha); c.drawRoundRect(area, 28f, 28f, fill)
                line.color = alpha(0xffc9cfd4.toInt(), alpha); line.strokeWidth = 2f; c.drawRoundRect(area, 28f, 28f, line)
            }
            else -> return false
        }
        return true
    }

    /**
     * Text in the box the original font would use: [y] is the top of a line [size] tall, and the
     * text shrinks to fit [width].
     */
    fun text(c: Canvas, value: String, x: Float, y: Float, size: Float, width: Float, center: Boolean,
             color: Int = TEXT, alpha: Int = 255) {
        if (value.isEmpty() || alpha <= 0) return
        type.textSize = size * 0.74f
        val measured = type.measureText(value)
        if (measured > width && measured > 0f) type.textSize *= width / measured
        type.color = alpha(color, alpha)
        type.textAlign = if (center) Paint.Align.CENTER else Paint.Align.LEFT
        c.drawText(value, x, y + size * 0.78f, type)
    }
}
