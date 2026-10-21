package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File
import java.util.zip.ZipInputStream

/**
 * User-imported Riivolution-shaped mods (e.g. GameBanana downloads: a riivolution folder with an
 * XML file in it, plus a folder of replacement .brres/.szs files) - covers texture packs, UI
 * skins, and character/vehicle model swaps alike, since none of this is texture-specific: it's
 * the same generic disc-file-replacement pipeline Retro Rewind's own custom tracks use. Each mod
 * lives in its own subdirectory of [packsDir] (still named "TexturePacks" on disk - renaming it
 * would orphan whatever's already imported there for existing installs, and it's an internal
 * implementation detail the user never sees), registered as an extra DVD overlay root (see
 * nativeAddOverlayRoot / android_jni_bridge.cpp's g_androidOverlayRoots) exactly like Retro
 * Rewind's own install already is. The actual file-serve pipeline (RiivoDiscoverRoots ->
 * ScanOverlayRoot -> RegisterFileEntry, runtime/src/hle/storage) needs no changes for this - it
 * never distinguishes where an overlay root's files came from or what kind of file they replace.
 *
 * Mods are only picked up at the next app launch (same limitation as Retro Rewind: nothing here
 * can hot-swap which files the running guest fiber is reading from), so enable/disable/import all
 * just edit files on disk and tell the user to restart.
 */
object ModManager {

    private const val DISABLED_MARKER = ".disabled"
    private const val ORDER_FILE = ".mod_order"

    data class Pack(val dir: File, val enabled: Boolean) {
        val name: String get() = dir.name
    }

    fun packsDir(activity: Activity): File = File(activity.filesDir, "WiiCompiled/TexturePacks")

    private fun orderFile(activity: Activity): File = File(packsDir(activity), ORDER_FILE)

    // Priority order, top (index 0) wins: dvd.cpp applies discovered overlay roots in REVERSE
    // (overlays.rbegin()..rend(), "the explicitly configured root outranks the mod manifest"),
    // so the root at index 0 - the first one Kotlin pushes via enabledOverlayRoots/
    // nativeAddOverlayRoot - is the LAST one applied and therefore wins any file-path collision.
    // Persisted as a flat, newline-separated list of folder names (not the packs' own file
    // contents) since order needs to survive across imports/deletes independent of what's
    // actually installed at any given moment.
    fun installedPacks(activity: Activity): List<Pack> {
        val root = packsDir(activity)
        val dirs = root.listFiles { f -> f.isDirectory } ?: return emptyList()
        val byName = dirs.associateBy { it.name }
        val saved = orderFile(activity).takeIf { it.isFile }?.readLines().orEmpty()
        val ordered = mutableListOf<File>()
        for (name in saved) {
            byName[name]?.let { ordered.add(it) }
        }
        val remaining = dirs.filter { it.name !in saved }.sortedBy { it.name.lowercase() }
        ordered.addAll(remaining)
        return ordered.map { Pack(it, enabled = !File(it, DISABLED_MARKER).exists()) }
    }

    /** Persists a new priority order - top of the list (index 0) wins, matching
     * [installedPacks]'s convention above. */
    fun setOrder(activity: Activity, orderedNames: List<String>) {
        orderFile(activity).writeText(orderedNames.joinToString("\n"))
    }

    /** Called from MainActivity right before super.onCreate(), same timing rule as
     * nativeSetRetroRewindRoot - RuntimeRiivolution::Overlays() caches this list on first use,
     * which happens later during DVD init, but never re-reads it after. */
    fun enabledOverlayRoots(activity: Activity): List<String> =
        installedPacks(activity).filter { it.enabled }.map { it.dir.absolutePath }

    fun setEnabled(pack: Pack, enabled: Boolean) {
        val marker = File(pack.dir, DISABLED_MARKER)
        if (enabled) marker.delete() else marker.createNewFile()
    }

    fun delete(pack: Pack) {
        pack.dir.deleteRecursively()
    }

    // --- Import ---

    /** Extracts a zip (Zip-Slip guarded, same pattern as RetroRewindInstallService) into a fresh
     * subdirectory of [packsDir] named after the mod, on a background thread. [onDone] runs on
     * the UI thread with an error message, or null on success. */
    fun importZip(activity: Activity, source: Uri, packName: String, onDone: (String?) -> Unit) {
        val safeName = packName.trim().ifEmpty { "Mod" }.replace(Regex("[^A-Za-z0-9 _-]"), "_")
        Thread {
            val error = runCatching { extractZip(activity, source, safeName) }
                .fold(onSuccess = { it }, onFailure = { it.message ?: "unknown error" })
            activity.runOnUiThread { onDone(error) }
        }.start()
    }

    private fun extractZip(activity: Activity, source: Uri, safeName: String): String? {
        val root = packsDir(activity)
        root.mkdirs()
        var dest = File(root, safeName)
        var suffix = 2
        while (dest.exists()) {
            dest = File(root, "$safeName ($suffix)")
            suffix++
        }
        val scratch = File(activity.cacheDir, "mod_import_scratch")
        scratch.deleteRecursively()
        scratch.mkdirs()
        val scratchCanonical = scratch.canonicalFile
        try {
            val stream = activity.contentResolver.openInputStream(source)
                ?: return "couldn't open the selected file"
            stream.use { input ->
                ZipInputStream(input).use { zip ->
                    var entry = zip.nextEntry
                    var any = false
                    while (entry != null) {
                        val outFile = File(scratch, entry.name)
                        if (!outFile.canonicalPath.startsWith(scratchCanonical.path + File.separator) &&
                            outFile.canonicalPath != scratchCanonical.path
                        ) {
                            return "archive contains an unsafe file path"
                        }
                        if (entry.isDirectory) {
                            outFile.mkdirs()
                        } else {
                            outFile.parentFile?.mkdirs()
                            outFile.outputStream().use { output -> zip.copyTo(output) }
                            any = true
                        }
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                    if (!any) return "archive was empty"
                }
            }
        } catch (e: Exception) {
            return e.message ?: "extraction failed"
        }

        // A GameBanana zip commonly wraps its actual riivolution/ folder one level deep (a single
        // top-level folder named after the mod) - unwrap that one level so `dest` itself becomes
        // the overlay root riivolution.cpp expects (its riivolution/*.xml at the top).
        val resolved = resolveOverlayRoot(scratch)
        if (!resolved.renameTo(dest)) {
            resolved.copyRecursively(dest, overwrite = true)
        }
        scratch.deleteRecursively()
        return null
    }

    private fun resolveOverlayRoot(extracted: File): File {
        if (File(extracted, "riivolution").isDirectory) return extracted
        val children = extracted.listFiles { f -> f.isDirectory } ?: return extracted
        if (children.size == 1 && File(children[0], "riivolution").isDirectory) return children[0]
        return extracted
    }

    // --- UI ---

    const val REQUEST_CODE_PICK_ZIP = 9821

    fun startPickZip(activity: Activity) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        activity.startActivityForResult(intent, REQUEST_CODE_PICK_ZIP)
    }

    /** Call from the activity's onActivityResult for [REQUEST_CODE_PICK_ZIP]. */
    fun onZipPicked(activity: Activity, uri: Uri?) {
        if (uri == null) return
        val nameInput = EditText(activity)
        nameInput.hint = "Mod name"
        ThorDialogs.builder(activity)
            .setTitle("Name this mod")
            .setView(nameInput)
            .setPositiveButton("Import") { _, _ ->
                val name = nameInput.text?.toString().orEmpty()
                val progress = ThorDialogs.builder(activity)
                    .setTitle("Importing mod...")
                    .setCancelable(false)
                    .show()
                importZip(activity, uri, name) { error ->
                    progress.dismiss()
                    if (error != null) {
                        ThorDialogs.builder(activity).setTitle("Import failed").setMessage(error)
                            .setPositiveButton("OK", null).show()
                    } else {
                        showManagerDialog(activity)
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // The dialog currently on screen, if any. Every refresh (after import/delete/toggle) used to
    // build and show a brand-new AlertDialog on top of whichever one was already showing, rather
    // than replacing it - confirmed directly: importing a mod left the freshly updated dialog
    // stacked ON TOP of the still-open, stale pre-import one, so closing it (which looked like
    // "close the dialog") actually just revealed the stale copy underneath, making the import look
    // like it never took effect until the user backed out and reopened the whole thing fresh.
    private var currentDialog: AlertDialog? = null

    /** Lists installed mods with enable/disable + delete, and an Import button. Restart-required
     * note matches the same limitation Retro Rewind's own install/update flow already has. */
    fun showManagerDialog(activity: Activity) {
        currentDialog?.dismiss()
        val dp: (Int) -> Int = { (it * activity.resources.displayMetrics.density).toInt() }
        val container = LinearLayout(activity)
        container.orientation = LinearLayout.VERTICAL
        val pad = dp(16)
        container.setPadding(pad, pad, pad, pad)

        val note = TextView(activity)
        note.text = "Enabled mods load next time you start a race. Higher mods in the list win " +
            "when two mods replace the same file."
        note.setTextColor(Color.GRAY)
        note.textSize = 13f
        note.setPadding(0, 0, 0, dp(12))
        container.addView(note)

        val packs = installedPacks(activity)
        val emptyLabel = TextView(activity)
        emptyLabel.text = "No mods installed yet."
        emptyLabel.setPadding(0, dp(8), 0, dp(8))
        emptyLabel.visibility = if (packs.isEmpty()) View.VISIBLE else View.GONE
        container.addView(emptyLabel)

        val recyclerView = RecyclerView(activity)
        recyclerView.layoutManager = LinearLayoutManager(activity)
        recyclerView.visibility = if (packs.isEmpty()) View.GONE else View.VISIBLE
        val adapter = PackAdapter(activity, packs.toMutableList())
        recyclerView.adapter = adapter
        val itemTouchHelper = ItemTouchHelper(DragCallback(adapter))
        itemTouchHelper.attachToRecyclerView(recyclerView)
        adapter.dragStarter = { holder -> itemTouchHelper.startDrag(holder) }
        // Bounded rather than wrap_content: RecyclerView would otherwise measure every row
        // unbounded and grow the dialog past the screen with enough mods installed. Fixed height
        // here means the LIST scrolls internally - the outer ScrollView below (wrapping the whole
        // dialog body, note/button included) handles the separate case where even a short list
        // plus the surrounding text doesn't fit a small/landscape screen. Missing that outer
        // ScrollView is exactly what broke this before: an AlertDialog's setView content is NOT
        // auto-wrapped in a scroll container the way plain setMessage() text is, so on a screen
        // too short for note + list + button, the button (and part of the list) just got clipped
        // off entirely with nothing to scroll - looked like "mods disappeared and there's no
        // Import button" when every mod was actually still on disk and in .mod_order untouched.
        container.addView(recyclerView, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(220)))

        val importButton = Button(activity)
        importButton.text = "Import Mod (.zip)"
        importButton.setPadding(0, dp(16), 0, 0)
        importButton.setOnClickListener { startPickZip(activity) }
        container.addView(importButton)

        val scroll = ScrollView(activity)
        scroll.addView(container)

        val dialog =
            ThorDialogs.builder(activity)
                .setTitle("Mods")
                .setView(scroll)
                .setPositiveButton("Close", null)
                .setOnDismissListener { if (currentDialog === it) currentDialog = null }
                .show()
        currentDialog = dialog
    }

    // Drag-to-reorder priority: a dedicated "≡" handle per row starts the drag (rather than
    // enabling ItemTouchHelper's default long-press-anywhere) so long-pressing the checkbox or
    // Delete button doesn't fight with their own touch handling. Order is persisted (setOrder)
    // as soon as a drag ends, not deferred to dialog close, so it survives even if the user backs
    // out with the system back button rather than tapping Close.
    private class PackAdapter(private val activity: Activity, val items: MutableList<Pack>) :
        RecyclerView.Adapter<PackAdapter.VH>() {

        var dragStarter: ((RecyclerView.ViewHolder) -> Unit)? = null

        class VH(val row: LinearLayout, val handle: TextView, val checkBox: CheckBox, val deleteButton: Button) :
            RecyclerView.ViewHolder(row)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
            val dp: (Int) -> Int = { (it * activity.resources.displayMetrics.density).toInt() }
            val row = LinearLayout(activity)
            row.orientation = LinearLayout.HORIZONTAL
            row.gravity = android.view.Gravity.CENTER_VERTICAL
            row.setPadding(0, dp(6), 0, dp(6))

            val handle = TextView(activity)
            handle.text = "≡"
            handle.textSize = 20f
            handle.setPadding(dp(4), 0, dp(12), 0)
            row.addView(handle)

            val checkBox = CheckBox(activity)
            val checkParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            row.addView(checkBox, checkParams)

            val deleteButton = Button(activity)
            deleteButton.text = "Delete"
            row.addView(deleteButton)

            return VH(row, handle, checkBox, deleteButton)
        }

        override fun getItemCount(): Int = items.size

        override fun onBindViewHolder(holder: VH, position: Int) {
            val pack = items[position]
            holder.checkBox.text = pack.name
            holder.checkBox.setOnCheckedChangeListener(null)
            holder.checkBox.isChecked = pack.enabled
            holder.checkBox.setOnCheckedChangeListener { _, checked -> setEnabled(pack, checked) }

            holder.handle.setOnTouchListener { _, event ->
                if (event.action == MotionEvent.ACTION_DOWN) {
                    dragStarter?.invoke(holder)
                }
                false
            }

            holder.deleteButton.setOnClickListener {
                ThorDialogs.builder(activity)
                    .setTitle("Delete \"${pack.name}\"?")
                    .setPositiveButton("Delete") { _, _ ->
                        val index = items.indexOf(pack)
                        if (index >= 0) {
                            items.removeAt(index)
                            notifyItemRemoved(index)
                            delete(pack)
                            setOrder(activity, items.map { it.name })
                        }
                    }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        }

        fun moveItem(from: Int, to: Int) {
            val moved = items.removeAt(from)
            items.add(to, moved)
            notifyItemMoved(from, to)
        }

        fun persistOrder() {
            setOrder(activity, items.map { it.name })
        }
    }

    private class DragCallback(private val adapter: PackAdapter) :
        ItemTouchHelper.SimpleCallback(ItemTouchHelper.UP or ItemTouchHelper.DOWN, 0) {

        override fun isLongPressDragEnabled(): Boolean = false

        override fun onMove(
            recyclerView: RecyclerView,
            viewHolder: RecyclerView.ViewHolder,
            target: RecyclerView.ViewHolder,
        ): Boolean {
            adapter.moveItem(viewHolder.bindingAdapterPosition, target.bindingAdapterPosition)
            return true
        }

        override fun clearView(recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder) {
            super.clearView(recyclerView, viewHolder)
            adapter.persistOrder()
        }

        override fun onSwiped(viewHolder: RecyclerView.ViewHolder, direction: Int) {}
    }
}
