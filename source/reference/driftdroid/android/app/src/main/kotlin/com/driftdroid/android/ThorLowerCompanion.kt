package com.driftdroid.android

import android.app.Activity
import android.app.Presentation
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Display
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import java.io.File

/** Passive lower-screen information before a native runtime or race map is available. */
class ThorLowerCompanion(private val activity: Activity, private val folder: File) : DisplayManager.DisplayListener {
    private val displays = activity.getSystemService(DisplayManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private var presentation: StatusPresentation? = null
    private var running = false
    private var title = "Ready to race"
    private var message = "Choose a game on the upper screen."
    private var librarySelection: ThorLibraryCompanionView.Selection? = null

    fun setStatus(title: String, message: String) {
        this.title = title
        this.message = message
        librarySelection = null
        presentation?.showStatus(title, message)
    }

    /** Opt-in read-only library details; other owners keep the existing status presentation. */
    internal fun setLibrarySelection(product: Int?, options: Boolean, available: Boolean,
        description: String = "", status: String = "",
        phase: ThorLauncherView.Phase = ThorLauncherView.Phase.BROWSE, message: String = "") {
        val value = ThorLibraryCompanionView.Selection(product, options, available, description, status, phase, message)
        librarySelection = value
        presentation?.showLibrary(value)
    }

    fun start() {
        if (running || displays == null) return
        running = true
        displays.registerDisplayListener(this, handler)
        refresh()
    }

    fun stop() {
        if (!running) return
        running = false
        displays?.unregisterDisplayListener(this)
        closePresentation()
    }

    private fun closePresentation() {
        val previous = presentation
        presentation = null
        previous?.status?.close()
        previous?.library?.close()
        previous?.dismiss()
    }

    private fun refresh() {
        if (!running) return
        val upper = activity.windowManager.defaultDisplay.displayId
        var lower: Display? = null
        for (display in displays?.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION).orEmpty()) {
            if (!display.isValid || display.displayId == upper) continue
            if (lower == null || display.name == "Screen-2") lower = display
        }
        val target = lower
        if (target != null && presentation?.isShowing == true &&
            presentation?.display?.displayId == target.displayId) return
        closePresentation()
        if (target == null) return
        val next = StatusPresentation(target)
        presentation = next
        next.setOnDismissListener {
            next.status.close()
            next.library?.close()
            if (presentation === next) presentation = null
        }
        try {
            next.show()
        } catch (failure: WindowManager.InvalidDisplayException) {
            Log.w("ThorLowerCompanion", "Lower display unavailable", failure)
            closePresentation()
        } catch (failure: WindowManager.BadTokenException) {
            Log.w("ThorLowerCompanion", "Activity no longer owns a display window", failure)
            closePresentation()
        } catch (failure: SecurityException) {
            Log.w("ThorLowerCompanion", "Lower display permission unavailable", failure)
            closePresentation()
        }
    }

    override fun onDisplayAdded(displayId: Int) = refresh()
    override fun onDisplayRemoved(displayId: Int) = refresh()
    override fun onDisplayChanged(displayId: Int) = refresh()

    private inner class StatusPresentation(display: Display) : Presentation(activity, display) {
        val status = ThorLowerStatusView(context, folder)
        var library: ThorLibraryCompanionView? = null
            private set
        private lateinit var content: FrameLayout

        fun showStatus(title: String, message: String) {
            library?.visibility = View.GONE
            status.visibility = View.VISIBLE
            status.setStatus(title, message)
        }

        fun showLibrary(selection: ThorLibraryCompanionView.Selection) {
            if (!::content.isInitialized) return
            val view = library ?: ThorLibraryCompanionView(context, folder).also {
                library = it
                content.addView(it, FrameLayout.LayoutParams(-1, -1))
            }
            status.visibility = View.GONE
            view.visibility = View.VISIBLE
            view.setSelection(selection)
        }

        override fun onCreate(savedInstanceState: Bundle?) {
            super.onCreate(savedInstanceState)
            status.setStatus(title, message)
            content = FrameLayout(context)
            content.addView(status, FrameLayout.LayoutParams(-1, -1))
            val selected = librarySelection
            if (selected != null) showLibrary(selected)
            setContentView(content)
            window?.apply {
                addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON or
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE)
                decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT)
            }
        }
    }
}
