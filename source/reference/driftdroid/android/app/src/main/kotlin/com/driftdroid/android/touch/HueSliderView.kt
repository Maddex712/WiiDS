package com.driftdroid.android.touch

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.view.MotionEvent
import android.view.View

/** Full-spectrum horizontal hue bar with a draggable circular thumb, matching a standard color
 * picker's hue strip (see SaturationValuePickerView's doc comment for the reference screenshot
 * this and it were built from). */
class HueSliderView(context: Context) : View(context) {
    var hue: Float = 0f
        private set

    var onHueChanged: ((Float) -> Unit)? = null

    private var shader: Shader? = null
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val thumbRingPaint =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 5f
            color = Color.WHITE
        }
    private val thumbShadowPaint =
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 7f
            color = Color.argb(120, 0, 0, 0)
        }

    fun setHue(newHue: Float) {
        hue = newHue.coerceIn(0f, 360f)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        if (shader == null && width > 0) {
            val colors = IntArray(361) { Color.HSVToColor(floatArrayOf(it.toFloat(), 1f, 1f)) }
            shader = LinearGradient(0f, 0f, width.toFloat(), 0f, colors, null, Shader.TileMode.CLAMP)
            trackPaint.shader = shader
        }
        val trackHeight = height * 0.6f
        val top = (height - trackHeight) / 2f
        canvas.drawRoundRect(RectF(0f, top, width.toFloat(), top + trackHeight), trackHeight / 2f, trackHeight / 2f, trackPaint)

        val thumbX = (hue / 360f) * width
        val thumbY = height / 2f
        canvas.drawCircle(thumbX, thumbY, trackHeight / 2f + 6f, thumbShadowPaint)
        canvas.drawCircle(thumbX, thumbY, trackHeight / 2f + 6f, thumbRingPaint)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        shader = null
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                hue = ((event.x / width) * 360f).coerceIn(0f, 360f)
                invalidate()
                onHueChanged?.invoke(hue)
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
        }
        return true
    }
}
