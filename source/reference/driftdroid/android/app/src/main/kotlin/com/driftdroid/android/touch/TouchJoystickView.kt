package com.driftdroid.android.touch

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

/**
 * Drag-anywhere virtual joystick: touching anywhere within this view drops the knob at that
 * point and steering follows the finger from there, rather than requiring a precise tap on a
 * fixed-position base graphic. Feeds SDL_GAMEPAD_AXIS_LEFTX/LEFTY on the same virtual gamepad the
 * on-screen buttons use (runtime/src/android_touch_controls.cpp), so steering works with
 * whatever the player has the left stick bound to, exactly like a real analog stick would.
 *
 * White/translucent by default, same as always - baseColor/outlineColor/knobColor are a per-
 * control customization opted into via the control editor, matching TouchButtonView's colors
 * (requested directly: "make it so the joystick can be colored").
 */
class TouchJoystickView(context: Context) : View(context) {

    // See TouchButtonView's identical setter for why this must repaint immediately rather than
    // waiting for the next unrelated redraw.
    var editMode: Boolean = false
        set(value) {
            field = value
            invalidate()
        }
    var onEditEnd: ((moved: Boolean) -> Unit)? = null

    private var active = false
    private var originX = 0f
    private var originY = 0f
    private var knobX = 0f
    private var knobY = 0f

    private val basePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val baseBorderPaint =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 4f
        }
    private val knobPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    var baseColor: Int = DEFAULT_BASE_COLOR
        set(value) {
            field = value
            basePaint.color = value
            invalidate()
        }
    var outlineColor: Int = DEFAULT_OUTLINE_COLOR
        set(value) {
            field = value
            baseBorderPaint.color = value
            invalidate()
        }
    var knobColor: Int = DEFAULT_KNOB_COLOR
        set(value) {
            field = value
            knobPaint.color = value
            invalidate()
        }

    init {
        basePaint.color = baseColor
        baseBorderPaint.color = outlineColor
        knobPaint.color = knobColor
    }

    private val editGesture = EditGestureHelper(context, this) { moved -> onEditEnd?.invoke(moved) }

    override fun onDraw(canvas: Canvas) {
        val cx = if (active) originX else width / 2f
        val cy = if (active) originY else height / 2f
        val baseRadius = min(width, height) / 2f - 8f
        canvas.drawCircle(cx, cy, baseRadius, basePaint)
        // Always the real, user-chosen outline color - see TouchButtonView's identical fix for
        // why this no longer forces yellow in edit mode.
        canvas.drawCircle(cx, cy, baseRadius, baseBorderPaint)
        val knobRenderX = if (active) knobX else cx
        val knobRenderY = if (active) knobY else cy
        canvas.drawCircle(knobRenderX, knobRenderY, baseRadius * 0.45f, knobPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (editMode) {
            editGesture.onTouchEvent(event)
            return true
        }

        val maxRadius = min(width, height) / 2f - 8f
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                active = true
                originX = event.x
                originY = event.y
                knobX = event.x
                knobY = event.y
                setAxes(0f, 0f)
                invalidate()
            }
            MotionEvent.ACTION_MOVE -> {
                if (active) {
                    val dx = event.x - originX
                    val dy = event.y - originY
                    val distance = min(hypot(dx, dy), maxRadius)
                    val angle = atan2(dy, dx)
                    knobX = originX + cos(angle) * distance
                    knobY = originY + sin(angle) * distance
                    setAxes((knobX - originX) / maxRadius, (knobY - originY) / maxRadius)
                    invalidate()
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                active = false
                setAxes(0f, 0f)
                invalidate()
            }
        }
        return true
    }

    private fun setAxes(axisX: Float, axisY: Float) {
        TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_X, axisX)
        TouchInputBridge.nativeSetTouchAxis(TouchInputBridge.AXIS_LEFT_Y, axisY)
    }

    companion object {
        // The user's own hand-tuned joystick colors, adopted as the shipped default for everyone
        // ("ok now make the default layout the one i have, with the colors and stuff").
        const val DEFAULT_BASE_COLOR = 1191182335
        const val DEFAULT_OUTLINE_COLOR = -1610612736
        const val DEFAULT_KNOB_COLOR = -922746881
    }
}
