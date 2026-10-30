package com.driftdroid.android

import android.app.Activity
import android.content.Intent
import android.content.ComponentName
import android.graphics.*
import android.graphics.drawable.*
import android.os.*
import android.view.*
import android.widget.*
import com.driftdroid.android.storage.SafeOffload
import com.driftdroid.android.rom.GameInstaller
import java.io.File

/** Uses the same game-derived artwork and controls as the existing Save manager. */
class GameStorageActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var controls: LinearLayout
    private val buttons=ArrayList<Button>()
    private val games=FrontendGame.values().toList()
    private var art:ThorMenuArtwork?=null
    private var chrome:org.json.JSONObject?=null
    private var companion:ThorLowerCompanion?=null
    private var scanning=false
    private var refreshNeeded=true
    private var wasBusy=false
    private val sizes=HashMap<FrontendGame,Long>()
    private var selected=FrontendGame.MARIO_KART
    private var aPressed=false
    private val handler=Handler(Looper.getMainLooper())
    private val poll=object:Runnable {
        override fun run() {
            val busy=GameStorage.busy.get()
            if(wasBusy&&!busy)refreshNeeded=true
            wasBusy=busy
            if(refreshNeeded&&!busy&&!scanning) { refreshNeeded=false;refresh() }
            handler.postDelayed(this,1500)
        }
    }
    override fun onCreate(savedInstanceState:Bundle?) {
        super.onCreate(savedInstanceState)
        val folder=File(filesDir,"WiiCompiled/ThorMap")
        art=runCatching { ThorMenuArtwork(folder) }.getOrNull()
        chrome=runCatching { org.json.JSONObject(File(folder,"menu.json").readText()).getJSONObject("images") }.getOrNull()
        val layout=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL;setPadding(dp(26),dp(16),dp(26),dp(16))
            setBackgroundColor(Color.rgb(22,27,34));image("background")?.let { background=BitmapDrawable(resources,it) }
        }
        layout.addView(label("Manage games",25f))
        layout.addView(label("Offload game files to free space. Saves, backups, settings and case art are kept.",13f))
        status=label("Checking stored game files...",15f).apply { minLines=2;setPadding(0,dp(8),0,dp(8)) }
        layout.addView(status)
        controls=LinearLayout(this).apply { orientation=LinearLayout.VERTICAL }
        val scroll=ScrollView(this);scroll.addView(controls);layout.addView(scroll,LinearLayout.LayoutParams(-1,0,1f))
        for(game in games) button(game.label) { selected=game;toggle(game) }
        button("Finish interrupted offload") { recover() }
        button("Use optional Eclipse installer") {
            if(GameStorage.blocker(this)==null) startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/zip").addCategory(Intent.CATEGORY_OPENABLE),3102)
        }
        button("Back to library") { onBackPressed() }
        layout.addView(label("A  Select     B  Back     D-pad  Move",13f))
        setContentView(layout)
        companion=ThorLowerCompanion(this,folder,"Manage games")
        buttons.first().requestFocus()
        fullscreen()
    }
    private fun message(text:String) { status.text=styled(text);companion?.setStatus(selected.label,text) }
    private fun amount(bytes:Long):String = if(bytes>=1L.shl(30)) String.format(java.util.Locale.US,"%.2f GB",bytes/1073741824.0)
        else String.format(java.util.Locale.US,"%.1f MB",bytes/1048576.0)
    private fun refresh() {
        if(scanning||GameStorage.busy.get())return
        scanning=true
        Thread {
            val values=games.associateWith { runCatching { SafeOffload.size(GameStorage.plan(this,it)).bytes } }
            runOnUiThread {
                scanning=false
                if(isDestroyed||isFinishing)return@runOnUiThread
                values.forEach { (g,r) -> r.getOrNull()?.let { sizes[g]=it } }
                for((i,g) in games.withIndex()) {
                    val count=sizes[g]
                    val line=when {
                        values[g]?.isFailure==true -> "Storage needs attention"
                        GameStorage.ready(this,g) -> "ON - Offload ${amount(count?:0)}"
                        (count?:0)>0 -> "OFF - ${amount(count!!)} stored; select to manage"
                        else -> "OFF - Set up game"
                    }
                    buttons[i].text=styled("${g.label}\n$line")
                }
                buttons.forEach { it.isEnabled=!GameStorage.busy.get() }
                buttons[games.size].isEnabled=SafeOffload.pending(GameStorage.work(this))&&!GameStorage.busy.get()
                GameStorage.blocker(this)?.let { message(it) }
                if(status.text.toString().startsWith("Checking stored"))message("Select a game to turn it on or off. Your own disc is needed to set it up again.")
            }
        }.apply { name="WiiDsStorageSize";isDaemon=true }.start()
    }
    private fun toggle(g:FrontendGame) {
        GameStorage.blocker(this)?.let { message(it);return }
        if(GameStorage.ready(this,g)) confirmOffload(g)
        else if((sizes[g]?:0)>0) ThorDialogs.builder(this).setTitle(g.label)
            .setMessage("${amount(sizes[g]!!)} of game or map files are stored, but the game is not ready.")
            .setPositiveButton("Set up game") { _,_->setup(g) }
            .setNeutralButton("Offload files") { _,_->confirmOffload(g) }
            .setNegativeButton("Cancel",null).show()
        else setup(g)
    }
    private fun setup(g:FrontendGame) {
        runCatching {
            GameStorage.requireIdle(this)
            if(g==FrontendGame.RETRO_REWIND) {
                if(!GameStorage.ready(this,FrontendGame.MARIO_KART)) { message("Import your Mario Kart Wii PAL disc first.");return }
                startActivity(Intent(this,ThorRetroRewindUpdatesActivity::class.java))
            } else startActivity(Intent(this,GamesActivity::class.java).putExtra("storage_game",g.id))
        }.onFailure { message(it.message?:"Setup couldn't start.") }
    }
    private fun confirmOffload(g:FrontendGame) {
        runCatching { GameStorage.requireIdle(this);GameStorage.dependency(this,g)?.let { error(it) } }
            .onFailure { message(it.message?:"Close the game first.");return }
        ThorDialogs.builder(this).setTitle("Offload ${g.label}?")
            .setMessage("Free ${amount(sizes[g]?:0)} by removing imported game files and generated maps. Saves, backups, settings and case art stay on this device.\n\nTo play again, turn it on and import your own disc or mod again.")
            .setPositiveButton("Offload") { _,_->remove(g) }.setNegativeButton("Cancel",null).show()
    }
    private fun recover() {
        runCatching { FrontendGame.fromId(SafeOffload.pendingGame(GameStorage.work(this)))?:error("No recognized offload is pending.") }
            .onSuccess { remove(it) }.onFailure { message(it.message?:"Recovery needs attention. All remaining files were kept.") }
    }
    private fun remove(g:FrontendGame) {
        buttons.forEach { it.isEnabled=false };message("Offloading ${g.label}...")
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        Thread {
            var last=0L
            val result=runCatching { GameStorage.offload(this,g) { bytes,_->
                val now=SystemClock.uptimeMillis()
                if(now-last>250) { last=now;runOnUiThread { message("Offloading ${g.label}... ${amount(bytes)} freed") } }
            } }
            runOnUiThread {
                if(isDestroyed)return@runOnUiThread
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                message(result.fold({ "${g.label} is off. Freed ${amount(it.bytes)}. Your saves and settings were kept." },
                    { "Offload stopped: ${it.message}. Reopen Manage games to finish an interrupted removal." }))
                refresh()
            }
        }.apply { name="WiiDsOffload";isDaemon=true }.start()
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

    @Deprecated("Android compatibility") override fun onActivityResult(requestCode:Int,resultCode:Int,data:Intent?) {
        super.onActivityResult(requestCode,resultCode,data)
        if(requestCode==3102&&resultCode==RESULT_OK) {
            val uri=data?.data?:return
            buttons.forEach { it.isEnabled=false };message("Checking the optional Eclipse installer...")
            Thread {
                var last=0L
                val result=runCatching { EclipseContentPack.import(this,uri) { text ->
                    val now=SystemClock.uptimeMillis();if(now-last>300) { last=now;runOnUiThread { message(text) } }
                } }
                runOnUiThread {
                    if(isDestroyed)return@runOnUiThread
                    message(result.fold({ "Eclipse installer is ready. Turn Eclipse on to install it using your Sunshine disc." },{ "Installer wasn't added: ${it.message}" }))
                    refresh()
                }
            }.apply { name="WiiDsEclipsePack";isDaemon=true }.start()
        }
    }
    private fun fullscreen() {
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
        window.setDecorFitsSystemWindows(false)
        window.insetsController?.apply { systemBarsBehavior=WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE;hide(WindowInsets.Type.systemBars()) }
    }
    override fun onResume() { super.onResume();companion?.start();fullscreen();refreshNeeded=true;handler.post(poll) }
    override fun onPause() { handler.removeCallbacks(poll);companion?.stop();super.onPause() }
    override fun onWindowFocusChanged(hasFocus:Boolean) { super.onWindowFocusChanged(hasFocus);if(hasFocus)fullscreen() }
    override fun dispatchKeyEvent(event:KeyEvent):Boolean {
        if(event.keyCode==KeyEvent.KEYCODE_BUTTON_B) { if(event.action==KeyEvent.ACTION_UP)onBackPressed();return true }
        if(event.keyCode==KeyEvent.KEYCODE_BUTTON_A) {
            if(event.action==KeyEvent.ACTION_DOWN)aPressed=true
            if(event.action==KeyEvent.ACTION_UP&&!aPressed)return true
            if(event.action==KeyEvent.ACTION_UP)aPressed=false
            val b=currentFocus as? Button?:return true
            if(b.isEnabled) { b.isPressed=event.action==KeyEvent.ACTION_DOWN;if(event.action==KeyEvent.ACTION_UP&&!event.isCanceled)b.performClick() };return true
        }
        return super.dispatchKeyEvent(event)
    }
    @Deprecated("Android compatibility") override fun onBackPressed() {
        if(GameStorage.busy.get()) Toast.makeText(this,"Wait for the offload to finish.",Toast.LENGTH_SHORT).show()
        else super.onBackPressed()
    }
}
