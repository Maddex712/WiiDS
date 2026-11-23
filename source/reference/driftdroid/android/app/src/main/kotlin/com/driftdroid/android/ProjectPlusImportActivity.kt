package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.format.Formatter
import android.view.View
import android.widget.*
import dev.wiids.brawl.BrawlStorage
import java.io.File

/** Android's document picker can read Downloads, microSD and document providers without app-folder access. */
class ProjectPlusImportActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private lateinit var rawLabel: TextView
    private lateinit var codeLabel: TextView
    private lateinit var progress: ProgressBar
    private lateinit var official: Button
    private lateinit var chooseZip: Button
    private lateinit var chooseRaw: Button
    private lateinit var chooseCode: Button
    private lateinit var importPair: Button
    private lateinit var enable: Button
    private lateinit var cancel: Button
    private var raw: Uri? = null
    private var codes: Uri? = null
    private var rawName = "No SD image selected"
    private var codeName = "No codes selected"
    private var message: String? = null
    private val tick = object : Runnable {
        override fun run() { refresh();handler.postDelayed(this, 150) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        raw = savedInstanceState?.getString("raw")?.let(Uri::parse)
        codes = savedInstanceState?.getString("codes")?.let(Uri::parse)
        rawName = savedInstanceState?.getString("rawName") ?: rawName
        codeName = savedInstanceState?.getString("codeName") ?: codeName
        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(32), dp(20), dp(32), dp(20))
            setBackgroundColor(0xff13263f.toInt())
        }
        fun text(value: String, size: Float = 16f) = TextView(this).apply {
            text = value;textSize = size;setTextColor(Color.WHITE);setPadding(0,dp(6),0,dp(8));column.addView(this)
        }
        fun button(value: String, action: () -> Unit) = Button(this).apply {
            text = value;isAllCaps = false;setOnClickListener { action() };column.addView(this)
        }
        text("Set up Project+", 27f)
        text("Install the official Project+ 3.2.1 pack after importing USA Brawl revision 2. WiiDS downloads the SD image and sets up the matching codes and launch configuration.")
        text("Already extracted the download? Choose sd.raw and RSBE01.gct separately. You do not need to create a WiiDS folder or copy files into Android/data.")
        status = text("Checking Project+ setup")
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = true;visibility = View.GONE;column.addView(this,LinearLayout.LayoutParams(-1,dp(10)))
        }
        official = button("Install official Project+") {
            ProjectPlusInstaller.requestOfficial(this) { message = it; refresh() }
        }
        chooseZip = button("Choose Project+ ZIP") { pick(PICK_ZIP) }
        text("Or choose the two extracted files:",18f)
        rawLabel = text(rawName)
        chooseRaw = button("Choose sd.raw") { pick(PICK_RAW) }
        codeLabel = text(codeName)
        chooseCode = button("Choose RSBE01.gct") { pick(PICK_CODES) }
        importPair = button("Import selected files") { confirmImport(null) }
        enable = button("Enable existing Project+ setup") {
            runCatching { GameStorage.requireIdle(this);GameStorage.enable(this, FrontendGame.PROJECT_PLUS) }
                .onSuccess { message = "Project+ is enabled. Open its case in the Brawl stack." }
                .onFailure { message = it.message }
            refresh()
        }
        cancel = button("Cancel import") { ProjectPlusInstaller.cancel();refresh() }
        text("You can also import the official Project+ Dolphin ZIP for 3.2.1. For other builds, choose the SD image and USA codes from the same download.")
        text("Your source files, Brawl disc and save folders are kept. Replaced Project+ mod files are retained in a backup.")
        button("Back") { onBackPressed() }
        setContentView(ScrollView(this).apply { isFillViewport = true;addView(column) })
        refresh()
    }

    override fun onResume() { super.onResume();handler.post(tick) }
    override fun onPause() { handler.removeCallbacks(tick);super.onPause() }
    override fun onSaveInstanceState(out: Bundle) {
        super.onSaveInstanceState(out)
        out.putString("raw",raw?.toString());out.putString("codes",codes?.toString())
        out.putString("rawName",rawName);out.putString("codeName",codeName)
    }
    private fun pick(kind: Int) {
        message = null
        runCatching {
            GameStorage.requireIdle(this)
            check(ThorGameLaunch.brawlInstalled(this)) { "Import your USA Brawl revision 2 disc first." }
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION), kind)
        }.onFailure { message = it.message ?: "No Android file picker is available on this device.";refresh() }
    }
    @Deprecated("Uses the existing WiiDS Activity result flow")
    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request,result,data)
        if (request !in listOf(PICK_ZIP,PICK_RAW,PICK_CODES) || result != RESULT_OK) return
        val uri = data?.data ?: return
        if (uri.scheme != "content") { message = "Choose the file using Android's document picker.";refresh();return }
        if (data.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION != 0)
            runCatching { contentResolver.takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION) }
        when (request) {
            PICK_RAW -> { raw = uri;rawName = "SD image selected" }
            PICK_CODES -> { codes = uri;codeName = "USA codes selected" }
            PICK_ZIP -> confirmImport(uri)
        }
        refresh()
    }
    private fun confirmImport(zip: Uri?) {
        val start = {
            runCatching { ProjectPlusInstaller.start(this,zip,if (zip == null) raw else null,if (zip == null) codes else null) }
                .onFailure { message = it.message ?: "Project+ import could not start." }
            refresh()
        }
        val root = BrawlStorage.projectPlusDirectory(this)
        if (File(root,"sd.raw").exists() || File(root,"Codes/RSBE01.gct").exists() || File(root,"Codes/gc.txt").exists()) {
            ThorDialogs.builder(this).setTitle("Replace Project+ mod files?")
                .setMessage("Any replaced SD image, codes, and launch configuration will be backed up. Brawl's disc and both games' save folders stay in place.")
                .setPositiveButton("Import") { _, _ -> start() }.setNegativeButton("Cancel",null).show()
        } else start()
    }
    private fun refresh() {
        val state = ProjectPlusInstaller.state
        val brawl = runCatching { ThorGameLaunch.brawlInstalled(this) }.getOrDefault(false)
        val existing = runCatching { BrawlStorage.projectPlusAvailable(this) }.getOrDefault(false)
        val blocker = if (state.running) null else SaveManagerProfiles.launchBlocker(this)
        val allowed = !state.running && brawl && blocker == null
        for (button in listOf(official,chooseZip,chooseRaw,chooseCode)) button.isEnabled = allowed
        importPair.isEnabled = allowed && raw != null && codes != null
        enable.visibility = if (existing && !state.running) View.VISIBLE else View.GONE
        enable.isEnabled = allowed
        cancel.visibility = if (state.running) View.VISIBLE else View.GONE
        cancel.isEnabled = state.running && state.cancellable
        progress.visibility = if (state.running) View.VISIBLE else View.GONE
        rawLabel.text = rawName;codeLabel.text = codeName
        status.text = when {
            state.running -> state.phase + if (state.bytes > 0) " — " + Formatter.formatShortFileSize(this,state.bytes) else ""
            message != null -> message
            state.finished -> state.error ?: state.phase
            blocker != null -> blocker
            !brawl -> "Import your USA Brawl revision 2 disc from Your games first."
            existing -> "Your existing Project+ files are ready. Enable them, or choose replacement files below."
            else -> "Choose a Project+ ZIP, or select the two files below."
        }
    }
    @Deprecated("Keep publication in progress visible")
    override fun onBackPressed() {
        val state = ProjectPlusInstaller.state
        if (!state.running) { super.onBackPressed();return }
        if (state.cancellable) ProjectPlusInstaller.cancel()
        Toast.makeText(this,if (state.cancellable) "Cancelling import. Your current files are kept." else "Finishing Project+ setup. Please wait.",Toast.LENGTH_SHORT).show()
    }
    private fun dp(value: Int) = (resources.displayMetrics.density * value).toInt()
    companion object { private const val PICK_ZIP = 730;private const val PICK_RAW = 731;private const val PICK_CODES = 732 }
}
