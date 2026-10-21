package com.driftdroid.android.rom

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import java.io.File

/**
 * Shown full-screen over the (still blank) SDL surface when no extracted Mario Kart Wii DATA
 * folder exists yet at [dvdRoot] - runtime/src/hle/storage/dvd.cpp polls for that folder instead
 * of failing outright on Android specifically, for exactly this reason: this overlay and native
 * boot are racing, and native just waits for this to finish.
 *
 * Picks a disc image via Storage Access Framework, then hands the raw file descriptor to the
 * native extractor (runtime/src/hle/storage/wii_disc_extractor.cpp) on a background thread, which
 * decrypts and unpacks it straight into [dvdRoot]/sys and [dvdRoot]/files. [onImportComplete]
 * fires once that succeeds, after this overlay has removed itself.
 */
class RomImportOverlay private constructor(
    private val activity: Activity,
    private val dvdRoot: File,
    private val onImportComplete: () -> Unit,
) {

    private val container = FrameLayout(activity)
    private val statusText = TextView(activity)
    private val selectButton = Button(activity)
    private val progressBar = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal)
    private val mainHandler = Handler(Looper.getMainLooper())

    private var openFd: ParcelFileDescriptor? = null
    private var progressPoller: Runnable? = null

    init {
        container.setBackgroundColor(Color.rgb(18, 18, 22))

        val column = LinearLayout(activity)
        column.orientation = LinearLayout.VERTICAL
        column.gravity = Gravity.CENTER
        val pad = dp(24)
        column.setPadding(pad, pad, pad, pad)

        val title = TextView(activity)
        title.text = "Please select the Mario Kart Wii ROM ISO"
        title.setTextColor(Color.WHITE)
        title.textSize = 20f
        title.gravity = Gravity.CENTER

        val subtitle = TextView(activity)
        subtitle.text = "(ONLY PAL REGION SUPPORTED)"
        subtitle.setTextColor(Color.argb(200, 255, 210, 90))
        subtitle.textSize = 14f
        subtitle.gravity = Gravity.CENTER
        subtitle.setPadding(0, dp(8), 0, dp(24))

        selectButton.text = "Select ROM file (.iso / .wbfs)"
        selectButton.setOnClickListener { launchPicker() }

        progressBar.max = 1000
        progressBar.visibility = View.GONE
        val progressParams =
            LinearLayout.LayoutParams(dp(260), ViewGroup.LayoutParams.WRAP_CONTENT)
        progressParams.topMargin = dp(16)

        statusText.setTextColor(Color.argb(200, 255, 255, 255))
        statusText.textSize = 13f
        statusText.gravity = Gravity.CENTER
        statusText.setPadding(0, dp(16), 0, 0)

        column.addView(title)
        column.addView(subtitle)
        column.addView(selectButton)
        column.addView(progressBar, progressParams)
        column.addView(statusText)

        val columnParams =
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        columnParams.gravity = Gravity.CENTER
        container.addView(column, columnParams)
    }

    private fun dp(value: Int): Int = (value * activity.resources.displayMetrics.density).toInt()

    private fun launchPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        activity.startActivityForResult(intent, REQUEST_CODE_PICK_ROM)
    }

    /** Call from the activity's onActivityResult for [REQUEST_CODE_PICK_ROM]. */
    fun onFilePicked(uri: Uri?) {
        if (uri == null) {
            return
        }
        val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "selected file"
        val extension = name.substringAfterLast('.', "").lowercase()
        if (extension !in ACCEPTED_EXTENSIONS) {
            showResult(false, "\"$name\" isn't a .iso/.gcm/.gcz/.wbfs file - pick your Mario Kart Wii disc image.")
            return
        }

        // WBFS hides the game ID behind its own block table, so this quick check only covers
        // plain disc images - the native extractor checks the real game ID either way before it
        // touches anything else, so a wrong WBFS pick still fails cleanly, just a bit later.
        if (extension != "wbfs") {
            val gameId = readGameId(uri)
            if (gameId == null) {
                showResult(false, "Couldn't read \"$name\" - make sure it's a valid disc image.")
                return
            }
            if (gameId != "RMCP01") {
                showResult(
                    false,
                    "\"$name\" is game ID $gameId, not RMCP01 (PAL Mario Kart Wii). Only the PAL release is supported.",
                )
                return
            }
        }

        startExtraction(uri, name)
    }

    /** A disc found by [RomScanner] in the launcher: extract it directly, no picker. */
    fun startFromFile(file: File) {
        val candidate = RomScanner.identify(file)
        if (candidate == null || !candidate.supported) {
            showResult(false, "\"${file.name}\" is no longer a PAL Mario Kart Wii disc image. Select it manually.")
            return
        }
        val fd = try { ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY) } catch (e: Exception) { null }
        startExtraction(null, file.name, fd)
    }

    private fun startExtraction(uri: Uri?, name: String, opened: ParcelFileDescriptor? = null) {
        val fd = opened ?:
            try {
                uri?.let { activity.contentResolver.openFileDescriptor(it, "r") }
            } catch (e: Exception) {
                null
            }
        if (fd == null) {
            showResult(false, "Couldn't open \"$name\" for reading.")
            return
        }
        openFd = fd

        selectButton.visibility = View.GONE
        progressBar.visibility = View.VISIBLE
        progressBar.progress = 0
        statusText.setTextColor(Color.WHITE)
        statusText.text = "Extracting \"$name\" - this can take a while for a multi-GB disc..."

        val poller =
            object : Runnable {
                override fun run() {
                    val total = nativeExtractBytesTotal()
                    val done = nativeExtractBytesDone()
                    if (total > 0) {
                        progressBar.progress = ((done * 1000) / total).toInt().coerceIn(0, 1000)
                        val percent = (done * 100) / total
                        statusText.text = "Extracting \"$name\"... $percent%"
                    }
                    mainHandler.postDelayed(this, 400)
                }
            }
        progressPoller = poller
        mainHandler.postDelayed(poller, 400)

        Thread {
            val error = nativeExtractDisc(fd.fd, dvdRoot.absolutePath)
            mainHandler.post { finishExtraction(error) }
        }.start()
    }

    private fun finishExtraction(error: String?) {
        progressPoller?.let { mainHandler.removeCallbacks(it) }
        progressPoller = null
        try {
            openFd?.close()
        } catch (_: Exception) {
        }
        openFd = null

        if (error == null) {
            progressBar.progress = 1000
            statusText.setTextColor(Color.rgb(120, 255, 150))
            statusText.text = "Done! Starting the game..."
            (container.parent as? ViewGroup)?.removeView(container)
            onImportComplete()
        } else {
            progressBar.visibility = View.GONE
            selectButton.visibility = View.VISIBLE
            showResult(false, error)
        }
    }

    private fun showResult(success: Boolean, message: String) {
        statusText.setTextColor(if (success) Color.rgb(120, 255, 150) else Color.rgb(255, 110, 110))
        statusText.text = message
    }

    private fun queryDisplayName(uri: Uri): String? =
        try {
            activity.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
            }
        } catch (_: Exception) {
            null
        }

    /** First 6 bytes of a GC/Wii disc image are the ASCII game ID (e.g. "RMCP01" for PAL MKWii). */
    private fun readGameId(uri: Uri): String? =
        try {
            activity.contentResolver.openInputStream(uri)?.use { stream ->
                val header = ByteArray(6)
                var read = 0
                while (read < header.size) {
                    val n = stream.read(header, read, header.size - read)
                    if (n < 0) break
                    read += n
                }
                if (read == header.size) String(header, Charsets.US_ASCII) else null
            }
        } catch (_: Exception) {
            null
        }

    private external fun nativeExtractDisc(fd: Int, destDataFolder: String): String?

    private external fun nativeExtractBytesDone(): Long

    private external fun nativeExtractBytesTotal(): Long

    companion object {
        const val REQUEST_CODE_PICK_ROM = 0x524F4D // "ROM"

        private val ACCEPTED_EXTENSIONS = setOf("iso", "gcm", "gcz", "wbfs")

        fun isRomAlreadyImported(dvdRoot: File): Boolean = File(dvdRoot, "sys/main.dol").isFile

        fun attach(
            activity: Activity,
            parent: ViewGroup,
            dvdRoot: File,
            onImportComplete: () -> Unit,
            presetFile: File? = null,
        ): RomImportOverlay {
            val overlay = RomImportOverlay(activity, dvdRoot, onImportComplete)
            presetFile?.let { overlay.startFromFile(it) }
            parent.addView(
                overlay.container,
                ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT),
            )
            return overlay
        }
    }
}
