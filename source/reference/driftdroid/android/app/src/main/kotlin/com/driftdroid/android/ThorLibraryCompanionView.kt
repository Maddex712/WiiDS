package com.driftdroid.android

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.RadialGradient
import android.graphics.Rect
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
import com.driftdroid.android.dualscreen.WiiDsSkin
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors
import kotlin.math.roundToInt

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
    /** The menu style of the last frame; the poll redraws when Options switches it. */
    private var drawnStyle: ThorMenuTheme.Style? = null
    private var installedVersion: String? = null
    private var videoSettings: VideoSettings? = null
    private var snapshot = ThorRetroRewindUpdateStatus.snapshot
    private var pendingUpdate = false
    private var recoveryRequired = false
    private var closed = false
    private var phaseAt = SystemClock.uptimeMillis()
    private var previewAt = phaseAt
    private val area = RectF()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    /** Touching a preview card or View all: (game id, achievement id or null). */
    var onOpenAchievements: ((String, String?) -> Unit)? = null
    /** Wii Home Menu theme: the channel screen's buttons ("start", "menu", "prev", "next"). */
    var onChannelButton: ((String) -> Unit)? = null
    private val channelHits = ArrayList<Pair<RectF, String>>()
    private val channelStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    var drawChannelBanner: ((Canvas, RectF, Int, Long) -> Unit)? = null
    var channelPlayer: WiiChannelPreviewPlayer? = null
    private val channelShell = RectF(20f, 26f, 1260f, 1054f)
    private val channelBanner = RectF(47f, 49f, 1233f, 709f)
    private val channelBannerClip = Path().apply { addRoundRect(channelBanner, 22f, 22f, Path.Direction.CW) }
    private val channelVideoBounds = Rect()
    private val channelArrow = Path()
    private val channelSeam = Path().apply {
        moveTo(21f, 755f)
        lineTo(278f, 755f)
        cubicTo(389f, 755f, 420f, 787f, 508f, 787f)
        lineTo(772f, 787f)
        cubicTo(860f, 787f, 891f, 755f, 1002f, 755f)
        lineTo(1259f, 755f)
    }
    private val channelControls = Path(channelSeam).apply {
        lineTo(1259f, 1031f)
        quadTo(1259f, 1053f, 1237f, 1053f)
        lineTo(43f, 1053f)
        quadTo(21f, 1053f, 21f, 1031f)
        lineTo(21f, 755f)
        close()
    }
    private var channelPressed: String? = null
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
    private val collection = FrontendGame.values().filter { it != FrontendGame.RETRO_REWIND || BuildConfig.THOR_RETRO_REWIND }
    private var caseLayers = ThorLauncherFolders.caseLayers(context, folder)
    private var cacheKey = artKey(caseLayers)
    private fun artKey(layers: List<File>) = folder.absolutePath + "|" + layers.joinToString("|") { source ->
        val manifest = File(source, "artwork.json")
        "${source.absolutePath}:${source.lastModified()}:${manifest.lastModified()}:${manifest.length()}"
    }
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
            if (ThorMenuTheme.current(context) != drawnStyle) invalidate()
            // Cases composed while this screen is showing (art built after an import).
            val layers = ThorLauncherFolders.caseLayers(context, folder)
            val key = artKey(layers)
            if (key != cacheKey) {
                waiters[cacheKey]?.remove(loaded)
                cacheKey = key
                caseLayers = layers
                requestArtwork(cacheKey, folder, caseLayers, loaded)
            }
            postDelayed(this, 1000L)
        }
    }

    init {
        isFocusable = false
        isFocusableInTouchMode = false
        fallback.setStatus("Game library", "Choose a game on the upper screen.")
        fallback.setOnContentChanged(Runnable { if (!closed) invalidate() })
        requestArtwork(cacheKey, folder, caseLayers, loaded)
    }

    fun setSelection(value: Selection) {
        if (closed) return
        if (value.phase != selection.phase) phaseAt = SystemClock.uptimeMillis()
        if (value.product != selection.product) {
            previewAt = SystemClock.uptimeMillis()
            stopChannelPreview()
        }
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
        val games = if (selection.options || selection.product == null)
            collection.joinToString(prefix = "Your games: ", postfix = ". ") { it.label } else ""
        contentDescription = "$title. ${readiness()}. $games" +
            (if (selection.product == 1) installedVersion?.let { "Retro Rewind $it. " }.orEmpty() else "") +
            selection.message
    }

    /** Separate games validate their own data and never inherit Mario Kart update/video state. */
    private fun separateGame() = (selection.product == NSMB || selection.product == SUNSHINE ||
        selection.product == MSMBWII || selection.product == ECLIPSE || selection.product == GALAXY || selection.product == KAIZO || selection.product == BRAWL || selection.product == MKDD || selection.product == MKDD_PLUS || selection.product == PROJECT_PLUS ||
        selection.product == POKEMON_XD || selection.product == POKEMON_XG ||
        selection.product == COLOSSEUM || selection.product == CITY_FOLK) && !selection.options
    /** MSMBWii's disc is there but its mod isn't (yet). */
    private fun modPending() = selection.product == MSMBWII && nsmbDiscImported && !selection.available
    private fun separateGameName() = when (selection.product) {
        CITY_FOLK -> "Animal Crossing"
        COLOSSEUM -> "Pokémon Colosseum"
        POKEMON_XG -> "Pokémon XG"
        POKEMON_XD -> "Pokemon XD"
        PROJECT_PLUS -> "Project+"
        MKDD_PLUS -> "Double Dash Plus"
        MKDD -> "Double Dash"
        BRAWL -> "Brawl"
        KAIZO -> "Kaizo Rebalanced"
        GALAXY -> "Galaxy"
        SUNSHINE -> "Sunshine"
        ECLIPSE -> "Eclipse"
        MSMBWII -> "MSMBWii"
        else -> "NSMB Wii"
    }

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
        stopChannelPreview()
        removeCallbacks(poll)
        super.onDetachedFromWindow()
    }

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility != VISIBLE) stopChannelPreview()
    }

    fun stopChannelPreview() { channelPlayer?.show(null) }

    fun close() {
        stopChannelPreview()
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
        BRAWL -> "Super Smash Bros. Brawl"
        PROJECT_PLUS -> "Project+"
        MKDD -> "Mario Kart: Double Dash!!"
        MKDD_PLUS -> "Mario Kart: Double Dash!! Plus"
        POKEMON_XD -> "Pokemon XD: Gale of Darkness"
        POKEMON_XG -> "Pokémon XG: NeXt Gen"
        COLOSSEUM -> "Pokémon Colosseum"
        CITY_FOLK -> dev.wiids.cityfolk.CityFolkStorage.TITLE
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
        BRAWL -> FrontendGame.BRAWL.id
        PROJECT_PLUS -> FrontendGame.PROJECT_PLUS.id
        MKDD -> FrontendGame.MKDD.id
        MKDD_PLUS -> FrontendGame.MKDD_PLUS.id
        POKEMON_XD -> FrontendGame.POKEMON_XD.id
        POKEMON_XG -> FrontendGame.POKEMON_XG.id
        COLOSSEUM -> FrontendGame.POKEMON_COLOSSEUM.id
        CITY_FOLK -> FrontendGame.CITY_FOLK.id
        else -> null
    }

    private fun artPrefix() = when (selection.product) {
        1 -> "retro-rewind"
        NSMB -> "nsmbw"
        SUNSHINE -> "sunshine"
        ECLIPSE -> "eclipse"
        GALAXY -> "galaxy"
        KAIZO -> "kaizo"
        BRAWL -> "brawl"
        PROJECT_PLUS -> "project-plus"
        MKDD -> "mkdd"
        MKDD_PLUS -> "mkdd-plus"
        POKEMON_XD -> "pokemon-xd"
        POKEMON_XG -> "pokemon-xg"
        COLOSSEUM -> "pokemon-colosseum"
        CITY_FOLK -> "city-folk"
        MSMBWII -> "msmbwii"
        else -> "original"
    }

    override fun onDraw(canvas: Canvas) {
        if (width <= 0 || height <= 0) return
        val scale = minOf(width / 1280f, height / 1080f)
        designScale = scale; designLeft = (width - 1280f * scale) / 2f; designTop = (height - 1080f * scale) / 2f
        if (selection.options || selection.product == null || selection.phase == ThorLauncherView.Phase.LAUNCHING) previewArt.hits.clear()
        canvas.save()
        canvas.translate(designLeft, designTop)
        canvas.scale(scale, scale)
        drawnStyle = ThorMenuTheme.current(context)
        channelHits.clear()
        if (drawnStyle?.channels == true) {
            val product = selection.product
            if (!selection.options && product != null) {
                drawChannelScreen(canvas, product)
            } else {
                stopChannelPreview()
                drawChannelHome(canvas)
            }
            canvas.restore()
            return
        }
        stopChannelPreview()
        canvas.drawColor(Color.rgb(24, 29, 37))
        val art = artwork
        if (art == null) {
            canvas.restore()
            fallback.render(canvas, width, height)
            return
        }
        val font = art.font
        val wii = drawnStyle != ThorMenuTheme.Style.MARIO_KART
        fun image(role: String, left: Float, top: Float, right: Float, bottom: Float, rounded: Boolean = false, alpha: Int = 255) {
            area.set(left, top, right, bottom)
            if (wii && ThorMenuTheme.chrome(canvas, role, area, alpha)) return
            art.images[role]?.let {
                if (rounded) font.roundedImage(canvas, it, area, 26f, alpha) else font.image(canvas, it, area, alpha)
            }
        }
        fun text(value: String, x: Float, y: Float, height: Float, width: Float, center: Boolean = false) {
            if (value.isEmpty()) return
            if (wii) ThorMenuTheme.text(canvas, value, x, y, height, width, center)
            else font.text(canvas, value, x, y, height, width, center)
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
        // Fit the entire library in the collection area, without moving the Options panel.
        val rows = (collection.size + 5) / 6
        val perRow = (collection.size + rows - 1) / rows
        val w = minOf((486f - (perRow - 1) * 10f) / perRow, (338f - (rows - 1) * 12f) / rows * 112f / 160f)
        val h = w * 160f / 112f
        val top = 455f - (rows * h + (rows - 1) * 12f) / 2
        for ((i, game) in collection.withIndex()) {
            val row = i / perRow
            val inRow = minOf(perRow, collection.size - row * perRow)
            val x = 96f + (486f - (inRow * w + (inRow - 1) * 10f)) / 2 + (i % perRow) * (w + 10f)
            val y = top + row * (h + 12f)
            val role = "${game.artwork}-cover"
            if (artwork?.images?.containsKey(role) == true) image(role, x, y, x + w, y + h, false, 255)
            else {
                paint.color = if (drawnStyle != ThorMenuTheme.Style.MARIO_KART) 0xffdce6ef.toInt() else 0xff25364c.toInt()
                c.drawRoundRect(x, y, x + w, y + h, 6f, 6f, paint)
                text(game.label, x + w / 2, y + h / 2 - 9f, 18f, w - 6f, true)
            }
        }
        text("Your games", 322f, 674f, 35f, 442f, true)
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
                selection.product == BRAWL -> "Import your Brawl USA revision 2 disc"
                selection.product == PROJECT_PLUS -> "Set up Project+ in Manage games"
                selection.product == MKDD -> "Import your Double Dash USA disc"
                selection.product == MKDD_PLUS -> "Set up Double Dash Plus to play"
                selection.product == POKEMON_XG -> "Set up Pokémon XG to play"
                selection.product == COLOSSEUM -> "Import your Pokémon Colosseum disc"
                selection.product == CITY_FOLK -> "Import your Animal Crossing disc"
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
                selection.product == BRAWL -> if (selection.available) "Brawl ready" else "Import your Brawl disc"
                selection.product == PROJECT_PLUS -> if (selection.available) "Project+ ready" else "Set up Project+ first"
                selection.product == MKDD -> if (selection.available) "Double Dash ready" else "Import your Double Dash disc"
                selection.product == POKEMON_XD -> if (selection.available) "Pokemon XD ready" else "Import your Pokemon XD disc"
                selection.product == POKEMON_XG -> if (selection.available) "Pokémon XG ready" else "Set up Pokémon XG first"
                selection.product == COLOSSEUM -> if (selection.available) "Pokémon Colosseum ready" else "Import your Pokémon Colosseum disc"
                selection.product == CITY_FOLK -> if (selection.available) "Animal Crossing ready" else "Import your Animal Crossing disc"
                selection.product == MKDD_PLUS -> if (selection.available) "Double Dash Plus ready" else "Set up Double Dash Plus first"
                selection.product == ECLIPSE -> if (selection.available) "Eclipse app installed" else "Install the Eclipse Thor app"
                selection.available -> "Disc imported"
                modPending() -> "Finish it on Your games"
                else -> "Import it on Your games"
            }, when (selection.product) {
                KAIZO -> "Own Galaxy disc + official 2.1 patch"
                GALAXY -> "USA Wii disc"
                BRAWL -> "Import your USA Wii disc, revision 2, in WiiDS"
                PROJECT_PLUS -> "Uses your Brawl disc with separate saves"
                MKDD -> "USA GameCube disc"
                MKDD_PLUS -> "Plus 3.3 / separate memory card"
                POKEMON_XD -> "USA GameCube disc, revision 0"
                POKEMON_XG -> "NeXt Gen 1.2.1 / separate saves"
                COLOSSEUM -> "USA GameCube disc, revision 0"
                CITY_FOLK -> "European Wii disc, revision 1"
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

    private fun drawChannelHome(c: Canvas) {
        area.set(-400f, -400f, 1680f, 1480f)
        ThorMenuTheme.chrome(c, "background", area)
        val settings = selection.phase == ThorLauncherView.Phase.OPTIONS
        val title = if (settings) "WiiDS Settings" else "WiiDS Menu"
        val hint = if (settings) "Choose a setting on the upper screen." else "Highlight a channel on the upper screen."
        ThorMenuTheme.text(c, title, 640f, 419f, 76f, 1040f, true)
        paint.color = ThorMenuTheme.tone(0xff79c3dc.toInt(), ThorMenuTheme.accent)
        c.drawRoundRect(592f, 529f, 688f, 533f, 2f, 2f, paint)
        ThorMenuTheme.text(c, hint, 640f, 580f, 44f, 1000f, true)
    }

    /** Keep the highlighted channel open below while the upper display browses the grid. */
    private fun drawChannelScreen(c: Canvas, product: Int) {
        val phase = selection.phase
        val launching = phase == ThorLauncherView.Phase.LAUNCHING
        val now = SystemClock.uptimeMillis()
        val elapsed = now - previewAt
        val motion = WiiDsSkin.motion() && windowVisibility == VISIBLE && isShown &&
            !launching && phase != ThorLauncherView.Phase.BLOCKED
        val reveal = if (motion) (elapsed / 200f).coerceIn(0f, 1f) else 1f
        val artId = artPrefix()
        val live = channelPlayer
        channelVideoBounds.set(
            (designLeft + channelBanner.left * designScale).roundToInt(),
            (designTop + channelBanner.top * designScale).roundToInt(),
            (designLeft + channelBanner.right * designScale).roundToInt(),
            (designTop + channelBanner.bottom * designScale).roundToInt(),
        )
        live?.place(channelVideoBounds, 22f * designScale)
        // Show the poster immediately and wait for focus to settle before opening its decoder.
        live?.show(if (motion && reveal >= 1f) artId else null)
        val video = motion && live?.hasFrame == true
        val artMotion = motion && WiiChannelArtwork.hasChannelLoop(artId)

        c.save()
        if (video) c.clipOutPath(channelBannerClip)
        paint.color = ThorMenuTheme.tone(0xffe5ebef.toInt(), ThorMenuTheme.surfaceBottom)
        c.drawRect(-400f, -400f, 1680f, 1480f, paint)
        paint.color = ThorMenuTheme.tone(0x250e3346, 0x33000000)
        c.drawRoundRect(channelShell.left, channelShell.top + 6f, channelShell.right, channelShell.bottom + 6f, 33f, 33f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, channelShell.top, 0f, channelShell.bottom,
            ThorMenuTheme.tone(0xfffbfdfe.toInt(), ThorMenuTheme.panel),
            ThorMenuTheme.tone(0xffe4ecf1.toInt(), ThorMenuTheme.surfaceBottom), Shader.TileMode.CLAMP)
        c.drawRoundRect(channelShell, 33f, 33f, paint)
        paint.shader = null
        if (!video) {
            paint.color = ThorMenuTheme.tone(0xffdde9ef.toInt(), ThorMenuTheme.buttonBottom)
            c.drawRoundRect(channelBanner, 22f, 22f, paint)
            c.clipPath(channelBannerClip)
            val zoom = 1f - 0.06f * (1f - reveal) * (1f - reveal)
            c.scale(zoom, zoom, channelBanner.centerX(), channelBanner.centerY())
            val banner = drawChannelBanner
            if (banner != null) banner(c, channelBanner, product, if (artMotion) elapsed else 0L)
            else ThorMenuTheme.text(c, productTitle(), 640f, 338f, 82f, 1040f, true)
        }
        c.restore()
        channelStroke.color = ThorMenuTheme.tone(0xff9cabb5.toInt(), ThorMenuTheme.border)
        channelStroke.strokeWidth = 2.5f
        c.drawRoundRect(channelBanner, 22f, 22f, channelStroke)

        drawChannelArrow(c, next = false)
        drawChannelArrow(c, next = true)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, 755f, 0f, 1053f,
            ThorMenuTheme.tone(0xfff5f8fa.toInt(), ThorMenuTheme.panel),
            ThorMenuTheme.tone(0xffcfdae2.toInt(), ThorMenuTheme.surfaceBottom), Shader.TileMode.CLAMP)
        c.drawPath(channelControls, paint)
        paint.shader = null
        channelStroke.color = ThorMenuTheme.tone(0xff77bdd5.toInt(), ThorMenuTheme.accent)
        channelStroke.strokeWidth = 3f
        c.drawPath(channelSeam, channelStroke)
        drawChannelButton(c, "WiiDS Menu", "menu", 171f, primary = false)
        drawChannelButton(c, if (selection.available) "Start" else "Set up", "start", 683f, primary = true)

        if (phase == ThorLauncherView.Phase.BLOCKED && selection.message.isNotEmpty()) {
            area.set(100f, 614f, 1180f, 696f)
            paint.color = ThorMenuTheme.panel
            c.drawRoundRect(area, 32f, 32f, paint)
            ThorMenuTheme.text(c, selection.message, 640f, 630f, 46f, 990f, true)
        }
        if (launching) {
            val fade = ((now - phaseAt) / 520f).coerceIn(0f, 1f)
            paint.color = (ThorMenuTheme.tone(Color.WHITE, ThorMenuTheme.surfaceBottom) and 0x00ffffff) or ((255 * fade).toInt() shl 24)
            c.drawRect(-400f, -400f, 1680f, 1480f, paint)
            channelHits.clear()
        }
        if (launching || reveal < 1f) postInvalidateOnAnimation()
        else if (artMotion) postInvalidateDelayed(32L)
    }

    private fun drawChannelArrow(c: Canvas, next: Boolean) {
        val id = if (next) "next" else "prev"
        val x = if (next) 1191f else 89f
        val y = channelBanner.centerY()
        val direction = if (next) 1f else -1f
        channelHits.add(RectF(x - 57f, y - 78f, x + 57f, y + 78f) to id)
        channelArrow.reset()
        channelArrow.moveTo(x - 9f * direction, y - 24f)
        channelArrow.lineTo(x + 14f * direction, y)
        channelArrow.lineTo(x - 9f * direction, y + 24f)
        channelArrow.close()
        paint.color = if (channelPressed == id) ThorMenuTheme.tone(0xff83d2ec.toInt(), ThorMenuTheme.accent) else ThorMenuTheme.tone(0xf8f8fdff.toInt(), ThorMenuTheme.buttonTop)
        c.drawPath(channelArrow, paint)
        channelStroke.color = ThorMenuTheme.tone(0xff3d9fbd.toInt(), ThorMenuTheme.accent)
        channelStroke.strokeWidth = 4f
        channelStroke.strokeJoin = Paint.Join.ROUND
        c.drawPath(channelArrow, channelStroke)
    }

    private fun drawChannelButton(c: Canvas, label: String, id: String, left: Float, primary: Boolean) {
        area.set(left - 15f, 829f, left + 441f, 978f)
        channelHits.add(RectF(area) to id)
        area.set(left, 845f, left + 426f, 962f)
        val pressed = channelPressed == id
        if (pressed) area.offset(0f, 3f)
        paint.color = ThorMenuTheme.tone(0x29152735, 0x33000000)
        c.drawRoundRect(area.left, area.top + 5f, area.right, area.bottom + 5f, 58f, 58f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, area.top, 0f, area.bottom,
            intArrayOf(ThorMenuTheme.buttonTop, ThorMenuTheme.tone(0xfff6f9fa.toInt(), ThorMenuTheme.panel),
                ThorMenuTheme.tone(if (pressed) 0xffc7e8f3.toInt() else 0xffdce8ed.toInt(),
                    if (pressed) ThorMenuTheme.buttonTop else ThorMenuTheme.buttonBottom)),
            floatArrayOf(0f, 0.53f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(area, 58f, 58f, paint)
        paint.shader = null
        channelStroke.color = if (primary) ThorMenuTheme.tone(0xff58adcc.toInt(), ThorMenuTheme.accent)
            else ThorMenuTheme.tone(0xff94aeba.toInt(), ThorMenuTheme.border)
        channelStroke.strokeWidth = if (primary) 4f else 3f
        c.drawRoundRect(area, 58f, 58f, channelStroke)
        ThorMenuTheme.text(c, label, area.centerX(), area.centerY() - 28f, 55f, area.width() - 50f, true,
            if (primary) ThorMenuTheme.tone(0xff275367.toInt(), ThorMenuTheme.accentText) else ThorMenuTheme.TEXT)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (channelHits.isNotEmpty() && onChannelButton != null) {
            val x = (event.x - designLeft) / designScale
            val y = (event.y - designTop) / designScale
            val hit = channelHits.firstOrNull { it.first.contains(x, y) }?.second
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> channelPressed = hit
                MotionEvent.ACTION_UP -> {
                    val target = channelPressed
                    channelPressed = null
                    if (target != null && target == hit) onChannelButton?.invoke(target)
                }
                MotionEvent.ACTION_CANCEL -> channelPressed = null
            }
            invalidate()
            return true
        }
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
        const val BRAWL = 8
        const val MKDD = 9
        const val MKDD_PLUS = 10
        const val PROJECT_PLUS = 11
        const val POKEMON_XD = 12
        const val POKEMON_XG = 13
        const val COLOSSEUM = 14
        const val CITY_FOLK = 15
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
        private fun requestArtwork(key: String, folder: File, layers: List<File>, ready: (Artwork?) -> Unit) {
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
                    for (game in FrontendGame.values()) {
                        for (kind in listOf("cover", "disc")) {
                            val role = "${game.artwork}-$kind"
                            layers.firstNotNullOfOrNull { ThorLauncherArtwork.bitmap(it, role) }?.let { images[role] = it }
                        }
                        val disc = "${game.artwork}-disc"
                        images[disc]?.let { images["$disc-round"] = roundDisc(it, discScale(game.artwork)) }
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
        private fun discScale(prefix: String) = if (prefix == "sunshine" || prefix == "eclipse" || prefix == "mkdd" || prefix == "mkdd-plus" || prefix == "pokemon-xd" || prefix == "pokemon-xg" || prefix == "pokemon-colosseum") ThorLauncherView.GAMECUBE_DISC else 1f

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
