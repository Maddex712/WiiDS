package com.driftdroid.android.touch

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Color
import android.net.Uri
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import org.json.JSONObject

/** One on-screen control's identity and default placement (fractions of the play area, 0..1).
 * color/pill give each button its own look by default (matching a real Wii Classic Controller
 * Pro's color-coded face buttons and stretched L/R shoulder buttons - requested directly after
 * comparing against KartPad's Android port, which does the same, instead of every button being an
 * identical plain gray circle). */
private data class ControlSpec(
    val id: String,
    val label: String,
    val sdlButton: Int, // TouchInputBridge.BUTTON_*, or -1 when isJoystick is used instead
    val defaultXFraction: Float,
    val defaultYFraction: Float,
    val defaultSizeDp: Float,
    val defaultEnabled: Boolean,
    val isJoystick: Boolean = false,
    val color: Int = TouchButtonView.DEFAULT_FILL_COLOR,
    val pill: Boolean = false,
    val textColor: Int = Color.WHITE,
    val outlineColor: Int = TouchButtonView.DEFAULT_OUTLINE_COLOR,
)

/**
 * Owns the whole on-screen touch control layout: creates every button/joystick, persists where
 * the player has dragged/resized/hidden each one (SharedPreferences - this is purely a per-device
 * UI preference, not game state, so it deliberately does not go through RuntimeConfigFile/
 * Config.toml), and drives layout editing (drag/resize/hide, named layout save/load, file
 * export/import).
 *
 * Layout mirrors a Nintendo Switch Pro Controller, since that's the default mapping
 * AutoConfigureTouchControllerIfPresent (settings_overlay.cpp) applies to this virtual pad:
 * A accelerates, B reverses, L=item, R=drift, Start ("+") pauses, Select ("-") is GC Z, and the
 * D-Pad is enabled by default since bikes need it (Up) to wheelie.
 */
class TouchControlsOverlay private constructor(private val activity: Activity, parent: ViewGroup) {

    private val prefs: SharedPreferences = activity.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    private val library: SharedPreferences = activity.getSharedPreferences(LIBRARY_PREFS_NAME, Context.MODE_PRIVATE)
    private val container = FrameLayout(activity)
    private val views = LinkedHashMap<String, View>()
    private var editMode = false
    private val toolbar = LinearLayout(activity)
    private var pendingExportJson: String? = null

    // Effective visibility is userVisible AND NOT controllerConnected AND NOT settingsOpen: a real
    // Bluetooth controller auto-hides the touch overlay (see
    // MainActivity.onNativeGamepadConnectionChanged, driven by aurora-main/lib/window.cpp's
    // SDL_EVENT_GAMEPAD_ADDED/REMOVED), the settings sidebar hides it while open (see
    // MainActivity.onNativeSettingsVisibilityChanged - the sidebar docks to the same screen edge
    // these buttons do, and gameplay input is already blocked while it's open anyway), and
    // userVisible is the persisted "Touch controls" checkbox in the in-game settings menu
    // (MainActivity.onNativeSetTouchOverlayVisible). Independent of the per-button drag/hide state
    // stored below - this only ever hides/shows the WHOLE overlay at once.
    private var userVisible = true
    private var controllerConnected = false
    private var settingsOpen = false

    /** Lets MainActivity keep the settings gear's dimmed/highlighted state in sync when edit mode
     * exits via the toolbar's "Done" button rather than however it was entered. */
    var onEditModeChanged: ((Boolean) -> Unit)? = null

    init {
        userVisible = prefs.getBoolean(KEY_MASTER_VISIBLE, true)

        parent.addView(
            container,
            ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT),
        )
        applyMasterVisibility()

        // Fixes a real, reproducible bug: controls restored slightly ABOVE their saved position
        // on every launch. Root cause - SDL asynchronously switches this window to fullscreen
        // (SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN, once the emulator core sets up video output) some
        // time AFTER onCreate/attach() runs, growing `container` to include what was status-bar
        // space. The layout editor is normally used during actual gameplay (i.e. already
        // fullscreen), so saved x/y fractions are relative to the LARGER, post-fullscreen size -
        // but the one-shot `container.post {}` measurement below that restores them on next
        // launch fires before that switch happens, against the SMALLER, pre-fullscreen size,
        // and nothing ever corrected for the container growing out from under it afterward.
        // Re-applying every control's saved fraction whenever the container's real size actually
        // changes (not just once at startup) means the final resting position always matches
        // whatever size the container ends up at, regardless of when that resize lands.
        container.addOnLayoutChangeListener { _, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom ->
            val widthChanged = (right - left) != (oldRight - oldLeft)
            val heightChanged = (bottom - top) != (oldBottom - oldTop)
            if (widthChanged || heightChanged) {
                reapplyPositions()
            }
        }

        buildToolbar()

        // Build the controls after the container has a real size - fractions need it to convert
        // to pixel positions, and onCreate() hasn't laid anything out yet at this point.
        container.post {
            for (spec in SPECS) {
                addControl(spec)
            }
            val toolbarParams =
                FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            toolbarParams.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
            toolbarParams.topMargin = dp(70f)
            container.addView(toolbar, toolbarParams)
        }
    }

    private fun dp(value: Float): Int =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, container.resources.displayMetrics).toInt()

    private fun toolbarButton(text: String, onClick: () -> Unit): Button {
        val button = Button(activity)
        button.text = text
        button.textSize = 12f
        button.setOnClickListener { onClick() }
        return button
    }

    /** "Done editing" first and most prominent - the previous design (long-press the gear again
     * to exit) was confirmed confusing on-device ("i clicked on the gear it sent me to edit
     * layout, but i couldn't exit out of it"). Entering edit mode is now the gear's 5-second-hold
     * gesture (MainActivity.addSettingsButton) - exiting is always this explicit button. */
    private fun buildToolbar() {
        toolbar.orientation = LinearLayout.HORIZONTAL
        toolbar.visibility = View.GONE
        toolbar.setBackgroundColor(Color.argb(180, 30, 30, 30))
        val pad = dp(6f)
        toolbar.setPadding(pad, pad, pad, pad)

        val doneButton = toolbarButton("Done") { setEditMode(false) }
        doneButton.setBackgroundColor(Color.rgb(60, 130, 70))
        toolbar.addView(doneButton)
        toolbar.addView(toolbarButton("Reset layout") { resetLayout() })
        toolbar.addView(toolbarButton("Save layout") { promptSaveLayout() })
        toolbar.addView(toolbarButton("Layouts...") { showLayoutLibraryDialog() })
    }

    private fun addControl(spec: ControlSpec) {
        val sizePx = dp(spec.defaultSizeDp)
        // Pills get a wider-than-tall base size (not just a rounded square) so they actually read
        // as stretched shoulder buttons - scaleX/scaleY below stay uniform, so this aspect ratio
        // is preserved through resizing.
        val widthPx = if (spec.pill) (sizePx * 1.8f).toInt() else sizePx
        val heightPx = sizePx
        val view: View =
            if (spec.isJoystick) {
                TouchJoystickView(container.context).apply {
                    baseColor = prefs.getInt(key(spec.id, "fillColor"), TouchJoystickView.DEFAULT_BASE_COLOR)
                    outlineColor = prefs.getInt(key(spec.id, "outlineColor"), TouchJoystickView.DEFAULT_OUTLINE_COLOR)
                    knobColor = prefs.getInt(key(spec.id, "knobColor"), TouchJoystickView.DEFAULT_KNOB_COLOR)
                }
            } else {
                val fillColor = prefs.getInt(key(spec.id, "fillColor"), spec.color)
                val outlineColor = prefs.getInt(key(spec.id, "outlineColor"), spec.outlineColor)
                TouchButtonView(container.context, spec.label, spec.sdlButton, fillColor, spec.pill, Color.WHITE, outlineColor).apply {
                    // Only the A (accelerate) button ever supports this - GitHub issue #4's whole
                    // point was freeing up the thumb that's holding A down, not any other button.
                    if (spec.id == "a") {
                        doubleTapAutoHoldEnabled = prefs.getBoolean(KEY_DOUBLE_TAP_AUTO_HOLD, false)
                    }
                }
            }

        val params = FrameLayout.LayoutParams(widthPx, heightPx)
        container.addView(view, params)

        val containerWidth = container.width.takeIf { it > 0 } ?: container.resources.displayMetrics.widthPixels
        val containerHeight = container.height.takeIf { it > 0 } ?: container.resources.displayMetrics.heightPixels

        val xFraction = prefs.getFloat(key(spec.id, "x"), spec.defaultXFraction)
        val yFraction = prefs.getFloat(key(spec.id, "y"), spec.defaultYFraction)
        val scale = prefs.getFloat(key(spec.id, "scale"), 1f)
        val enabled = prefs.getBoolean(key(spec.id, "enabled"), spec.defaultEnabled)
        val opacity = prefs.getFloat(key(spec.id, "opacity"), 1f)

        view.x = xFraction * containerWidth - widthPx / 2f
        view.y = yFraction * containerHeight - heightPx / 2f
        view.scaleX = scale
        view.scaleY = scale
        view.visibility = if (enabled) View.VISIBLE else View.GONE
        view.alpha = if (enabled) opacity else 0.35f

        val onEditEnd: (Boolean) -> Unit = { moved ->
            if (moved) {
                // Deliberately NOT the containerWidth/containerHeight captured above - those are
                // a snapshot from whenever this control was first created (possibly before the
                // window's later fullscreen switch), and saving against a stale size is exactly
                // what caused the restore-position bug this replaced. Editing happens live, so
                // the container's actual current size is always the right denominator here.
                saveTransform(spec.id, view)
            } else {
                // A plain tap (no drag/pinch) while editing opens the per-control editor instead
                // of instantly toggling visibility - requested directly: "tapping on a button
                // lets you resize it or hide it with a GUI option."
                showControlEditor(spec, view)
            }
        }

        when (view) {
            is TouchButtonView -> {
                view.editMode = editMode
                view.onEditEnd = onEditEnd
            }
            is TouchJoystickView -> {
                view.editMode = editMode
                view.onEditEnd = onEditEnd
            }
        }

        views[spec.id] = view
    }

    private fun showControlEditor(spec: ControlSpec, view: View) {
        val activityContext = activity
        val root = LinearLayout(activityContext)
        root.orientation = LinearLayout.VERTICAL
        val pad = dp(16f)
        root.setPadding(pad, pad, pad, pad)

        val visibleCheckbox = CheckBox(activityContext)
        visibleCheckbox.text = "Visible"
        visibleCheckbox.isChecked = view.visibility == View.VISIBLE
        root.addView(visibleCheckbox)

        // Color/outline customization - white/translucent by default for every control, this is
        // purely an opt-in choice (requested directly: "the touchpad controls should just be
        // white by default... there should be customizability for custom colors, outlines etc"),
        // and covers the joystick too ("make it so the joystick can be colored").
        when (view) {
            is TouchButtonView -> {
                addColorPickerRow(root, "Color", view.fillColor) { view.fillColor = it }
                addColorPickerRow(root, "Outline", view.outlineColor) { view.outlineColor = it }
            }
            is TouchJoystickView -> {
                addColorPickerRow(root, "Base Color", view.baseColor) { view.baseColor = it }
                addColorPickerRow(root, "Outline", view.outlineColor) { view.outlineColor = it }
                addColorPickerRow(root, "Knob Color", view.knobColor) { view.knobColor = it }
            }
        }

        val opacityLabel = TextView(activityContext)
        opacityLabel.text = "Opacity"
        opacityLabel.setPadding(0, dp(12f), 0, 0)
        root.addView(opacityLabel)

        val startingOpacity = prefs.getFloat(key(spec.id, "opacity"), 1f)
        val opacitySeekBar = SeekBar(activityContext)
        opacitySeekBar.max = 100
        opacitySeekBar.progress = (startingOpacity * 100).toInt().coerceIn(10, 100)
        opacitySeekBar.setOnSeekBarChangeListener(
            object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
                    // A floor of 10% keeps a control from becoming a truly invisible-but-still-
                    // there dead zone on screen - Visible unchecked is the actual "hide it" action.
                    if (fromUser && visibleCheckbox.isChecked) {
                        view.alpha = progress.coerceAtLeast(10) / 100f
                    }
                }

                override fun onStartTrackingTouch(seek: SeekBar) {}

                override fun onStopTrackingTouch(seek: SeekBar) {}
            },
        )
        root.addView(opacitySeekBar)

        val sizeLabel = TextView(activityContext)
        sizeLabel.text = "Size"
        sizeLabel.setPadding(0, dp(12f), 0, 0)
        root.addView(sizeLabel)

        val sizeRow = LinearLayout(activityContext)
        sizeRow.orientation = LinearLayout.HORIZONTAL
        sizeRow.gravity = Gravity.CENTER_VERTICAL

        // Scale range mirrors EditGestureHelper's pinch-to-resize clamp (0.5x-2.5x) so typing a
        // value and pinching can never disagree about what's achievable.
        val seekBar = SeekBar(activityContext)
        seekBar.max = 200
        val currentScale = view.scaleX
        seekBar.progress = (((currentScale - 0.5f) / 2.0f) * 200).toInt().coerceIn(0, 200)
        val seekParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
        sizeRow.addView(seekBar, seekParams)

        val sizeInput = EditText(activityContext)
        sizeInput.inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
        sizeInput.setText(String.format("%.2f", currentScale))
        val inputParams = LinearLayout.LayoutParams(dp(80f), ViewGroup.LayoutParams.WRAP_CONTENT)
        inputParams.marginStart = dp(8f)
        sizeInput.layoutParams = inputParams

        var syncingFromSeek = false
        var syncingFromText = false
        seekBar.setOnSeekBarChangeListener(
            object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seek: SeekBar, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    val scale = 0.5f + (progress / 200f) * 2.0f
                    view.scaleX = scale
                    view.scaleY = scale
                    if (!syncingFromText) {
                        syncingFromSeek = true
                        sizeInput.setText(String.format("%.2f", scale))
                        syncingFromSeek = false
                    }
                }

                override fun onStartTrackingTouch(seek: SeekBar) {}

                override fun onStopTrackingTouch(seek: SeekBar) {}
            },
        )
        sizeInput.setOnFocusChangeListener { _, hasFocus ->
            if (hasFocus || syncingFromSeek) return@setOnFocusChangeListener
            val typed = sizeInput.text.toString().toFloatOrNull()?.coerceIn(0.5f, 2.5f) ?: return@setOnFocusChangeListener
            view.scaleX = typed
            view.scaleY = typed
            syncingFromText = true
            seekBar.progress = (((typed - 0.5f) / 2.0f) * 200).toInt().coerceIn(0, 200)
            syncingFromText = false
        }

        sizeRow.addView(sizeInput)
        root.addView(sizeRow)

        val scroll = android.widget.ScrollView(activityContext)
        scroll.addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        // Persists on ANY dismiss path (Close button, tapping outside the dialog, or the back
        // button), not just an explicit "Close" tap - confirmed directly as a real loss: colors
        // (and everything else here) apply live to the view immediately, so dismissing the dialog
        // by tapping outside it - a completely natural thing to do - looked like it worked but
        // silently discarded the change instead of saving it, since only the "Close" button used
        // to persist anything. OnDismissListener fires for every one of those paths uniformly.
        fun persist() {
            val enabled = visibleCheckbox.isChecked
            val opacity = opacitySeekBar.progress.coerceAtLeast(10) / 100f
            view.visibility = if (enabled) View.VISIBLE else View.GONE
            view.alpha = if (enabled) opacity else 0.35f
            val editor =
                prefs
                    .edit()
                    .putBoolean(key(spec.id, "enabled"), enabled)
                    .putFloat(key(spec.id, "scale"), view.scaleX)
                    .putFloat(key(spec.id, "opacity"), opacity)
            when (view) {
                is TouchButtonView -> {
                    editor.putInt(key(spec.id, "fillColor"), view.fillColor)
                    editor.putInt(key(spec.id, "outlineColor"), view.outlineColor)
                }
                is TouchJoystickView -> {
                    editor.putInt(key(spec.id, "fillColor"), view.baseColor)
                    editor.putInt(key(spec.id, "outlineColor"), view.outlineColor)
                    editor.putInt(key(spec.id, "knobColor"), view.knobColor)
                }
                else -> {}
            }
            editor.apply()
            saveTransform(spec.id, view)
            // Edit mode keeps disabled controls visible-but-dimmed so they stay reachable -
            // matches the pre-dialog behavior.
            if (!enabled) {
                view.visibility = View.VISIBLE
            }
        }

        AlertDialog.Builder(activityContext)
            .setTitle(spec.label.ifEmpty { "Joystick" })
            .setView(scroll)
            .setPositiveButton("Close", null)
            .setOnDismissListener { persist() }
            .show()
    }

    /** A label plus a horizontal row of tappable color swatches (including a "reset to white/
     * translucent default" swatch first) - [onPick] fires immediately on tap for a live preview,
     * same as the opacity/size controls above it. */
    /** A standard SV-square + hue-strip + hex-field color picker (see SaturationValuePickerView's
     * doc comment) rather than a fixed swatch list or plain sliders - "having a limited selection
     * kinda sucks" led to sliders, then a reference screenshot asked for this specific layout.
     * [onPick] fires live on every drag/edit for an immediate preview, same as the opacity/size
     * controls elsewhere in this dialog. */
    private fun addColorPickerRow(root: LinearLayout, label: String, initialColor: Int, onPick: (Int) -> Unit) {
        val activityContext = activity
        val rowLabel = TextView(activityContext)
        rowLabel.text = label
        rowLabel.setPadding(0, dp(12f), 0, dp(4f))
        root.addView(rowLabel)

        val hsv = FloatArray(3)
        Color.colorToHSV(initialColor, hsv)
        var alpha = Color.alpha(initialColor)

        val preview = View(activityContext)
        val svPicker = SaturationValuePickerView(activityContext)
        val hueSlider = HueSliderView(activityContext)
        val hexField = EditText(activityContext)

        fun currentColor(): Int = Color.HSVToColor(alpha, hsv)

        var syncingHexField = false
        fun refreshPreview() {
            val color = currentColor()
            preview.setBackgroundColor(color)
            syncingHexField = true
            hexField.setText(String.format("#%06X", 0xFFFFFF and color))
            syncingHexField = false
            onPick(color)
        }

        val pickerRow = LinearLayout(activityContext)
        pickerRow.orientation = LinearLayout.HORIZONTAL
        val previewParams = LinearLayout.LayoutParams(dp(56f), dp(140f))
        pickerRow.addView(preview, previewParams)
        val svParams = LinearLayout.LayoutParams(0, dp(140f), 1f)
        svParams.marginStart = dp(8f)
        pickerRow.addView(svPicker, svParams)
        root.addView(pickerRow)

        val hueParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36f))
        hueParams.topMargin = dp(8f)
        root.addView(hueSlider, hueParams)

        val hexLabel = TextView(activityContext)
        hexLabel.text = "HEX"
        hexLabel.textSize = 12f
        hexLabel.setPadding(0, dp(10f), 0, dp(2f))
        root.addView(hexLabel)
        hexField.inputType = InputType.TYPE_CLASS_TEXT
        hexField.filters = arrayOf(android.text.InputFilter.LengthFilter(7))
        root.addView(hexField)

        svPicker.hue = hsv[0]
        svPicker.setSaturationValue(hsv[1], hsv[2])
        hueSlider.setHue(hsv[0])
        refreshPreview()

        svPicker.onColorChanged = { saturation, value ->
            hsv[1] = saturation
            hsv[2] = value
            refreshPreview()
        }
        hueSlider.onHueChanged = { hue ->
            hsv[0] = hue
            svPicker.hue = hue
            refreshPreview()
        }
        hexField.setOnFocusChangeListener { _, hasFocus ->
            if (hasFocus || syncingHexField) return@setOnFocusChangeListener
            val typed = hexField.text.toString().removePrefix("#")
            val parsed = typed.toLongOrNull(16)?.takeIf { typed.length == 6 } ?: return@setOnFocusChangeListener
            val color = Color.rgb((parsed shr 16 and 0xFF).toInt(), (parsed shr 8 and 0xFF).toInt(), (parsed and 0xFF).toInt())
            Color.colorToHSV(color, hsv)
            svPicker.hue = hsv[0]
            svPicker.setSaturationValue(hsv[1], hsv[2])
            hueSlider.setHue(hsv[0])
            refreshPreview()
        }
    }

    private fun saveTransform(id: String, view: View) {
        val containerWidth = container.width.takeIf { it > 0 } ?: container.resources.displayMetrics.widthPixels
        val containerHeight = container.height.takeIf { it > 0 } ?: container.resources.displayMetrics.heightPixels
        val centerX = view.x + view.width / 2f
        val centerY = view.y + view.height / 2f
        prefs
            .edit()
            .putFloat(key(id, "x"), centerX / containerWidth)
            .putFloat(key(id, "y"), centerY / containerHeight)
            .putFloat(key(id, "scale"), view.scaleX)
            .apply()
    }

    /** Re-applies every control's saved (or, if never saved, default) x/y fraction against the
     * container's CURRENT size - called whenever that size actually changes (see the
     * OnLayoutChangeListener in attach()), not just once at startup, so a later fullscreen resize
     * can't leave positions permanently off, including for a first-time install that hasn't saved
     * anything yet. Does not touch scale/enabled/opacity/colors, only position. */
    private fun reapplyPositions() {
        val containerWidth = container.width.takeIf { it > 0 } ?: return
        val containerHeight = container.height.takeIf { it > 0 } ?: return
        val specsById = SPECS.associateBy { it.id }
        for ((id, view) in views) {
            val spec = specsById[id] ?: continue
            val xFraction = prefs.getFloat(key(id, "x"), spec.defaultXFraction)
            val yFraction = prefs.getFloat(key(id, "y"), spec.defaultYFraction)
            view.x = xFraction * containerWidth - view.width / 2f
            view.y = yFraction * containerHeight - view.height / 2f
        }
    }

    fun isUserVisible(): Boolean = userVisible

    /** From the in-game settings menu's "Touch controls" checkbox (settings_overlay.cpp, via
     * MainActivity.onNativeSetTouchOverlayVisible). */
    fun setUserVisible(visible: Boolean) {
        userVisible = visible
        prefs.edit().putBoolean(KEY_MASTER_VISIBLE, visible).apply()
        applyMasterVisibility()
    }

    fun isDoubleTapAutoHoldEnabled(): Boolean = prefs.getBoolean(KEY_DOUBLE_TAP_AUTO_HOLD, false)

    /** From the in-game settings menu's "Double-tap A to auto-hold acceleration" checkbox
     * (settings_overlay.cpp, via MainActivity.onNativeSetDoubleTapAutoHold). Turning it off while
     * already locked also releases the hold immediately, rather than leaving A stuck down with no
     * way to release it until the player re-enables the feature just to double-tap it off. */
    fun setDoubleTapAutoHoldEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_DOUBLE_TAP_AUTO_HOLD, enabled).apply()
        (views["a"] as? TouchButtonView)?.doubleTapAutoHoldEnabled = enabled
    }

    /** From MainActivity.onNativeGamepadConnectionChanged - a real controller connecting/
     * disconnecting, not the touch overlay's own virtual gamepad (filtered out native-side). */
    fun setControllerConnected(connected: Boolean) {
        controllerConnected = connected
        applyMasterVisibility()
    }

    /** From MainActivity.onNativeSettingsVisibilityChanged - the native settings sidebar just
     * opened/closed. */
    fun setSettingsOpen(open: Boolean) {
        settingsOpen = open
        applyMasterVisibility()
    }

    private fun applyMasterVisibility() {
        container.visibility =
            if (userVisible && !controllerConnected && !settingsOpen) View.VISIBLE else View.GONE
    }

    /** From MotionSteering - hides just the on-screen steering stick while gravity-based steering
     * is driving the same virtual axis (TouchInputBridge.AXIS_LEFT_X), so the two don't fight over
     * it. Restores whatever hidden/visible + opacity state the player had saved for it. */
    fun setMotionSteeringActive(active: Boolean) {
        val view = views[JOYSTICK_CONTROL_ID] ?: return
        if (active) {
            view.visibility = View.GONE
            return
        }
        val enabled = prefs.getBoolean(key(JOYSTICK_CONTROL_ID, "enabled"), true)
        val opacity = prefs.getFloat(key(JOYSTICK_CONTROL_ID, "opacity"), 1f)
        view.visibility = if (enabled) View.VISIBLE else View.GONE
        view.alpha = if (enabled) opacity else 0.35f
    }

    /** Enter/exit layout editing (drag to move, pinch to resize, tap to open the hide/resize
     * dialog). Entered by holding the settings gear for 5 seconds (MainActivity), exited via the
     * toolbar's "Done" button - not a toggle callable from two different places, since that's
     * exactly what made exiting confusing before ("i couldn't exit out of it"). */
    fun setEditMode(enabled: Boolean) {
        if (editMode == enabled) return
        editMode = enabled
        for ((id, view) in views) {
            when (view) {
                is TouchButtonView -> view.editMode = editMode
                is TouchJoystickView -> view.editMode = editMode
            }
            // While editing, show every control (dimmed if disabled) so hidden ones can be
            // brought back; leaving edit mode re-hides whatever is still disabled. Checked against
            // the real persisted enabled flag, not alpha - opacity can legitimately be anywhere
            // from 10%-100% on an ENABLED control now, so alpha alone can no longer tell disabled
            // apart from "enabled but dialed down".
            val isEnabled = prefs.getBoolean(key(id, "enabled"), true)
            view.visibility = if (editMode || isEnabled) View.VISIBLE else View.GONE
        }
        toolbar.visibility = if (editMode) View.VISIBLE else View.GONE
        onEditModeChanged?.invoke(editMode)
    }

    fun isEditMode(): Boolean = editMode

    private fun resetLayout() {
        prefs.edit().clear().apply()
        rebuildControls()
    }

    private fun rebuildControls() {
        val wasEditing = editMode
        container.removeAllViews()
        views.clear()
        editMode = false
        for (spec in SPECS) {
            addControl(spec)
        }
        val toolbarParams =
            FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        toolbarParams.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        toolbarParams.topMargin = dp(70f)
        container.addView(toolbar, toolbarParams)
        if (wasEditing) {
            setEditMode(true)
        }
    }

    private fun key(id: String, field: String) = "${id}_$field"

    // --- Named layout library (save/apply/delete) + file export/import ---------------------

    private fun currentLayoutJson(): JSONObject {
        val root = JSONObject()
        for (spec in SPECS) {
            val obj = JSONObject()
            obj.put("x", prefs.getFloat(key(spec.id, "x"), spec.defaultXFraction).toDouble())
            obj.put("y", prefs.getFloat(key(spec.id, "y"), spec.defaultYFraction).toDouble())
            obj.put("scale", prefs.getFloat(key(spec.id, "scale"), 1f).toDouble())
            obj.put("enabled", prefs.getBoolean(key(spec.id, "enabled"), spec.defaultEnabled))
            obj.put("opacity", prefs.getFloat(key(spec.id, "opacity"), 1f).toDouble())
            // Colors were saved straight to the shared per-device prefs, not into named layouts -
            // meaning switching layouts never changed them, so every saved layout looked like it
            // used "the same" (whatever was currently live) colors. Confirmed directly ("touchpad
            // colors arent being saved to their layouts, the colors apply to every layout saved").
            val defaultFill = if (spec.isJoystick) TouchJoystickView.DEFAULT_BASE_COLOR else spec.color
            val defaultOutline = if (spec.isJoystick) TouchJoystickView.DEFAULT_OUTLINE_COLOR else spec.outlineColor
            obj.put("fillColor", prefs.getInt(key(spec.id, "fillColor"), defaultFill))
            obj.put("outlineColor", prefs.getInt(key(spec.id, "outlineColor"), defaultOutline))
            if (spec.isJoystick) {
                obj.put("knobColor", prefs.getInt(key(spec.id, "knobColor"), TouchJoystickView.DEFAULT_KNOB_COLOR))
            }
            root.put(spec.id, obj)
        }
        return root
    }

    private fun applyLayoutJson(json: JSONObject) {
        val editor = prefs.edit()
        for (spec in SPECS) {
            val obj = json.optJSONObject(spec.id) ?: continue
            editor.putFloat(key(spec.id, "x"), obj.optDouble("x", spec.defaultXFraction.toDouble()).toFloat())
            editor.putFloat(key(spec.id, "y"), obj.optDouble("y", spec.defaultYFraction.toDouble()).toFloat())
            editor.putFloat(key(spec.id, "scale"), obj.optDouble("scale", 1.0).toFloat())
            editor.putBoolean(key(spec.id, "enabled"), obj.optBoolean("enabled", spec.defaultEnabled))
            editor.putFloat(key(spec.id, "opacity"), obj.optDouble("opacity", 1.0).toFloat())
            val defaultFill = if (spec.isJoystick) TouchJoystickView.DEFAULT_BASE_COLOR else spec.color
            val defaultOutline = if (spec.isJoystick) TouchJoystickView.DEFAULT_OUTLINE_COLOR else spec.outlineColor
            editor.putInt(key(spec.id, "fillColor"), obj.optInt("fillColor", defaultFill))
            editor.putInt(key(spec.id, "outlineColor"), obj.optInt("outlineColor", defaultOutline))
            if (spec.isJoystick) {
                editor.putInt(key(spec.id, "knobColor"), obj.optInt("knobColor", TouchJoystickView.DEFAULT_KNOB_COLOR))
            }
        }
        editor.apply()
        rebuildControls()
    }

    private fun libraryNames(): MutableSet<String> = LinkedHashSet(library.getStringSet(KEY_LIBRARY_NAMES, emptySet()) ?: emptySet())

    private fun promptSaveLayout() {
        val input = EditText(activity)
        input.hint = "Layout name"
        AlertDialog.Builder(activity)
            .setTitle("Save layout")
            .setView(input)
            .setPositiveButton("Save") { _, _ ->
                val name = input.text.toString().trim()
                if (name.isNotEmpty()) {
                    saveNamedLayout(name, currentLayoutJson())
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun saveNamedLayout(name: String, json: JSONObject) {
        val names = libraryNames()
        names.add(name)
        library
            .edit()
            .putString(libraryKey(name), json.toString())
            .putStringSet(KEY_LIBRARY_NAMES, names)
            .apply()
    }

    private fun deleteNamedLayout(name: String) {
        val names = libraryNames()
        names.remove(name)
        library.edit().remove(libraryKey(name)).putStringSet(KEY_LIBRARY_NAMES, names).apply()
    }

    private fun libraryKey(name: String) = "layout_$name"

    private fun showLayoutLibraryDialog() {
        val names = libraryNames().toList()
        val root = LinearLayout(activity)
        root.orientation = LinearLayout.VERTICAL
        val pad = dp(8f)
        root.setPadding(pad, pad, pad, pad)

        if (names.isEmpty()) {
            val empty = TextView(activity)
            empty.text = "No saved layouts yet."
            empty.setPadding(pad, pad, pad, pad)
            root.addView(empty)
        }
        for (name in names) {
            val row = LinearLayout(activity)
            row.orientation = LinearLayout.HORIZONTAL
            row.gravity = Gravity.CENTER_VERTICAL

            val nameButton = Button(activity)
            nameButton.text = name
            val nameParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            row.addView(nameButton, nameParams)

            // Replaces this saved layout with whatever is currently live, rather than only being
            // able to save a brand new named layout - requested directly ("there should also be
            // an overwrite option to replace one of the saved ones with the current one you have").
            val overwriteButton = toolbarButton("Overwrite") {
                saveNamedLayout(name, currentLayoutJson())
                showLayoutLibraryDialog()
            }
            row.addView(overwriteButton)

            val exportButton = toolbarButton("Export") {
                val json = library.getString(libraryKey(name), null) ?: return@toolbarButton
                launchExportLayout(json)
            }
            row.addView(exportButton)

            val deleteButton = toolbarButton("Delete") {
                deleteNamedLayout(name)
                showLayoutLibraryDialog()
            }
            row.addView(deleteButton)

            root.addView(row)
        }

        val scroll = android.widget.ScrollView(activity)
        scroll.addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        val dialog =
            AlertDialog.Builder(activity)
                .setTitle("Layouts")
                .setView(scroll)
                .setPositiveButton("Import from file...") { _, _ -> launchImportLayout() }
                .setNegativeButton("Close", null)
                .create()

        // Tapping a name applies it and closes - wired after dialog creation so the click
        // listener can dismiss it (Builder's own buttons handle their own dismissal already).
        for (i in 0 until root.childCount) {
            val row = root.getChildAt(i) as? LinearLayout ?: continue
            val nameButton = row.getChildAt(0) as? Button ?: continue
            val name = nameButton.text.toString()
            nameButton.setOnClickListener {
                val json = library.getString(libraryKey(name), null)
                if (json != null) {
                    applyLayoutJson(JSONObject(json))
                }
                dialog.dismiss()
            }
        }
        dialog.show()
    }

    private fun launchExportLayout(json: String) {
        pendingExportJson = json
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "application/json"
        intent.putExtra(Intent.EXTRA_TITLE, "driftdroid_touch_layout.json")
        activity.startActivityForResult(intent, REQUEST_CODE_EXPORT_LAYOUT)
    }

    private fun launchImportLayout() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        activity.startActivityForResult(intent, REQUEST_CODE_IMPORT_LAYOUT)
    }

    /** Call from the activity's onActivityResult. Returns true if this was one of this overlay's
     * own request codes. */
    fun onActivityResult(requestCode: Int, uri: Uri?): Boolean {
        when (requestCode) {
            REQUEST_CODE_EXPORT_LAYOUT -> {
                val json = pendingExportJson
                pendingExportJson = null
                if (uri != null && json != null) {
                    activity.contentResolver.openOutputStream(uri)?.use { it.write(json.toByteArray()) }
                }
                return true
            }
            REQUEST_CODE_IMPORT_LAYOUT -> {
                if (uri != null) {
                    val text = activity.contentResolver.openInputStream(uri)?.use { it.readBytes() }?.toString(Charsets.UTF_8)
                    if (text != null) {
                        try {
                            val json = JSONObject(text)
                            promptImportedLayoutName(json)
                        } catch (_: Exception) {
                            // Not a valid layout file - silently ignored, same as a cancelled picker.
                        }
                    }
                }
                return true
            }
            else -> return false
        }
    }

    private fun promptImportedLayoutName(json: JSONObject) {
        val input = EditText(activity)
        input.hint = "Layout name"
        input.setText("Imported layout")
        AlertDialog.Builder(activity)
            .setTitle("Import layout")
            .setView(input)
            .setPositiveButton("Import") { _, _ ->
                val name = input.text.toString().trim().ifEmpty { "Imported layout" }
                saveNamedLayout(name, json)
                applyLayoutJson(json)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    companion object {
        private const val PREFS_NAME = "touch_controls"
        private const val LIBRARY_PREFS_NAME = "touch_controls_library"
        private const val KEY_MASTER_VISIBLE = "master_visible"
        private const val KEY_DOUBLE_TAP_AUTO_HOLD = "double_tap_auto_hold"
        private const val JOYSTICK_CONTROL_ID = "joystick"
        private const val KEY_LIBRARY_NAMES = "layout_names"

        const val REQUEST_CODE_EXPORT_LAYOUT = 0x544C4558 // "TLEX"
        const val REQUEST_CODE_IMPORT_LAYOUT = 0x544C494D // "TLIM"

        private val SPECS =
            listOf(
                // Smaller than the first draft and moved down so it no longer overlaps L or the
                // D-Pad above it - the drag-anywhere zone is this view's own bounds, so the visual
                // size here doubles as the steering hit area.
                // Position and colors are the user's own fully hand-tuned layout, pulled live from
                // an installed device and adopted as the shipped default for every install
                // ("ok now make the default layout the one i have, with the colors and stuff").
                ControlSpec("joystick", "", -1, 0.12939323f, 0.68577796f, 170f, true, isJoystick = true),
                // Enabled by default: bikes need Up on the D-Pad to wheelie, which the analog
                // stick alone cannot do.
                ControlSpec("dpad_up", "▲", TouchInputBridge.BUTTON_DPAD_UP, 0.12407264f, 0.23613377f, 46f, true, color = 1862270975, outlineColor = -939524096),
                ControlSpec("dpad_down", "▼", TouchInputBridge.BUTTON_DPAD_DOWN, 0.12449999f, 0.3633333f, 46f, true, color = 1862270975, outlineColor = -939524096),
                ControlSpec("dpad_left", "◀", TouchInputBridge.BUTTON_DPAD_LEFT, 0.06703846f, 0.30883333f, 46f, true, color = 1862270975, outlineColor = -939524096),
                ControlSpec("dpad_right", "▶", TouchInputBridge.BUTTON_DPAD_RIGHT, 0.18196154f, 0.30883333f, 46f, true, color = 1862270975, outlineColor = -939524096),
                // Switch Pro Controller diamond: X top, Y left, A right (the big button), B bottom.
                // Custom colors are a per-button customization the player can further override via
                // the control editor (tap a button in edit mode). Only the L/R/Start shape is
                // structurally different by default: a stretched "pill" instead of a circle,
                // matching a real shoulder button's proportions.
                ControlSpec("x", "X", TouchInputBridge.BUTTON_X, 0.85796094f, 0.47683534f, 66f, true, color = 1862257920, outlineColor = -931108096),
                ControlSpec("y", "Y", TouchInputBridge.BUTTON_Y, 0.774085f, 0.64033693f, 66f, true, color = 1845559067, outlineColor = -937015552),
                ControlSpec("a", "A", TouchInputBridge.BUTTON_A, 0.93149424f, 0.623813f, 66f, true, color = 1862205440, outlineColor = -939524096),
                ControlSpec("b", "B", TouchInputBridge.BUTTON_B, 0.8581538f, 0.8199371f, 74f, true, color = 1846542591, outlineColor = -939524010),
                // L=item, R=drift (digital shoulder buttons, per the Classic Controller Pro
                // preset AutoConfigureTouchControllerIfPresent applies - see TouchInputBridge).
                ControlSpec("l", "L", TouchInputBridge.BUTTON_L, 0.085525885f, 0.08287472f, 68f, true, pill = true, color = 1862270975, outlineColor = -922746881),
                // Lower than L: the top-right corner is shared with the FPS counter - confirmed
                // on-device, R's hit-circle up there was swallowing taps. The settings gear no
                // longer lives up here at all (moved to top-center, see MainActivity).
                ControlSpec("r", "R", TouchInputBridge.BUTTON_R, 0.86071074f, 0.2600174f, 68f, true, pill = true, color = 1862270975, outlineColor = -922746881),
                // No Select control: Mario Kart Wii has no Select-equivalent action, so a touch
                // button for it would just do nothing.
                ControlSpec("start", "Start", TouchInputBridge.BUTTON_START, 0.47884616f, 0.83566666f, 60f, true, pill = true, color = 1862270975, outlineColor = -922746881),
            )

        fun attach(activity: Activity, parent: ViewGroup): TouchControlsOverlay = TouchControlsOverlay(activity, parent)
    }
}
