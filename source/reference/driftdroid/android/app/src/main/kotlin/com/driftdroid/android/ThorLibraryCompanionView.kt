package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import com.driftdroid.android.achievements.AchievementBadges
import com.driftdroid.android.achievements.AchievementCatalog
import com.driftdroid.android.achievements.AchievementText
import com.driftdroid.android.achievements.RaLibrary
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * Lower-screen companion to the physical-case launcher. It never repeats the upper cover:
 * it shows what the upper screen cannot - availability, the actual installed Retro Rewind
 * version, previously checked update facts, saved video options and the buttons that apply
 * to the current step - and a calm, intentional state while a game starts.
 * Read-only and passive; gameplay and updater presentations keep their own lower views.
 */
internal class ThorLibraryCompanionView(context: Context, private val folder: File) : View(context) {
    data class Selection(
        val product: Int?,
        val options: Boolean,
        val available: Boolean,
        val description: String,
        val status: String,
        val phase: ThorLauncherView.Phase = ThorLauncherView.Phase.BROWSE,
        val message: String = "",
    )

    private data class Artwork(val font: ThorMenuArtwork, val images: Map<String, Bitmap>)
    private data class VideoSettings(val resolution: String, val frameRate: String, val renderer: String)
    private var selection = Selection(0, false, true, "", "")
    private var artwork: Artwork? = null
    private var installedVersion: String? = null
    private var videoSettings: VideoSettings? = null
    private var snapshot = ThorRetroRewindUpdateStatus.snapshot
    private var pendingUpdate = false
    private var recoveryRequired = false
    private var closed = false
    private var phaseAt = SystemClock.uptimeMillis()
    private val area = RectF()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    /** Touching a preview card or View all: (game id, achievement id or null). */
    var onOpenAchievements: ((String, String?) -> Unit)? = null
    /** The selected game's previews; replaced only by the latest request for that game. */
    private var preview: ThorAchievementPreview.Data? = null
    private val previewArt = ThorAchievementPreview()
    private var previewGame: String? = null
    private val latest = AchievementCatalog.Latest()
    private var achievementFont: Pair<ThorMenuArtwork, AchievementText>? = null
    private var designScale = 1f
    private var designLeft = 0f
    private var designTop = 0f
    // Keyed by the case manifest too, so cases composed after an import replace the cached set.
    // The player's own cases when present (ThorLauncherFolders), else those built from the discs.
    private val cases = ThorLauncherFolders.cases(context, folder)
    private var cacheKey = artKey()
    private fun artKey() = folder.absolutePath + "|" + cases.absolutePath + ":" + File(cases, "artwork.json").let { "${it.lastModified()}:${it.length()}" }
    /** MSMBWii also needs its mod, which installs itself once the NSMB Wii disc is imported. */
    private var nsmbDiscImported = false
    private val fallback = ThorLowerStatusView(context, folder)
    private val loaded: (Artwork?) -> Unit = { loadedArt ->
        if (!closed) { artwork = loadedArt; invalidate() }
    }
    private val poll = object : Runnable {
        override fun run() {
            if (closed || !isAttachedToWindow) return
            if (refreshMetadata()) { updateDescription(); invalidate() }
            // Cases composed while this screen is showing (art built after an import).
            val key = artKey()
            if (key != cacheKey) {
                waiters[cacheKey]?.remove(loaded)
                cacheKey = key
                requestArtwork(cacheKey, folder, cases, loaded)
            }
            postDelayed(this, 1000L)
        }
    }

    init {
        isFocusable = false
        isFocusableInTouchMode = false
        fallback.setStatus("Game library", "Choose a game on the upper screen.")
        fallback.setOnContentChanged(Runnable { if (!closed) invalidate() })
        requestArtwork(cacheKey, folder, cases, loaded)
    }

    fun setSelection(value: Selection) {
        if (closed) return
        if (value.phase != selection.phase) phaseAt = SystemClock.uptimeMillis()
        selection = value
        val game = if (value.options) null else gameId(value.product)
        if (game != previewGame) { previewGame = game; preview = null; previewArt.pressed = null; requestAchievements(game) }
        nsmbDiscImported = com.driftdroid.android.rom.GameCatalog.Disc.NSMB_USA_REV1.isInstalled(context)
        refreshMetadata()
        updateDescription()
        invalidate()
    }

    /** Cache small local update facts away from Canvas drawing; never starts a catalog request. */
    private fun refreshMetadata(): Boolean {
        val next = ThorRetroRewindUpdateStatus.snapshot
        val pending = BuildConfig.THOR_RETRO_REWIND && ThorUpdateRestart.requested(context)
        val recovery = BuildConfig.THOR_RETRO_REWIND && ThorRetroRewindUpdateService.needsRecovery(context)
        val version = runCatching {
            File(folder.parentFile, "RetroRewind6/version.txt").takeIf { it.length() in 1..128 }
                ?.readText()?.trim()?.takeIf { it.matches(Regex("[0-9]+(?:\\.[0-9]+){1,3}")) }
        }.getOrNull()
        val video = readVideoSettings()
        val changed = snapshot != next || pendingUpdate != pending ||
            recoveryRequired != recovery || installedVersion != version || videoSettings != video
        snapshot = next
        pendingUpdate = pending
        recoveryRequired = recovery
        installedVersion = version
        videoSettings = video
        return changed
    }

    /** A bounded read-only view of saved options, never a frame-rate measurement. */
    private fun readVideoSettings(): VideoSettings? = runCatching {
        val file = File(folder.parentFile, "Config.toml")
        if (!file.isFile || file.length() > 65536L) return@runCatching null
        val bytes = ByteArray(65537)
        var length = 0
        file.inputStream().use { input ->
            while (length < bytes.size) {
                val count = input.read(bytes, length, bytes.size - length)
                if (count < 0) break
                length += count
            }
        }
        if (length > 65536) return@runCatching null
        val values = HashMap<String, String>()
        var video = false
        String(bytes, 0, length, Charsets.UTF_8).lineSequence().forEach { raw ->
            val line = raw.substringBefore('#').trim()
            if (line.startsWith('[')) video = line == "[video]"
            else if (video && '=' in line) {
                val key = line.substringBefore('=').trim()
                if (key in setOf("resolution_multiplier", "frame_interpolation_fps", "graphics_api"))
                    values[key] = line.substringAfter('=').trim()
            }
        }
        if (values.isEmpty()) return@runCatching null
        val multiplier = values["resolution_multiplier"]?.takeIf { it.length <= 32 }?.toBigDecimalOrNull()
            ?.takeIf { it.scale() in -8..16 && it.precision() <= 32 }
        val resolution = when {
            multiplier == null || multiplier.signum() < 0 -> "Resolution unavailable"
            multiplier.signum() == 0 -> "1080p"
            else -> "${multiplier.stripTrailingZeros().toPlainString()}x resolution"
        }
        val fps = values["frame_interpolation_fps"]?.toIntOrNull()
        // 120 is presentation interpolation only; the game itself always runs at 60 Hz.
        val frameRate = when (fps) {
            null -> "Frame rate unavailable"
            120, 180 -> "120 FPS interpolated"
            else -> "60 FPS"
        }
        val renderer = when (values["graphics_api"]?.removeSurrounding("\"")) {
            null -> "Graphics unavailable"
            "auto" -> "Automatic graphics"
            "vulkan" -> "Vulkan"
            "opengles" -> "OpenGL ES"
            else -> "Graphics: custom"
        }
        VideoSettings(resolution, frameRate, renderer)
    }.getOrNull()

    private fun updateDescription() {
        val title = if (selection.options || selection.product == null) "Game library options" else productTitle()
        contentDescription = "$title. ${readiness()}. " +
            (if (selection.product == 1) installedVersion?.let { "Retro Rewind $it. " }.orEmpty() else "") +
            selection.message
    }

    /** Separate games validate their own data and never inherit Mario Kart update/video state. */
    private fun separateGame() = (selection.product == NSMB || selection.product == SUNSHINE ||
        selection.product == MSMBWII || selection.product == ECLIPSE || selection.product == GALAXY || selection.product == KAIZO) && !selection.options
    /** MSMBWii's disc is there but its mod isn't (yet). */
    private fun modPending() = selection.product == MSMBWII && nsmbDiscImported && !selection.available
    private fun separateGameName() = when (selection.product) { KAIZO -> "Kaizo Rebalanced"; GALAXY -> "Galaxy"; SUNSHINE -> "Sunshine"; ECLIPSE -> "Eclipse"; MSMBWII -> "MSMBWii"; else -> "NSMB Wii" }

    private fun updateGated() = BuildConfig.THOR_RETRO_REWIND && !separateGame() &&
        (snapshot.running || pendingUpdate || recoveryRequired)

    private fun readiness(): String = when {
        // Imported is all the library can know; each game checks its disc data itself.
        separateGame() -> when {
            selection.product == ECLIPSE -> if (selection.available) "Ready to open" else "App not installed"
            selection.available -> "Ready to play"
            modPending() -> "Mod not installed"
            else -> "Disc not imported"
        }
        snapshot.running && snapshot.state == ThorRetroRewindUpdateStatus.State.CHECKING -> "Checking updates"
        snapshot.running -> "Update in progress"
        recoveryRequired -> "Recovery required"
        pendingUpdate -> "Update pending"
        selection.available -> "Ready to play"
        else -> "Game data required"
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        removeCallbacks(poll)
        post(poll)
    }

    override fun onDetachedFromWindow() {
        removeCallbacks(poll)
        super.onDetachedFromWindow()
    }

    fun close() {
        closed = true
        removeCallbacks(poll)
        fallback.close()
        waiters[cacheKey]?.remove(loaded)
    }

    private fun productTitle() = when (selection.product) {
        1 -> "Retro Rewind"
        NSMB -> "New Super Mario Bros. Wii"
        SUNSHINE -> "Super Mario Sunshine"
        ECLIPSE -> "Super Mario Eclipse"
        GALAXY -> "Super Mario Galaxy"
        KAIZO -> "Kaizo Mario Galaxy Rebalanced"
        MSMBWII -> "MSMBWii: Just a Little Adventure"
        else -> "Mario Kart Wii"
    }

    /** Stable game identity for the library case index (FrontendGame IDs, never carousel state). */
    private fun gameId(product: Int?): String? = when (product) {
        0 -> FrontendGame.MARIO_KART.id
        1 -> FrontendGame.RETRO_REWIND.id
        NSMB -> FrontendGame.NSMB.id
        MSMBWII -> FrontendGame.MSMBWII.id
        SUNSHINE -> FrontendGame.SUNSHINE.id
        ECLIPSE -> FrontendGame.ECLIPSE.id
        GALAXY -> FrontendGame.GALAXY.id
        KAIZO -> FrontendGame.KAIZO.id
        else -> null
    }

    private fun artPrefix() = when (selection.product) {
        1 -> "retro-rewind"
        NSMB -> "nsmbw"
        SUNSHINE -> "sunshine"
        ECLIPSE -> "eclipse"
        GALAXY -> "galaxy"
        KAIZO -> "kaizo"
        MSMBWII -> "msmbwii"
        else -> "original"
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Color.rgb(24, 29, 37))
        if (width <= 0 || height <= 0) return
        val art = artwork ?: run { fallback.render(canvas, width, height); return }
        val scale = minOf(width / 1280f, height / 1080f)
        designScale = scale; designLeft = (width - 1280f * scale) / 2f; designTop = (height - 1080f * scale) / 2f
        if (selection.options || selection.product == null || selection.phase == ThorLauncherView.Phase.LAUNCHING) previewArt.hits.clear()
        canvas.save()
        canvas.translate((width - 1280f * scale) / 2f, (height - 1080f * scale) / 2f)
        canvas.scale(scale, scale)
        val font = art.font
        fun image(role: String, left: Float, top: Float, right: Float, bottom: Float, rounded: Boolean = false, alpha: Int = 255) {
            art.images[role]?.let {
                area.set(left, top, right, bottom)
                if (rounded) font.roundedImage(canvas, it, area, 26f, alpha) else font.image(canvas, it, area, alpha)
            }
        }
        fun text(value: String, x: Float, y: Float, height: Float, width: Float, center: Boolean = false) {
            if (value.isNotEmpty()) font.text(canvas, value, x, y, height, width, center)
        }
        image("background", 0f, 0f, 1280f, 1080f)
        image("header", 0f, 60f, 1280f, 230f)
        val phase = selection.phase
        val product = selection.product
        val title = when {
            selection.options || product == null -> "Game Library"
            phase == ThorLauncherView.Phase.LAUNCHING -> "Starting ${productTitle()}"
            else -> productTitle()
        }
        text(title, 640f, 112f, 60f, 1100f, true)

        when {
            selection.options || product == null -> drawOptions(canvas, ::image, ::text)
            phase == ThorLauncherView.Phase.LAUNCHING -> drawLaunching(canvas, art, ::image, ::text)
            else -> drawDetails(canvas, art, ::image, ::text)
        }
        image("footer", 0f, 996f, 1280f, 1080f)
        canvas.restore()
        if (phase == ThorLauncherView.Phase.LAUNCHING) postInvalidateOnAnimation()
    }

    private fun drawOptions(c: Canvas, image: (String, Float, Float, Float, Float, Boolean, Int) -> Unit,
                            text: (String, Float, Float, Float, Float, Boolean) -> Unit) {
        image("panel", 54f, 276f, 1226f, 810f, true, 255)
        val later = listOf("msmbwii", "sunshine", "eclipse", "galaxy", "kaizo").filter { artwork?.images?.containsKey("$it-cover") == true }
        if (later.isNotEmpty()) {
            // The cases share the collection area (x 96..582, y 286..624) without moving its panels:
            // one row of up to six, then two rows.
            val games = listOf("original", "retro-rewind", "nsmbw") + later
            val rows = if (games.size > 6) 2 else 1
            val perRow = (games.size + rows - 1) / rows
            val w = minOf((486f - (perRow - 1) * 10f) / perRow, (338f - (rows - 1) * 12f) / rows * 112f / 160f)
            val h = w * 160f / 112f
            val top = 455f - (rows * h + (rows - 1) * 12f) / 2
            for ((i, game) in games.withIndex()) {
                val row = i / perRow
                val inRow = minOf(perRow, games.size - row * perRow)
                val x = 96f + (486f - (inRow * w + (inRow - 1) * 10f)) / 2 + (i % perRow) * (w + 10f)
                val y = top + row * (h + 12f)
                image("$game-cover", x, y, x + w, y + h, false, 255)
            }
        } else if (artwork?.images?.containsKey("nsmbw-cover") == true) {
            // Three games: the same covers, smaller, in the same space.
            image("original-cover", 96f, 360f, 250f, 580f, false, 255)
            image("retro-rewind-cover", 262f, 360f, 416f, 580f, false, 255)
            image("nsmbw-cover", 428f, 360f, 582f, 580f, false, 255)
        } else {
            image("original-cover", 106f, 332f, 310f, 622f, false, 255)
            image("retro-rewind-cover", 330f, 332f, 536f, 622f, false, 255)
        }
        text("Your game collection", 322f, 674f, 35f, 442f, true)
        text(if (updateGated()) readiness() else "Options", 594f, 326f, 48f, 544f, false)
        text("Graphics and game data", 594f, 414f, 34f, 544f, false)
        text("Menu music and updates", 594f, 472f, 34f, 544f, false)
        text(installedVersion?.let { "Retro Rewind $it installed" } ?: "Retro Rewind not installed", 594f, 536f, 32f, 544f, false)
        text(if (selection.options) "Choose an option above." else "A  Open Options", 594f, 656f, 30f, 544f, false)
        text(if (selection.options) "B returns to your cases." else "Up  Return to game cases", 594f, 708f, 30f, 544f, false)
        footerHints(text, if (selection.options) "D-pad  Choose     A  Select     B  Back" else "A  Options     Up  Game cases")
    }

    private fun drawDetails(c: Canvas, art: Artwork, image: (String, Float, Float, Float, Float, Boolean, Int) -> Unit,
                            text: (String, Float, Float, Float, Float, Boolean) -> Unit) {
        val open = selection.phase == ThorLauncherView.Phase.OPEN
        val blocked = selection.phase == ThorLauncherView.Phase.BLOCKED && selection.message.isNotEmpty()
        image("panel", 54f, 270f, 1226f, 830f, true, 255)
        // Left: the selected disc as it sits in the case, with a soft contact shadow.
        val prefix = artPrefix()
        val discX = 242f; val discY = 448f; val r = 150f * discScale(prefix)
        paint.shader = RadialGradient(discX + 8f, discY + 14f, r + 26f, intArrayOf(0x55000000, 0x00000000), floatArrayOf(0.8f, 1f), Shader.TileMode.CLAMP)
        c.drawCircle(discX + 8f, discY + 14f, r + 26f, paint); paint.shader = null
        val disc = art.images["$prefix-disc-round"]
        if (disc != null) { area.set(discX - r, discY - r, discX + r, discY + r); c.drawBitmap(disc, null, area, paint) }
        else image("$prefix-cover", discX - 104f, discY - 148f, discX + 104f, discY + 148f, false, 255)
        // Under the disc: availability with a restrained blue (ready) or grey marker, then compact facts.
        val ready = selection.available && !updateGated()
        val label = readiness()
        val labelWidth = minOf(art.font.width(label, 36f), 330f)
        paint.color = if (ready) 0xff2f8fe0.toInt() else 0xff9aa3ab.toInt()
        c.drawCircle(discX - labelWidth / 2f - 8f, 654f, 11f, paint)
        text(label, discX + 12f, 634f, 36f, 330f, true)
        facts().take(3).forEachIndexed { k, value -> text(value, discX, 690f + k * 40f, 27f, 352f, true) }
        // Right: this game's achievements.
        drawAchievements(c, art)
        // The step-specific prompt sits in a quiet strip under the panel.
        val achievementsKey = achievementsButton()
        val (headline, hints) = if (separateGame()) when {
            // Retro Rewind updates (Y) are not offered for this game; Y opens its achievements.
            blocked -> selection.message to "X  Options     B  Close case"
            open -> "Disc ready" to "A  Select disc     B  Close case"
            // MSMBWii plays on the NSMB Wii disc (its included mod installs itself).
            !selection.available -> when {
                modPending() -> "Open Your games to set up MSMBWii"
                selection.product == ECLIPSE -> "Install the Eclipse Thor app to play"
                selection.product == KAIZO -> "Set up Kaizo Rebalanced on Your games"
                selection.product == GALAXY -> "Set up Super Mario Galaxy to play"
                else -> "Import your ${if (selection.product == MSMBWII) "NSMB Wii" else separateGameName()} disc to play"
            } to "Left / Right  Choose a game     X  Options$achievementsKey"
            else -> (selection.status.trim().ifEmpty { "Left / Right  Choose a game" }) to "A  Open case     X  Options$achievementsKey"
        } else when {
            blocked -> selection.message to (if (BuildConfig.THOR_RETRO_REWIND) "Y  Updates     X  Options     B  Close case" else "X  Options     B  Close case")
            open -> "Disc ready" to "A  Select disc     B  Close case"
            !selection.available -> "Game data required" to "A  Set up game data     X  Options$achievementsKey"
            else -> (selection.status.trim().ifEmpty { "Left / Right  Choose a game" }) to
                (if (BuildConfig.THOR_RETRO_REWIND) "A  Open case     X  Options     Y  Updates" else "A  Open case     X  Options$achievementsKey")
        }
        val lines = ThorLowerStatus.messageLines(headline, 1100f) { art.font.width(it, if (blocked) 32f else 38f) }
        lines.take(2).forEachIndexed { k, l -> text(l, 640f, 852f + k * 44f, if (blocked) 32f else 38f, 1100f, true) }
        footerHints(text, hints)
    }

    /** Y opens achievements wherever it is not Retro Rewind's Updates button (ThorLauncherView). */
    private fun achievementsButton() =
        if (onOpenAchievements == null || (!separateGame() && BuildConfig.THOR_RETRO_REWIND)) "" else "     Y  Achievements"

    /** The secondary technical lines, compacted so achievements have room. ASCII for the menu font. */
    private fun facts(): List<String> {
        val video = videoSettings?.let { "${it.resolution} / ${it.frameRate} / ${it.renderer}" } ?: "Video settings unavailable"
        return when {
            separateGame() -> listOf(when {
                selection.product == KAIZO -> if (selection.available) "Rebalanced 2.1 ready" else "Set up Rebalanced first"
                selection.product == GALAXY -> if (selection.available) "Galaxy ready" else "Set up Galaxy first"
                selection.product == ECLIPSE -> if (selection.available) "Eclipse app installed" else "Install the Eclipse Thor app"
                selection.available -> "Disc imported"
                modPending() -> "Finish it on Your games"
                else -> "Import it on Your games"
            }, when (selection.product) {
                KAIZO -> "Own Galaxy disc + official 2.1 patch"
                GALAXY -> "USA Wii disc"
                SUNSHINE -> "USA GameCube disc"
                ECLIPSE -> "Separate game data and saves"
                MSMBWII -> "Mod included, on your NSMB Wii disc"
                else -> "USA Wii disc, revision 1"
            }, "Checked by the game when it starts")
            selection.product == 1 -> {
                val latest = snapshot.latestVersion
                val compatible = snapshot.compatibleVersion
                listOf(installedVersion?.let { "Installed  $it" } ?: "Pack not installed", when {
                    latest == null -> "Updates not checked yet"
                    latest == installedVersion -> "Up to date (checked $latest)"
                    compatible != null && compatible != installedVersion -> "Compatible update  $compatible"
                    else -> "Newer pack needs a newer build"
                }, video)
            }
            else -> listOf("Original PAL game", "Your own disc data", video)
        }
    }

    // ---------------------------------------------------------------- achievements preview
    private fun drawAchievements(c: Canvas, art: Artwork) {
        val name = when (selection.product) { 0 -> "Mario Kart Wii"; 1 -> "Retro Rewind"; else -> separateGameName() }
        previewArt.draw(c, achievementText(art), preview?.takeIf { it.game == previewGame }, name)
    }

    private fun achievementText(art: Artwork): AchievementText =
        achievementFont?.takeIf { it.first === art.font }?.second
            ?: ThorAchievementText.of(art.font).also { achievementFont = art.font to it }

    private fun requestAchievements(game: String?) {
        val token = latest.begin(game)
        if (game == null) return
        val app = context.applicationContext
        achievementWorker.execute {
            val result = runCatching {
                val loaded = RaLibrary.load(app)
                val snap = AchievementCatalog.of(loaded.library, game, loaded.account != null)
                val art = badgeArt(app)
                val shown = (snap.recent.take(2) + snap.upcoming.take(3)).ifEmpty { listOf(AchievementCatalog.placeholder()) }
                ThorAchievementPreview.Data(game, snap, shown.associate { it.id to art.badge(it, 140) }, false, RaLibrary.running())
            }.getOrElse { ThorAchievementPreview.Data(game, null, mapOf("none" to badgeArt(app).badge(AchievementCatalog.placeholder(), 140)), true) }
            main.post { if (!closed && latest.accept(token, game)) { preview = result; invalidate() } }
        }
    }

    /** Back in the library (from a game or the browser), or a refresh finished: re-read this game's previews. */
    fun refreshAchievements() { if (!closed) { badgeArt(context.applicationContext).forgetImported(); requestAchievements(previewGame) } }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (onOpenAchievements == null || selection.options || selection.product == null ||
            selection.phase == ThorLauncherView.Phase.LAUNCHING) return false
        val x = (event.x - designLeft) / designScale
        val y = (event.y - designTop) / designScale
        val hit = previewArt.hits.firstOrNull { it.first.contains(x, y) }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> { previewArt.pressed = hit; invalidate(); return hit != null }
            MotionEvent.ACTION_UP -> {
                val target = previewArt.pressed
                previewArt.pressed = null
                invalidate()
                val game = previewGame
                if (target != null && hit != null && target.second == hit.second && game != null) {
                    ThorLauncherAudio.play(ThorLauncherAudio.Sound.SELECT)
                    onOpenAchievements?.invoke(game, target.second)
                }
                return true
            }
            MotionEvent.ACTION_CANCEL -> { previewArt.pressed = null; invalidate(); return true }
        }
        return previewArt.pressed != null
    }

    private fun drawLaunching(c: Canvas, art: Artwork, image: (String, Float, Float, Float, Float, Boolean, Int) -> Unit,
                              text: (String, Float, Float, Float, Float, Boolean) -> Unit) {
        // A calm companion while the upper disc spins up: the same disc turning slowly, plain words, no fake progress.
        val t = (SystemClock.uptimeMillis() - phaseAt) / 1000f
        val prefix = artPrefix()
        val fade = (t / 0.6f).coerceIn(0f, 1f)
        val cx = 640f; val cy = 540f; val r = 210f * discScale(prefix)
        paint.shader = RadialGradient(cx, cy, r * 2.2f, intArrayOf(Color.argb((70 * fade).toInt(), 200, 225, 255), 0x00000000), null, Shader.TileMode.CLAMP)
        c.drawCircle(cx, cy, r * 2.2f, paint); paint.shader = null
        art.images["$prefix-disc-round"]?.let { disc ->
            c.save(); c.rotate((t * t * 60f + t * 40f) % 360f, cx, cy)
            area.set(cx - r, cy - r, cx + r, cy + r); paint.alpha = (255 * fade).toInt(); c.drawBitmap(disc, null, area, paint); paint.alpha = 255
            c.restore()
        }
        text("The game is loading.", 640f, 800f, 40f, 1100f, true)
        if (separateGame()) {
            footerHints(text, if (selection.product == GALAXY || selection.product == KAIZO) "Back returns to your WiiDS library." else "Main Menu in the game brings you back here.")
            return
        }
        val video = videoSettings
        text(video?.let { "${it.resolution}  /  ${it.frameRate}" } ?: "", 640f, 860f, 30f, 1100f, true)
        footerHints(text, "Your race map appears here once a race begins.")
    }

    private fun footerHints(text: (String, Float, Float, Float, Float, Boolean) -> Unit, value: String) {
        text(value, 640f, 940f, 32f, 1150f, true)
    }

    companion object {
        /** Library index of the New Super Mario Bros. Wii case (after Mario Kart Wii and Retro Rewind). */
        const val NSMB = 2
        const val MSMBWII = 3
        const val SUNSHINE = 4
        const val ECLIPSE = 5
        const val GALAXY = 6
        const val KAIZO = 7
        private val main = Handler(Looper.getMainLooper())
        private val achievementWorker = Executors.newSingleThreadExecutor { task ->
            Thread(task, "ThorLibraryAchievements").apply { isDaemon = true }
        }
        @Volatile private var badges: AchievementBadges? = null
        private fun badgeArt(context: Context): AchievementBadges =
            badges ?: synchronized(this) { badges ?: AchievementBadges(context).also { badges = it } }
        private val worker = Executors.newSingleThreadExecutor { task ->
            Thread(task, "ThorLibraryArtwork").apply { isDaemon = true }
        }
        private val cached = HashMap<String, Artwork>()
        private val waiters = HashMap<String, MutableList<(Artwork?) -> Unit>>()
        private fun requestArtwork(key: String, folder: File, cases: File, ready: (Artwork?) -> Unit) {
            cached[key]?.let { ready(it); return }
            waiters[key]?.let { it.add(ready); return }
            waiters[key] = mutableListOf(ready)
            worker.execute {
                val art = runCatching {
                    val font = ThorMenuArtwork(folder)
                    val images = LinkedHashMap<String, Bitmap>()
                    val manifest = JSONObject(File(folder, "menu.json").readText())
                    require(manifest.getInt("schema") == 1)
                    val roles = manifest.getJSONObject("images")
                    for (role in listOf("background", "header", "panel", "footer")) {
                        runCatching { font.bitmap(roles.getJSONObject(role).getString("texture")) }
                            .getOrNull()?.let { images[role] = it }
                    }
                    for (game in listOf("original", "retro-rewind", "nsmbw", "msmbwii", "sunshine", "eclipse", "galaxy", "kaizo")) for (kind in listOf("cover", "disc")) {
                        ThorLauncherArtwork.bitmap(cases, "$game-$kind")?.let { images["$game-$kind"] = it }
                    }
                    // Round, hole-punched discs prepared once here (the supplied original disc
                    // is a square JPEG; drawing it raw showed a white box on the lower screen).
                    for (name in listOf("original-disc", "retro-rewind-disc", "nsmbw-disc", "msmbwii-disc", "sunshine-disc", "eclipse-disc", "galaxy-disc", "kaizo-disc")) images[name]?.let {
                        images["$name-round"] = roundDisc(it, discScale(name.removeSuffix("-disc")))
                    }
                    Artwork(font, images)
                }.getOrNull()
                main.post {
                    if (art != null) {
                        cached.keys.removeAll { it.substringBefore('|') == key.substringBefore('|') }
                        cached[key] = art
                    }
                    waiters.remove(key)?.forEach { it(art) }
                }
            }
        }

        /** GameCube mini discs are 8 cm against the 12 cm Wii disc; the 15 mm hole is the same. */
        private fun discScale(prefix: String) = if (prefix == "sunshine" || prefix == "eclipse") ThorLauncherView.GAMECUBE_DISC else 1f

        private fun roundDisc(source: Bitmap, scale: Float): Bitmap {
            val size = 400
            val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
            val c = Canvas(out)
            c.drawBitmap(source, null, RectF(0f, 0f, size.toFloat(), size.toFloat()), Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG))
            val clear = Paint(Paint.ANTI_ALIAS_FLAG).apply { xfermode = PorterDuffXfermode(PorterDuff.Mode.CLEAR) }
            c.drawPath(Path().apply { fillType = Path.FillType.INVERSE_WINDING; addCircle(200f, 200f, 198f, Path.Direction.CW) }, clear)
            c.drawCircle(200f, 200f, 25f / scale, clear)
            return out
        }
    }
}
