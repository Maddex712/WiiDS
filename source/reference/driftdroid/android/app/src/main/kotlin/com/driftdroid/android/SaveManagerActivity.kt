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
import com.driftdroid.android.saves.KartPlayerData
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
    private var playerMode = false
    private var ratingDriver: KartPlayerData.Driver? = null
    private var selected = FrontendGame.NSMB
    private lateinit var status: TextView
    private lateinit var controls: LinearLayout
    private val buttons = ArrayList<Button>()
    private var art: ThorMenuArtwork? = null
    private var chrome: org.json.JSONObject? = null
    private var companion: ThorLowerCompanion? = null
    private var exportFile: File? = null
    private var pendingImport: File? = null
    private var awaitingPicker = false
    private var importGame = FrontendGame.NSMB
    private val folder get() = File(SaveManagerProfiles.work(this), "backups")
    private fun dated(game: FrontendGame, reason: String, shared: Boolean = playerMode) = File(folder,
        "${if (shared) KartPlayerData.ID else game.id}-${System.currentTimeMillis()}-$reason-${UUID.randomUUID()}.zip")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        playerMode = savedInstanceState?.getBoolean("players") ?: intent.getBooleanExtra("players", false)
        awaitingPicker = savedInstanceState?.getBoolean("awaiting-picker") ?: false
        selected = FrontendGame.fromId(savedInstanceState?.getString("game") ?: intent.getStringExtra("game")) ?: if (playerMode) FrontendGame.RETRO_REWIND else FrontendGame.NSMB
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
        layout.addView(label(if (playerMode) "Player profiles" else "Save manager", 25f))
        layout.addView(label(if (playerMode) "Import a kart save, change your driver name, or choose a Mii. Close games first." else "Close games before managing saves. Originals and mods have separate backups.", 13f))
        status = label("", 15f).apply { setPadding(0, dp(12), 0, dp(12)); minLines = 3; maxLines = 4 }
        layout.addView(status)
        val scroll = ScrollView(this)
        controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(controls); layout.addView(scroll, LinearLayout.LayoutParams(-1, 0, 1f))
        button("Choose game") { chooseGame() }
        if (!playerMode) backupButtons()
        button(if (playerMode) "Import existing player save..." else "Import a Dolphin save...") { importPlayerSave() }
        if (playerMode) {
            button("Rename driver...") { chooseDriver { driver -> renameDriver(driver) } }
            button("Choose driver Mii...") { chooseDriver { driver -> chooseDriverMii(driver) } }
            button("Import Retro Rewind ratings...") {
                if (selected != FrontendGame.RETRO_REWIND) status.text = "Choose Retro Rewind first."
                else chooseDriver { driver ->
                    ThorDialogs.builder(this).setTitle("Import ratings for ${driver.name}?")
                        .setMessage("Choose RRRating.pul from the same source as your Retro Rewind save. Only the matching online account's ratings are copied; other local ratings are kept. The online server can update ratings at login.")
                        .setPositiveButton("Choose ratings") { _, _ ->
                            ratingDriver = driver
                            pick(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), RATINGS)
                        }.setNegativeButton("Cancel", null).show()
                }
            }
            button("Game save backups...") {
                startActivity(Intent(this, SaveManagerActivity::class.java).putExtra("game", selected.id))
            }
        } else button("Driver names / player data...") {
            startActivity(Intent(this, SaveManagerActivity::class.java).putExtra("players", true))
        }
        if (playerMode) backupButtons()
        button("Back to library") { finish() }
        layout.addView(label("A  Select     B  Back     D-pad  Move", 13f))
        setContentView(layout)
        if (!BuildConfig.THOR_BUILD) { finish(); return }
        companion = ThorLowerCompanion(this, artFolder, if (playerMode) "Player profiles" else "Save manager")
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
        if (!awaitingPicker) recover()
    }
    private fun pick(request: Intent, code: Int) { awaitingPicker = true; startActivityForResult(request, code) }
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
        val games = FrontendGame.values().filter { (!playerMode || it == FrontendGame.MARIO_KART || it == FrontendGame.RETRO_REWIND) && (it != FrontendGame.RETRO_REWIND || BuildConfig.THOR_RETRO_REWIND) }
        ThorDialogs.builder(this).setTitle("Choose game").setItems(games.map { it.label }.toTypedArray()) { _, which ->
            selected = games[which]; refresh()
        }.setNegativeButton("Back", null).show()
    }
    private fun backupButtons() {
        button("Back up now") { backup() }
        button("Export a backup") { chooseBackup(false) }
        button("Restore a local backup") { chooseBackup(true) }
        button("Restore a backup file...") {
            importGame = selected
            pick(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/zip")
                .addCategory(Intent.CATEGORY_OPENABLE), IMPORT)
        }
    }
    private fun importPlayerSave() {
        val game = selected
        if (game == FrontendGame.BRAWL || game == FrontendGame.PROJECT_PLUS || game == FrontendGame.MKDD || game == FrontendGame.MKDD_PLUS) {
            status.text = "This game supports WiiDS backup and restore. Dolphin save-file import is not supported yet."; return
        }
        ThorDialogs.builder(this).setTitle("Import for ${game.label}")
            .setMessage(DolphinSaveImport.instructions(game.id) + "\n\nCopy the file to Downloads, then choose it here. This replaces all licenses in this game's save and backs up the current save first.\n\n" +
                if (game == FrontendGame.MARIO_KART || game == FrontendGame.RETRO_REWIND)
                    "Your saved online account IDs and progress are retained. Miis and console identity are separate. Import your Mii in Options > Mii Data, then use Choose driver Mii if needed. Retro Rewind ratings can be imported separately from RRRating.pul. Online login is still subject to the server's identity checks."
                else "Dolphin save states and full NAND/card backups are not supported.")
            .setPositiveButton("Choose save") { _, _ ->
                importGame = game
                pick(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), DOLPHIN)
            }.setNegativeButton("Cancel", null).show()
    }
    private fun driverLabel(driver: KartPlayerData.Driver): String {
        val title = when (driver.root) { "mkw" -> "Mario Kart Wii"; "retro-alt" -> "Retro Rewind (Separate Save)"; else -> "Retro Rewind" }
        return "$title - License ${driver.slot + 1}: ${driver.name}"
    }
    private fun chooseDriver(next: (KartPlayerData.Driver) -> Unit) {
        val game = selected
        var drivers = emptyList<KartPlayerData.Driver>()
        operation("Reading ${game.label} drivers...", after = {
            ThorDialogs.builder(this).setTitle("Choose driver")
                .setItems(drivers.map(::driverLabel).toTypedArray()) { _, index -> next(drivers[index]) }
                .setNegativeButton("Cancel", null).show()
        }) {
            drivers = KartPlayerData.drivers(SaveManagerProfiles.kartPlayers(this), game.id)
            if (drivers.isEmpty()) error("No licenses yet. Create one in the game, or import your existing player save first.")
            "Choose the driver to edit."
        }
    }
    private fun renameDriver(driver: KartPlayerData.Driver) {
        val input = EditText(this).apply { setText(driver.name); setSingleLine(true); selectAll(); hint = "Driver name (up to 10 characters)" }
        val dialog = ThorDialogs.builder(this).setTitle("Rename ${driver.name}").setView(input)
            .setMessage("Renames this Mii and every linked license in Mario Kart Wii and Retro Rewind. Friend codes, online accounts and progress are kept. A shared player backup is created first.")
            .setPositiveButton("Back up and rename", null).setNegativeButton("Cancel", null).create()
        dialog.setOnShowListener { dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            val name = input.text.toString()
            try { KartPlayerData.encodeName(name) } catch (e: java.io.IOException) { input.error = e.message; return@setOnClickListener }
            dialog.dismiss()
            operation("Backing up and renaming driver...") {
                KartPlayerData.rename(SaveManagerProfiles.kartPlayers(this), driver, name, SaveManagerProfiles.work(this), dated(selected, "before-name", true))
                "Driver renamed to $name. Shared player backup saved; account IDs and progress kept."
            }
        } }
        dialog.show()
    }
    private fun chooseDriverMii(driver: KartPlayerData.Driver) {
        var miis = emptyList<KartPlayerData.Mii>()
        operation("Reading Miis...", after = {
            ThorDialogs.builder(this).setTitle("Choose Mii for ${driver.name}")
                .setItems(miis.map { "${it.slot + 1}: ${it.name}" }.toTypedArray()) { _, index ->
                    val mii = miis[index]
                    ThorDialogs.builder(this).setTitle("Use ${mii.name}?")
                        .setMessage("Change only ${driverLabel(driver)} to this Mii's name and appearance. Its online account, friend code and progress are kept. A shared player backup is created first.")
                        .setPositiveButton("Back up and use Mii") { _, _ ->
                            operation("Backing up and updating driver Mii...") {
                                KartPlayerData.chooseMii(SaveManagerProfiles.kartPlayers(this), driver, mii, SaveManagerProfiles.work(this), dated(selected, "before-mii", true))
                                "Driver Mii updated. Account and progress kept; player backup saved."
                            }
                        }.setNegativeButton("Cancel", null).show()
                }.setNegativeButton("Cancel", null).show()
        }) {
            miis = KartPlayerData.miis(SaveManagerProfiles.kartPlayers(this))
            if (miis.isEmpty()) error("Import a single Mii in Options > Mii Data first.")
            "Choose an existing Mii."
        }
    }
    private fun currentProfile(game: FrontendGame) = if (playerMode) SaveManagerProfiles.kartPlayers(this) else SaveManagerProfiles.profile(this, game)
    private fun refresh() {
        if (!SaveManagerProfiles.busy.get()) status.text = "${selected.label}\n${backups().size} local backups. ${if (playerMode) "Player backups include both kart games, shared Miis and Retro Rewind settings." else "Achievements, controls and reserve items stay separate."}"
    }
    private fun backups(): List<File> = folder.listFiles()?.filter {
        it.isFile && if (playerMode) it.name.startsWith(KartPlayerData.ID + "-") && it.name.endsWith(".zip") else FrontendGame.fromBackupFilename(it.name) == selected
    }?.sortedByDescending { it.name.substringAfter((if (playerMode) KartPlayerData.ID else selected.id) + "-").substringBefore('-').toLongOrNull() ?: 0L }.orEmpty()
    private fun backup() {
        val game = selected
        operation("Backing up ${game.label}…") {
            SaveArchive.backup(currentProfile(game), dated(game, "manual"), false)
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
                    pick(Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip")
                        .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, "WiiDS-${files[which].name}"), EXPORT)
                }
            }.setNegativeButton("Back", null).show()
    }
    private fun confirmRestore(file: File, game: FrontendGame, imported: Boolean) {
        if (imported) pendingImport = file
        ThorDialogs.builder(this).setTitle("Restore ${game.label}?")
            .setMessage(if (playerMode) "Restore both Mario Kart Wii and Retro Rewind saves, shared Miis and Retro Rewind settings from this player backup? Current data is backed up first. Progress since this backup will be replaced in both games." else "Replace this game's in-game saves with the selected WiiDS backup? Its game identity and file hashes will be checked first. Your current saves will be kept in a dated backup.")
            .setPositiveButton("Restore") { _, _ ->
                if (imported) pendingImport = null
                val accepted = operation("Checking and restoring ${game.label}…", complete = { if (imported) file.delete() }) {
                    try {
                        SaveArchive.restore(currentProfile(game), file,
                            SaveManagerProfiles.work(this), dated(game, "before-restore"))
                        "${game.label} saves restored and verified. Start the game from the library to check them."
                    } finally { if (imported) file.delete() }
                }
                if (!accepted && imported) file.delete()
            }.setNegativeButton("Cancel") { _, _ -> if (imported) { pendingImport = null; file.delete() } }
            .setOnCancelListener { if (imported) { pendingImport = null; file.delete() } }.show()
    }
    private fun confirmDolphin(plan: DolphinSaveImport.Plan, game: FrontendGame) {
        ThorDialogs.builder(this).setTitle("Import into ${game.label}?")
            .setMessage("${plan.format}\n\n${plan.description}\n\nYour current saves will be kept in a dated backup. This replaces the selected game's progress.")
            .setPositiveButton("Back up and import") { _, _ ->
                operation("Backing up and importing ${game.label}...") {
                    DolphinSaveImport.restore(SaveManagerProfiles.profile(this, game), plan,
                        SaveManagerProfiles.work(this), dated(game, "before-import", false))
                    "${game.label} save imported and verified. Your previous save is in Save manager for ${game.label} (Game save backups in Player profiles). Start the game from the library to check your progress."
                }
            }.setNegativeButton("Cancel", null).show()
    }
    @Deprecated("Android compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        awaitingPicker = false
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            RATINGS -> {
                val driver = ratingDriver
                ratingDriver = null
                if (driver == null) { status.text = "Reopen Import Retro Rewind ratings and choose the driver again."; return }
                var bytes: ByteArray? = null
                operation("Checking rating file...", after = {
                    val checked = bytes ?: return@operation
                    ThorDialogs.builder(this).setTitle("Import ratings for ${driver.name}?")
                        .setMessage("Back up both kart saves, shared Miis and Retro Rewind settings, then import only this driver's matching account ratings?")
                        .setPositiveButton("Back up and import") { _, _ ->
                            operation("Importing matching ratings...") {
                                KartPlayerData.importRatings(SaveManagerProfiles.kartPlayers(this), driver, checked,
                                    SaveManagerProfiles.work(this), dated(selected, "before-ratings", true))
                                "Matching Retro Rewind ratings imported. Your player backup is available in Player profiles."
                            }
                        }.setNegativeButton("Cancel", null).show()
                }) {
                    bytes = (contentResolver.openInputStream(uri) ?: error("Cannot read ratings.")).use { input ->
                        val out = java.io.ByteArrayOutputStream(); val buffer = ByteArray(1641); var count: Int
                        while (input.read(buffer).also { count = it } != -1) {
                            if (out.size() + count > 1640) error("Unsupported rating file size.")
                            out.write(buffer, 0, count)
                        }; out.toByteArray()
                    }
                    "Rating file read. Confirm the import."
                }
            }
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
                val profile = if (playerMode) selected else FrontendGame.fromBackupFilename(source.name) ?: return
                operation("Exporting backup…") {
                    SaveArchive.verify(currentProfile(profile), source, null)
                    val output = contentResolver.openOutputStream(uri, "wt") ?: error("Cannot open backup destination.")
                    output.use { out -> FileInputStream(source).use { SaveArchive.copy(it, out) } }
                    "Backup exported. Keep this copy if you uninstall WiiDS."
                }
            }
            IMPORT -> {
                val game = importGame
                var staged: File? = null
                operation("Checking backup file…", abandoned = { staged?.delete() }, after = {
                    staged?.let { confirmRestore(it, game, true) }
                }) {
                    val temp = File(cacheDir.canonicalFile, "wiids-save-${UUID.randomUUID()}.zip")
                    try {
                        (contentResolver.openInputStream(uri) ?: error("Cannot read backup file.")).use { input ->
                            FileOutputStream(temp).use { SaveArchive.copy(input, it); it.fd.sync() }
                        }
                        SaveArchive.verify(currentProfile(game), temp, null)
                        staged = temp
                        "Backup verified for ${game.label}."
                    } catch (failure: Exception) { temp.delete(); throw failure }
                }
            }
        }
    }
    private fun recover() {
        operation("Recovering interrupted save restore…", recovery = true) {
            val work = SaveManagerProfiles.work(this)
            if (SaveArchive.pending(work)) {
                val p = java.util.Properties()
                FileInputStream(File(work, "restore.properties")).use { p.load(it) }
                val id = p.getProperty("profile")
                val profile = if (id == KartPlayerData.ID) SaveManagerProfiles.kartPlayers(this) else {
                    val game = FrontendGame.fromId(id) ?: error("Unknown recovery profile. Keep your backups.")
                    SaveManagerProfiles.profile(this, game)
                }
                SaveArchive.recover(profile, work)
            }
            SaveManagerProfiles.cleanup(this)
            "${selected.label}\nSave staging checked. Your verified backup archives are available."
        }
    }
    private fun operation(message: String, after: (() -> Unit)? = null, abandoned: (() -> Unit)? = null, complete: (() -> Unit)? = null, recovery: Boolean = false, action: () -> String): Boolean {
        synchronized(com.driftdroid.android.rom.GameInstaller) {
            if (com.driftdroid.android.rom.GameInstaller.running ||
                com.driftdroid.android.rom.EclipseMod.state.running ||
                com.driftdroid.android.rom.MsmbwiiMod.state.running ||
                com.driftdroid.android.rom.WiiDsArt.building != null || WiiDsUpdater.installing.get()) {
                status.text = "Finish or cancel game setup before managing saves."
                return false
            }
            if (!SaveManagerProfiles.busy.compareAndSet(false, true)) { status.text = "Wait for the current save operation to finish."; return false }
        }
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
                    lock.use { SaveManagerProfiles.cleanup(this); action() }
                }
            }
            try { complete?.invoke() } finally { SaveManagerProfiles.busy.set(false) }
            runOnUiThread {
                if (isFinishing || isDestroyed) { abandoned?.invoke(); return@runOnUiThread }
                buttons.forEach { it.isEnabled = true }
                focused?.requestFocus()
                status.text = result.getOrElse { "Save operation stopped: ${it.message ?: "unknown error"}. Your verified backups are kept." }
                if (result.isSuccess) after?.invoke()
            }
        }.apply { name = "WiiDsSaveManager"; isDaemon = true }.start()
        return true
    }
    override fun onDestroy() { pendingImport?.delete(); pendingImport = null; super.onDestroy() }
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
        outState.putBoolean("players", playerMode)
        outState.putBoolean("awaiting-picker", awaitingPicker)
        outState.putString("game", selected.id); outState.putString("import-game", importGame.id)
        exportFile?.let { outState.putString("export", it.name) }
        super.onSaveInstanceState(outState)
    }
    @Deprecated("Android compatibility")
    override fun onBackPressed() {
        if (SaveManagerProfiles.busy.get()) Toast.makeText(this, "Wait for the save operation to finish.", Toast.LENGTH_SHORT).show()
        else super.onBackPressed()
    }
    companion object { private const val IMPORT = 4101; private const val EXPORT = 4102; private const val DOLPHIN = 4103; private const val RATINGS = 4104 }
}
