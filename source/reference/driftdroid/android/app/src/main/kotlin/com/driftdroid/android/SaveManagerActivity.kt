package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.RectF
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.StateListDrawable
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.*
import com.driftdroid.android.saves.SaveArchive
import com.driftdroid.android.saves.DolphinSaveImport
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.RandomAccessFile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

/** Library-only save tools. Native games never share a process with a restore. */
class SaveManagerActivity : Activity() {
    private var selected = FrontendGame.NSMB
    private lateinit var status: TextView
    private lateinit var controls: LinearLayout
    private val buttons = ArrayList<Button>()
    private var art: ThorMenuArtwork? = null
    private var chrome: org.json.JSONObject? = null
    private var companion: ThorLowerCompanion? = null
    private var exportFile: File? = null
    private var importGame = FrontendGame.NSMB
    private val folder get() = File(SaveManagerProfiles.work(this), "backups")
    private fun dated(game: FrontendGame, reason: String) = File(folder,
        "${game.id}-${System.currentTimeMillis()}-$reason-${UUID.randomUUID()}.zip")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        selected = FrontendGame.fromId(savedInstanceState?.getString("game")) ?: FrontendGame.NSMB
        importGame = FrontendGame.fromId(savedInstanceState?.getString("import-game")) ?: selected
        exportFile = savedInstanceState?.getString("export")?.let { name ->
            File(folder, name).takeIf { it.parentFile?.canonicalFile == folder.canonicalFile && it.isFile }
        }
        val artFolder = File(filesDir, "WiiCompiled/ThorMap")
        art = runCatching { ThorMenuArtwork(artFolder) }.getOrNull()
        chrome = runCatching { org.json.JSONObject(File(artFolder, "menu.json").readText()).getJSONObject("images") }.getOrNull()
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(26), dp(16), dp(26), dp(16)); setBackgroundColor(Color.rgb(22, 27, 34))
            image("background")?.let { background = BitmapDrawable(resources, it) }
        }
        layout.addView(label("Save manager", 25f))
        layout.addView(label("Close games before managing saves. Originals and mods have separate backups.", 13f))
        status = label("", 15f).apply { setPadding(0, dp(12), 0, dp(12)); minLines = 3; maxLines = 4 }
        layout.addView(status)
        val scroll = ScrollView(this)
        controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(controls); layout.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        button("Choose game") { chooseGame() }
        button("Back up now") { backup() }
        button("Export a backup") { chooseBackup(false) }
        button("Restore a local backup") { chooseBackup(true) }
        button("Restore a backup file...") {
            importGame = selected
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/zip")
                .addCategory(Intent.CATEGORY_OPENABLE), IMPORT)
        }
        button("Import a Dolphin save...") {
            val game = selected
            if(game==FrontendGame.BRAWL) { status.text="Brawl supports WiiDS backup and restore here. Dolphin save-file import is not supported yet.";return@button }
            ThorDialogs.builder(this).setTitle("Import for ${game.label}")
                .setMessage(DolphinSaveImport.instructions(game.id) + "\n\nCopy the file to Downloads, then choose it here. No root is needed. Dolphin save states and full NAND/card backups are not supported.")
                .setPositiveButton("Choose save") { _, _ ->
                    importGame = game
                    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*")
                        .addCategory(Intent.CATEGORY_OPENABLE), DOLPHIN)
                }.setNegativeButton("Cancel", null).show()
        }
        button("Back to library") { finish() }
        layout.addView(label("A  Select     B  Back     D-pad  Move", 13f))
        setContentView(layout)
        if (!BuildConfig.THOR_BUILD) { finish(); return }
        companion = ThorLowerCompanion(this, artFolder, "Save manager")
        status.addTextChangedListener(object : android.text.TextWatcher {
            private var styling = false
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                if (styling) return
                styling = true; status.text = styled(status.text.toString()); styling = false
                companion?.setStatus(selected.label, status.text.toString().substringAfter('\n', status.text.toString()))
            }
        })
        for ((i, button) in buttons.withIndex()) {
            button.nextFocusLeftId = buttons[if (i % 2 == 1) i - 1 else i].id
            button.nextFocusRightId = buttons[if (i % 2 == 0 && i + 1 < buttons.size) i + 1 else i].id
            button.nextFocusUpId = buttons[maxOf(0, i - 2)].id
            button.nextFocusDownId = buttons[minOf(buttons.lastIndex, i + 2)].id
        }
        buttons.first().requestFocus()
        fullscreen()
        refresh()
        if (SaveArchive.pending(SaveManagerProfiles.work(this))) recover()
    }
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun styled(text: String): CharSequence = art?.styledText(text.replace("…", "...")) ?: text
    private fun label(text: String, size: Float) = TextView(this).apply {
        this.text = styled(text); textSize = size; setTextColor(Color.WHITE); includeFontPadding = true
    }
    private fun image(role: String): Bitmap? = runCatching { art?.bitmap(chrome!!.getJSONObject(role).getString("texture")) }.getOrNull()
    private fun buttonArt(bitmap: Bitmap): Drawable = object : Drawable() {
        private val target = RectF()
        private var opacity = 255
        override fun draw(canvas: Canvas) { target.set(bounds); art?.button(canvas, bitmap, target, opacity) }
        override fun setAlpha(alpha: Int) { opacity = alpha; invalidateSelf() }
        override fun setColorFilter(filter: android.graphics.ColorFilter?) {}
        @Deprecated("Android compatibility") override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
    }
    private fun button(label: String, action: () -> Unit) {
        if (buttons.size % 2 == 0) controls.addView(LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL })
        val row = controls.getChildAt(controls.childCount - 1) as LinearLayout
        val button = Button(this).apply {
            id = View.generateViewId(); text = styled(label); textSize = 17f; isAllCaps = false
            isFocusableInTouchMode = true; minHeight = dp(64); setPadding(dp(14), dp(10), dp(14), dp(10))
            // Focus before Button handles DOWN so the first tap also activates it.
            setOnTouchListener { view, event ->
                if (event.actionMasked == MotionEvent.ACTION_DOWN) view.requestFocus()
                false
            }
            val normal = image("button"); val focus = image("button_selected")
            if (normal != null && focus != null) {
                backgroundTintList = null
                background = StateListDrawable().apply {
                    addState(intArrayOf(android.R.attr.state_pressed), buttonArt(focus))
                    addState(intArrayOf(android.R.attr.state_focused), buttonArt(focus))
                    addState(intArrayOf(), buttonArt(normal))
                }
            }
            setOnClickListener { action() }
        }
        row.addView(button, LinearLayout.LayoutParams(0, -2, 1f).apply { setMargins(dp(4), dp(5), dp(4), dp(5)) })
        buttons += button
    }
    private fun chooseGame() {
        val games = FrontendGame.values().filter { it != FrontendGame.RETRO_REWIND || BuildConfig.THOR_RETRO_REWIND }
        ThorDialogs.builder(this).setTitle("Choose game").setItems(games.map { it.label }.toTypedArray()) { _, which ->
            selected = games[which]; refresh()
        }.setNegativeButton("Back", null).show()
    }
    private fun refresh() {
        if (!SaveManagerProfiles.busy.get()) status.text = "${selected.label}\n${backups().size} local backups. Achievements, controls and reserve items stay separate."
    }
    private fun backups(): List<File> = folder.listFiles()?.filter {
        it.isFile && it.name.startsWith(selected.id + "-") && it.extension == "zip"
    }?.sortedByDescending { it.name.substringAfter(selected.id + "-").substringBefore('-').toLongOrNull() ?: 0L }.orEmpty()
    private fun backup() {
        val game = selected
        operation("Backing up ${game.label}…") {
            SaveArchive.backup(SaveManagerProfiles.profile(this, game), dated(game, "manual"), false)
            "${game.label} backup saved and verified. Use Export a backup to copy it outside WiiDS."
        }
    }
    private fun chooseBackup(restore: Boolean) {
        val files = backups()
        if (files.isEmpty()) { status.text = "No local backups for ${selected.label}. Choose Back up now first."; return }
        val dates = SimpleDateFormat("MMM d, yyyy  h:mm:ss a", Locale.getDefault())
        ThorDialogs.builder(this).setTitle(if (restore) "Restore backup" else "Export backup")
            .setItems(files.map { dates.format(Date(it.lastModified())) + when {
                it.name.contains("-before-restore-") -> " · before restore"
                it.name.contains("-before-import-") -> " · before import"
                else -> ""
            } }.toTypedArray()) { _, which ->
                if (restore) confirmRestore(files[which], selected, false)
                else {
                    exportFile = files[which]
                    startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip")
                        .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, "WiiDS-${files[which].name}"), EXPORT)
                }
            }.setNegativeButton("Back", null).show()
    }
    private fun confirmRestore(file: File, game: FrontendGame, imported: Boolean) {
        ThorDialogs.builder(this).setTitle("Restore ${game.label}?")
            .setMessage("Replace this game's in-game saves with the selected WiiDS backup? Its game identity and file hashes will be checked first. Your current saves will be kept in a dated backup.")
            .setPositiveButton("Restore") { _, _ ->
                operation("Checking and restoring ${game.label}…") {
                    try {
                        SaveArchive.restore(SaveManagerProfiles.profile(this, game), file,
                            SaveManagerProfiles.work(this), dated(game, "before-restore"))
                        "${game.label} saves restored and verified. Start the game from the library to check them."
                    } finally { if (imported) file.delete() }
                }
            }.setNegativeButton("Cancel") { _, _ -> if (imported) file.delete() }.show()
    }
    private fun confirmDolphin(plan: DolphinSaveImport.Plan, game: FrontendGame) {
        ThorDialogs.builder(this).setTitle("Import into ${game.label}?")
            .setMessage("${plan.format}\n\n${plan.description}\n\nYour current saves will be kept in a dated backup. This replaces the selected game's progress.")
            .setPositiveButton("Back up and import") { _, _ ->
                operation("Backing up and importing ${game.label}...") {
                    DolphinSaveImport.restore(SaveManagerProfiles.profile(this, game), plan,
                        SaveManagerProfiles.work(this), dated(game, "before-import"))
                    "${game.label} save imported and verified. Your previous saves are in a local backup. Start the game from the library to check your progress."
                }
            }.setNegativeButton("Cancel", null).show()
    }
    @Deprecated("Android compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            DOLPHIN -> {
                val game = importGame
                var plan: DolphinSaveImport.Plan? = null
                operation("Checking Dolphin save for ${game.label}...", after = {
                    plan?.let { confirmDolphin(it, game) }
                }) {
                    // Check the storage/profile restrictions before offering a replacement.
                    SaveManagerProfiles.profile(this, game)
                    val bytes = (contentResolver.openInputStream(uri) ?: error("Cannot read save file.")).use {
                        DolphinSaveImport.read(it)
                    }
                    plan = DolphinSaveImport.inspect(game.id, bytes)
                    "Save checked for ${game.label}. Review the import before continuing."
                }
            }
            EXPORT -> {
                val source = exportFile ?: return
                val profile = FrontendGame.values().firstOrNull { source.name.startsWith(it.id + "-") } ?: return
                operation("Exporting backup…") {
                    SaveArchive.verify(SaveManagerProfiles.profile(this, profile), source, null)
                    val output = contentResolver.openOutputStream(uri, "wt") ?: error("Cannot open backup destination.")
                    output.use { out -> FileInputStream(source).use { SaveArchive.copy(it, out) } }
                    "Backup exported. Keep this copy if you uninstall WiiDS."
                }
            }
            IMPORT -> {
                val game = importGame
                var staged: File? = null
                operation("Checking backup file…", after = {
                    staged?.let { confirmRestore(it, game, true) }
                }) {
                    val temp = File(cacheDir.canonicalFile, "wiids-save-${UUID.randomUUID()}.zip")
                    try {
                        (contentResolver.openInputStream(uri) ?: error("Cannot read backup file.")).use { input ->
                            FileOutputStream(temp).use { SaveArchive.copy(input, it); it.fd.sync() }
                        }
                        SaveArchive.verify(SaveManagerProfiles.profile(this, game), temp, null)
                        staged = temp
                        "Backup verified for ${game.label}."
                    } catch (failure: Exception) { temp.delete(); throw failure }
                }
            }
        }
    }
    private fun recover() {
        operation("Recovering interrupted save restore…", recovery = true) {
            val p = java.util.Properties()
            FileInputStream(File(SaveManagerProfiles.work(this), "restore.properties")).use { p.load(it) }
            val game = FrontendGame.fromId(p.getProperty("profile")) ?: error("Unknown recovery profile. Keep your backups.")
            SaveArchive.recover(SaveManagerProfiles.profile(this, game), SaveManagerProfiles.work(this))
            "Previous saves recovered. Your backup archives are still available."
        }
    }
    private fun operation(message: String, after: (() -> Unit)? = null, recovery: Boolean = false, action: () -> String) {
        if (!SaveManagerProfiles.busy.compareAndSet(false, true)) return
        status.text = message
        val focused = currentFocus
        buttons.forEach { it.isEnabled = false }
        Thread {
            val result = runCatching {
                GameStorage.blocker(this)?.let { error(it) }
                SaveManagerProfiles.requireStopped(this)
                val work = SaveManagerProfiles.work(this); SaveArchive.mkdir(work)
                if (!recovery && SaveArchive.pending(work)) error("Recover the interrupted restore by reopening Save manager first.")
                RandomAccessFile(File(work, ".lock"), "rw").use { lockFile ->
                    val lock = lockFile.channel.tryLock() ?: error("Another save operation is running.")
                    lock.use { action() }
                }
            }
            SaveManagerProfiles.busy.set(false)
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                buttons.forEach { it.isEnabled = true }
                focused?.requestFocus()
                status.text = result.getOrElse { "Save operation stopped: ${it.message ?: "unknown error"}. Your verified backups are kept." }
                if (result.isSuccess) after?.invoke()
            }
        }.apply { name = "WiiDsSaveManager"; isDaemon = true }.start()
    }
    private fun fullscreen() {
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.apply {
            systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsets.Type.systemBars())
        }
    }
    override fun onWindowFocusChanged(hasFocus: Boolean) { super.onWindowFocusChanged(hasFocus); if (hasFocus) fullscreen() }
    override fun onResume() { super.onResume(); companion?.start(); fullscreen() }
    override fun onPause() { companion?.stop(); super.onPause() }
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_B) {
            if (event.action == KeyEvent.ACTION_UP) onBackPressed()
            return true
        }
        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_A) {
            val button = currentFocus as? Button ?: return true
            if (button.isEnabled) {
                button.isPressed = event.action == KeyEvent.ACTION_DOWN
                if (event.action == KeyEvent.ACTION_UP && !event.isCanceled) button.performClick()
            }
            return true
        }
        return super.dispatchKeyEvent(event)
    }
    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("game", selected.id); outState.putString("import-game", importGame.id)
        exportFile?.let { outState.putString("export", it.name) }
        super.onSaveInstanceState(outState)
    }
    @Deprecated("Android compatibility")
    override fun onBackPressed() {
        if (SaveManagerProfiles.busy.get()) Toast.makeText(this, "Wait for the save operation to finish.", Toast.LENGTH_SHORT).show()
        else super.onBackPressed()
    }
    companion object { private const val IMPORT = 4101; private const val EXPORT = 4102; private const val DOLPHIN = 4103 }
}
