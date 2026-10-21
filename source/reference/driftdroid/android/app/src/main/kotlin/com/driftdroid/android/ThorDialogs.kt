package com.driftdroid.android

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.graphics.Color
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.RectF
import android.graphics.drawable.Drawable
import android.graphics.drawable.StateListDrawable
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import android.widget.EditText
import android.widget.TextView
import java.io.File
import java.util.WeakHashMap

/** Shared launcher dialogs retain Android behavior while rendering the owner's native Wii font. */
internal object ThorDialogs {
    private val artCache = WeakHashMap<Context, ThorMenuArtwork>()
    private val styledViews = WeakHashMap<View, Boolean>()

    fun builder(activity: Activity): AlertDialog.Builder {
        if (!BuildConfig.THOR_BUILD) return AlertDialog.Builder(activity)
        return object : AlertDialog.Builder(activity) {
            override fun create(): AlertDialog {
                val dialog = super.create()
                dialog.setOnKeyListener { _, keyCode, event ->
                    if (keyCode == android.view.KeyEvent.KEYCODE_BUTTON_B) {
                        if (event.action == android.view.KeyEvent.ACTION_UP) {
                            @Suppress("DEPRECATION")
                            dialog.onBackPressed()
                        }
                        true
                    } else if (keyCode == android.view.KeyEvent.KEYCODE_BUTTON_A) {
                        val focus = dialog.currentFocus
                        val list = dialog.listView
                        if (focus is android.widget.Button) {
                            focus.isPressed = event.action == android.view.KeyEvent.ACTION_DOWN
                            if (event.action == android.view.KeyEvent.ACTION_UP && focus.isEnabled) focus.performClick()
                            true
                        } else if (list != null && (focus === list || list.hasFocus()) && list.selectedItemPosition >= 0) {
                            if (event.action == android.view.KeyEvent.ACTION_UP) {
                                list.performItemClick(list.selectedView, list.selectedItemPosition, list.selectedItemId)
                            }
                            true
                        } else false
                    } else false
                }
                dialog.window?.decorView?.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
                    private var originalAlpha = 1f
                    private var observer: ViewTreeObserver? = null
                    private val focusListener = ViewTreeObserver.OnWindowFocusChangeListener { focused ->
                        // A child dialog owns window focus. Keep this parent's
                        // layout, selections and input focus, but not fragments
                        // of its title/actions visible around the child.
                        dialog.window?.decorView?.alpha = if (focused) originalAlpha else 0f
                    }

                    override fun onViewAttachedToWindow(v: View) {
                        originalAlpha = v.alpha
                        observer = v.viewTreeObserver.also { it.addOnWindowFocusChangeListener(focusListener) }
                        v.post {
                            if (v.isAttachedToWindow && dialog.isShowing) {
                                style(activity, dialog)
                                v.alpha = if (v.hasWindowFocus()) originalAlpha else 0f
                            }
                        }
                    }

                    override fun onViewDetachedFromWindow(v: View) {
                        observer?.takeIf { it.isAlive }?.removeOnWindowFocusChangeListener(focusListener)
                        observer = null
                        v.alpha = originalAlpha
                    }
                })
                return dialog
            }
        }
    }

    private fun style(activity: Activity, dialog: AlertDialog) {
        if (android.os.Build.VERSION.SDK_INT >= 30) dialog.window?.apply {
            addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN)
            setDecorFitsSystemWindows(false)
            insetsController?.apply {
                systemBarsBehavior = android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                hide(android.view.WindowInsets.Type.systemBars())
            }
        }
        val art = runCatching {
            artCache[activity] ?: ThorMenuArtwork(File(activity.filesDir, "WiiCompiled/ThorMap")).also { artCache[activity] = it }
        }.getOrNull() ?: return
        val buttons = runCatching {
            val info = org.json.JSONObject(File(activity.filesDir, "WiiCompiled/ThorMap/menu.json").readText()).getJSONObject("images")
            art.bitmap(info.getJSONObject("button").getString("texture")) to
                art.bitmap(info.getJSONObject("button_selected").getString("texture"))
        }.getOrNull()
        val chrome = runCatching {
            val info = org.json.JSONObject(File(activity.filesDir, "WiiCompiled/ThorMap/menu.json").readText()).getJSONObject("images")
            art.bitmap(info.getJSONObject("panel").getString("texture"))
        }.getOrNull()
        chrome?.let { dialog.window?.setBackgroundDrawable(PanelArt(art, it, activity.resources.displayMetrics.density)) }
        val choiceColors = android.content.res.ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()),
            intArrayOf(Color.rgb(210, 170, 0), Color.rgb(170, 170, 170)))
        fun visit(view: View) {
            // Editable text keeps Android's input/cursor metrics and system keyboard.
            if (view is EditText && styledViews.put(view, true) != true) {
                // The platform's dark-dialog theme otherwise leaves pale text on
                // the original white Wii paper. Keep editing legible as well.
                view.setTextColor(Color.rgb(45, 45, 52))
                view.setHintTextColor(Color.rgb(104, 104, 112))
                view.backgroundTintList = choiceColors
            }
            if (view is TextView && view !is EditText && styledViews.put(view, true) != true) {
                if (view is android.widget.Button) {
                    view.isAllCaps = false
                    // Bitmap glyphs are taller than Android's default font. Let longer
                    // actions wrap instead of cutting them off in a single-line button.
                    view.setSingleLine(false)
                    view.maxLines = Int.MAX_VALUE
                    view.ellipsize = null
                    view.gravity = android.view.Gravity.CENTER
                    view.includeFontPadding = true
                    buttons?.let { (normal, focus) ->
                        view.backgroundTintList = null
                        view.background = StateListDrawable().apply {
                            addState(intArrayOf(android.R.attr.state_pressed), ButtonArt(art, focus))
                            addState(intArrayOf(android.R.attr.state_focused), ButtonArt(art, focus))
                            addState(intArrayOf(), ButtonArt(art, normal))
                        }
                        view.setPadding(28, 12, 28, 12)
                    }
                }
                view.setTextColor(Color.WHITE)
                if (view is android.widget.CompoundButton) androidx.core.widget.CompoundButtonCompat.setButtonTintList(view, choiceColors)
                if (view is android.widget.CheckedTextView) androidx.core.widget.CheckedTextViewCompat.setCheckMarkTintList(view, choiceColors)
                // Some OEM list layouts place the radio indicator in a compound drawable.
                view.compoundDrawables.filterNotNull().forEach { it.mutate().setTintList(choiceColors) }
                view.compoundDrawablesRelative.filterNotNull().forEach { it.mutate().setTintList(choiceColors) }
                var changing = false
                fun update() {
                    if (changing) return
                    changing = true
                    view.text = art.styledText(view.text)
                    changing = false
                }
                update()
                view.addTextChangedListener(object : android.text.TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                    override fun afterTextChanged(s: android.text.Editable?) { update() }
                })
            }
            if (view is ViewGroup) for (index in 0 until view.childCount) visit(view.getChildAt(index))
        }
        dialog.window?.decorView?.let { root ->
            visit(root)
            // Includes nested RecyclerView/ListView rows inflated or recycled after scrolling.
            // Adding an observer preserves each widget's own hierarchy/attachment callbacks.
            root.viewTreeObserver.addOnGlobalLayoutListener { visit(root) }
            buttons?.let { (_, focus) -> dialog.listView?.selector = ButtonArt(art, focus) }
        }
    }

    /** Keep the original end caps in proportion while the center stretches to the label. */
    private class ButtonArt(private val art: ThorMenuArtwork, private val bitmap: Bitmap) : Drawable() {
        private val target = RectF()
        private var opacity = 255
        override fun draw(canvas: Canvas) {
            target.set(bounds)
            art.button(canvas, bitmap, target, opacity)
        }
        override fun setAlpha(alpha: Int) { opacity = alpha; invalidateSelf() }
        override fun setColorFilter(filter: android.graphics.ColorFilter?) { /* Keep original disc colors. */ }
        @Deprecated("Deprecated in Android") override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
    }

    private class PanelArt(private val art: ThorMenuArtwork, private val bitmap: Bitmap, private val density: Float) : Drawable() {
        private val target = RectF()
        private var opacity = 255
        override fun draw(canvas: Canvas) {
            target.set(bounds)
            art.roundedImage(canvas, bitmap, target, minOf(12f * density, target.height() * 0.08f), opacity)
        }
        override fun setAlpha(alpha: Int) { opacity = alpha; invalidateSelf() }
        override fun setColorFilter(filter: android.graphics.ColorFilter?) { /* Keep original disc colors. */ }
        @Deprecated("Deprecated in Android") override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
    }
}
