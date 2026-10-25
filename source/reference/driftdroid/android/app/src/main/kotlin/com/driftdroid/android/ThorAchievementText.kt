package com.driftdroid.android

import android.content.Context
import android.graphics.Canvas
import com.driftdroid.android.achievements.AchievementText
import java.io.File

/**
 * Achievement text in the launcher's own outlined menu font (WiiCompiled/ThorMap, built from the
 * player's disc). Characters the font lacks fall back to a plain outlined face for that line.
 */
class ThorAchievementText private constructor(private val font: ThorMenuArtwork) : AchievementText {
    private val plain = AchievementText.Plain()

    override fun draw(canvas: Canvas, value: String, x: Float, y: Float, height: Float, maxWidth: Float, center: Boolean) {
        if (value.isEmpty()) return
        if (supported(value)) font.text(canvas, value, x, y, height, maxWidth, center)
        else plain.draw(canvas, value, x, y, height, maxWidth, center)
    }

    override fun width(value: String, height: Float): Float =
        if (supported(value)) font.width(value, height) else plain.width(value, height)

    private fun supported(value: String) = value.all { it in ' '..'~' }

    companion object {
        /** The menu font when its art is present, else the plain fallback. Never throws. */
        @JvmStatic
        fun create(context: Context): AchievementText = runCatching<AchievementText> {
            ThorAchievementText(ThorMenuArtwork(File(context.filesDir, "WiiCompiled/ThorMap")))
        }.getOrElse { AchievementText.Plain() }

        internal fun of(font: ThorMenuArtwork): AchievementText = ThorAchievementText(font)
    }
}
