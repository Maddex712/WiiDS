package com.driftdroid.android

import android.content.Context
import android.graphics.Outline
import android.graphics.Rect
import android.graphics.SurfaceTexture
import android.media.MediaPlayer
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.view.ViewOutlineProvider
import android.widget.FrameLayout
import java.io.File

/** Plays the selected disc's private, silent channel preview on the lower display. */
internal class WiiChannelPreviewPlayer(context: Context, private val onFrameChanged: () -> Unit) :
    TextureView(context), TextureView.SurfaceTextureListener {
    private val folders = ThorLauncherFolders.wiiChannels(context)
    private val viewport = Rect()
    private var requested: String? = null
    private var source: File? = null
    private var player: MediaPlayer? = null
    private var rendering = false
    private var radius = 0f
    var hasFrame = false
        private set

    init {
        visibility = INVISIBLE
        surfaceTextureListener = this
        clipToOutline = true
        outlineProvider = object : ViewOutlineProvider() {
            override fun getOutline(view: View, outline: Outline) {
                outline.setRoundRect(0, 0, view.width, view.height, radius)
            }
        }
    }

    fun place(bounds: Rect, cornerRadius: Float) {
        if (viewport == bounds && radius == cornerRadius) return
        viewport.set(bounds)
        radius = cornerRadius
        layoutParams = FrameLayout.LayoutParams(bounds.width(), bounds.height()).apply {
            leftMargin = bounds.left
            topMargin = bounds.top
        }
        invalidateOutline()
    }

    fun show(artId: String?) {
        if (requested == artId) return
        requested = artId
        releasePlayer()
        source = if (artId == null || artId !in loopStartMs) null else findVideo(artId)
        visibility = if (source == null) INVISIBLE else VISIBLE
        if (source != null) surfaceTexture?.let(::prepare)
    }

    private fun findVideo(artId: String): File? = folders.firstNotNullOfOrNull { directory ->
        runCatching {
            val file = File(directory.canonicalFile, "$artId-banner-motion.mp4")
            file.takeIf { it.canonicalFile == it && it.isFile && it.length() in 16L..67_108_864L }
        }.getOrNull()
    }

    private fun prepare(texture: SurfaceTexture) {
        val file = source ?: return
        val loop = loopStartMs[requested] ?: return
        if (player != null) return
        val clip = MediaPlayer()
        player = clip
        runCatching {
            val surface = Surface(texture)
            try { clip.setSurface(surface) } finally { surface.release() }
            file.inputStream().use { clip.setDataSource(it.fd) }
            clip.setVolume(0f, 0f)
            clip.setOnPreparedListener {
                if (player === it && it.videoWidth in 16..1920 && it.videoHeight in 16..1080 &&
                    it.duration > loop) it.start() else if (player === it) releasePlayer()
            }
            clip.setOnInfoListener { media, what, _ ->
                if (player === media && what == MediaPlayer.MEDIA_INFO_VIDEO_RENDERING_START) rendering = true
                false
            }
            clip.setOnCompletionListener {
                if (player === it) it.seekTo(loop.toLong(), MediaPlayer.SEEK_CLOSEST)
            }
            clip.setOnSeekCompleteListener { if (player === it) it.start() }
            clip.setOnErrorListener { media, _, _ ->
                if (player === media) releasePlayer()
                true
            }
            clip.prepareAsync()
        }.onFailure { releasePlayer() }
    }

    private fun releasePlayer() {
        val previous = player
        player = null
        previous?.release()
        rendering = false
        if (hasFrame) {
            hasFrame = false
            onFrameChanged()
        }
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) = prepare(surface)
    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) = Unit
    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        releasePlayer()
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
        if (rendering && !hasFrame) {
            hasFrame = true
            onFrameChanged()
        }
    }

    private companion object {
        // Start of the repeating segment in each rendered disc banner's original 60 Hz timeline.
        val loopStartMs = mapOf("original" to 1167, "nsmbw" to 8350, "galaxy" to 3350,
            "brawl" to 50017, "city-folk" to 5850)
    }
}
