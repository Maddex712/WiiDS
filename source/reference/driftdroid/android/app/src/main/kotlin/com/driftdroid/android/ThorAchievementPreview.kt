package com.driftdroid.android

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import com.driftdroid.android.achievements.AchievementCatalog
import com.driftdroid.android.achievements.AchievementText

/**
 * The lower library screen's achievement column (design canvas x 452..1196, y 288..812): the
 * selected game's RetroAchievements sets and earned count, its newest unlocks and what is
 * still locked, and a View all link. Pure drawing from an immutable [Data]; touch targets are
 * rebuilt each frame.
 */
internal class ThorAchievementPreview {
    /** One game's previews as read on a worker; [badges] are keyed by achievement ID ("none": empty state). */
    data class Data(val game: String, val snapshot: AchievementCatalog.Snapshot?, val badges: Map<String, Bitmap>, val failed: Boolean,
                    val refreshing: Boolean = false)

    /** (area, achievement ID or null for View all / the empty state), in design coordinates. */
    val hits = ArrayList<Pair<RectF, String?>>()
    var pressed: Pair<RectF, String?>? = null
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3.5f }
    private val area = RectF()
    private val left = 452f
    private val right = 1196f

    fun draw(c: Canvas, words: AchievementText, data: Data?, gameName: String) {
        hits.clear()
        words.draw(c, "Achievements", left, 288f, 42f, 520f, false)
        val pill = RectF(1010f, 290f, right, 340f)
        drawPill(c, pill, pressed != null && pressed?.second == null)
        words.draw(c, "View all", pill.centerX(), 299f, 31f, pill.width() - 24f, true)
        hits += RectF(pill) to null
        if (data == null) {
            // Loading: calm placeholders, never another game's cards.
            for (top in listOf(436f, 518f, 636f, 718f)) cardBack(c, RectF(left, top, right, top + 76f), 0x8a17222f.toInt())
            return
        }
        val snap = data.snapshot
        when {
            data.failed || snap == null -> return emptyCard(c, words, data, "Achievements could not be read",
                "Your game saves are not affected.", "Open View all to try again.")
            !snap.connected -> return emptyCard(c, words, data, "Connect RetroAchievements",
                "Shows $gameName's RetroAchievements sets", "with everything you have already earned.")
            snap.syncedAt <= 0 -> return emptyCard(c, words, data,
                if (data.refreshing) "Loading from RetroAchievements..." else "Not loaded yet",
                "Your achievements appear after the first refresh.", "Open View all to refresh now.")
            !snap.hasSets() -> return emptyCard(c, words, data, "No RetroAchievements set for $gameName",
                "RetroAchievements has no set for this game yet.", "WiiDS checks again when your progress refreshes.")
        }
        snap!!
        val sets = if (snap.sets.size > 1) "Base set + ${snap.sets.size - 1} subset${if (snap.sets.size > 2) "s" else ""}" else snap.sets[0].label()
        words.draw(c, "RetroAchievements  /  $sets", left, 346f, 26f, 500f, false)
        val count = "${snap.earned} / ${snap.total()} earned"
        words.draw(c, count, right - minOf(words.width(count, 28f), 230f), 346f, 28f, 230f, false)
        // The earned share of the whole list (counts only, never progress toward one achievement).
        area.set(left, 384f, right, 394f); paint.color = 0x66101820; c.drawRoundRect(area, 5f, 5f, paint)
        if (snap.earned > 0) {
            area.set(left, 384f, left + maxOf(10f, (right - left) * snap.earned / snap.total()), 394f)
            paint.color = 0xfff2c14e.toInt(); c.drawRoundRect(area, 5f, 5f, paint)
        }
        words.draw(c, "Recently earned", left, 406f, 26f, 400f, false)
        if (snap.recent.isEmpty()) {
            cardBack(c, RectF(left, 436f, right, 512f), 0xd9172231.toInt())
            words.draw(c, "Nothing earned yet", left + 28f, 446f, 30f, 680f, false)
            words.draw(c, "Achievements you earn on RetroAchievements appear here.", left + 28f, 482f, 22f, 690f, false)
        } else snap.recent.take(2).forEachIndexed { k, item -> card(c, words, data, item, 436f + k * 82f) }
        val upTop = if (snap.recent.size >= 2) 604f else 522f
        words.draw(c, if (snap.upcoming.isEmpty()) "Everything earned" else "Still to earn", left, upTop, 26f, 400f, false)
        val shown = if (snap.recent.size >= 2) 2 else 3
        val more = snap.upcoming.size - shown
        if (more > 0) { val tag = "+$more more"; words.draw(c, tag, right - words.width(tag, 24f), upTop + 2f, 24f, 200f, false) }
        snap.upcoming.take(shown).forEachIndexed { k, item ->
            val top = upTop + 32f + k * 82f
            if (top + 76f <= 812f) card(c, words, data, item, top)
        }
    }

    private fun card(c: Canvas, words: AchievementText, data: Data, item: AchievementCatalog.Item, top: Float) {
        val box = RectF(left, top, right, top + 76f)
        cardBack(c, box, if (pressed?.second == item.id) 0xf02a3a52.toInt() else 0xd9172231.toInt())
        paint.color = if (item.earned()) 0xfff2c14e.toInt() else 0xff8e979f.toInt()
        area.set(box.left, box.top + 12f, box.left + 6f, box.bottom - 12f); c.drawRoundRect(area, 3f, 3f, paint)
        data.badges[item.id]?.let { area.set(box.left + 14f, top + 3f, box.left + 84f, top + 73f); c.drawBitmap(it, null, area, paint) }
        val x = box.left + 98f
        val points = "${item.raPoints} pts"
        val pointsWidth = words.width(points, 22f)
        val width = box.right - 18f - x
        words.draw(c, item.title, x, top + 7f, 31f, width - pointsWidth - 16f, false)
        words.draw(c, points, box.right - 18f - pointsWidth, top + 13f, 22f, pointsWidth, false)
        val detail = if (item.earned()) "Earned " + java.text.DateFormat.getDateInstance(java.text.DateFormat.MEDIUM).format(java.util.Date(item.earnedAt)) +
            (if (item.hardcore()) "  (Hardcore)" else "") else item.description
        words.draw(c, fit(words, detail, 22f, width), x, top + 44f, 22f, width, false)
        hits += RectF(box) to item.id
    }

    private fun emptyCard(c: Canvas, words: AchievementText, data: Data, title: String, first: String, second: String) {
        val box = RectF(left, 380f, right, 800f)
        cardBack(c, box, 0xd9172231.toInt())
        data.badges["none"]?.let { area.set(box.centerX() - 80f, box.top + 34f, box.centerX() + 80f, box.top + 194f); c.drawBitmap(it, null, area, paint) }
        words.draw(c, title, box.centerX(), box.top + 222f, 32f, box.width() - 48f, true)
        words.draw(c, first, box.centerX(), box.top + 276f, 24f, box.width() - 56f, true)
        words.draw(c, second, box.centerX(), box.top + 312f, 24f, box.width() - 56f, true)
        hits += RectF(box) to null
    }

    private fun cardBack(c: Canvas, box: RectF, color: Int) {
        paint.color = 0x33000000; area.set(box.left, box.top + 4f, box.right, box.bottom + 5f); c.drawRoundRect(area, 18f, 18f, paint)
        paint.color = color; c.drawRoundRect(box, 18f, 18f, paint)
    }

    /** The launcher's gold selection edge on a dark pill. */
    private fun drawPill(c: Canvas, box: RectF, pressed: Boolean) {
        paint.color = if (pressed) 0xff3a4d68.toInt() else 0xf0172231.toInt()
        c.drawRoundRect(box, box.height() / 2f, box.height() / 2f, paint)
        stroke.shader = LinearGradient(0f, box.top, 0f, box.bottom, intArrayOf(0xfffff3b8.toInt(), 0xfff2c14e.toInt(), 0xffa86f0e.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(box, box.height() / 2f, box.height() / 2f, stroke)
        stroke.shader = null
    }

    private fun fit(words: AchievementText, value: String, height: Float, width: Float): String {
        if (words.width(value, height) <= width) return value
        var cut = value
        while (cut.isNotEmpty() && words.width("$cut...", height) > width) cut = cut.substringBeforeLast(' ', cut.dropLast(1))
        return "$cut..."
    }
}
