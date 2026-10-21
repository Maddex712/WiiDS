package com.driftdroid.android.touch

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ComposeShader
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.Shader
import android.view.MotionEvent
import android.view.View

/**
 * The classic 2D saturation (x-axis: white -> pure hue) / value (y-axis: bright -> black) square
 * from a standard color picker, with a draggable thumb - requested directly, with a screenshot,
 * after the slider-only version felt too fiddly ("having a limited selection kinda sucks" led to
 * sliders; this is the next step up from that). [hue] is driven externally by [HueSliderView].
 */
class SaturationValuePickerView(context: Context) : View(context) {
    var hue: Float = 0f
        set(value) {
            field = value
            shader = null
            invalidate()
        }
    var saturation: Float = 1f
        private set
    var value: Float = 1f
        private set

    var onColorChanged: ((saturation: Float, value: Float) -> Unit)? = null

    private var shader: Shader? = null
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
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

    fun setSaturationValue(newSaturation: Float, newValue: Float) {
        saturation = newSaturation.coerceIn(0f, 1f)
        value = newValue.coerceIn(0f, 1f)
        invalidate()
    }

    private fun ensureShader(width: Int, height: Int) {
        if (shader != null || width <= 0 || height <= 0) return
        val hueColor = Color.HSVToColor(floatArrayOf(hue, 1f, 1f))
        val saturationGradient =
            LinearGradient(
                0f, 0f, width.toFloat(), 0f,
                Color.WHITE, hueColor, Shader.TileMode.CLAMP,
            )
        val valueGradient =
            LinearGradient(
                0f, 0f, 0f, height.toFloat(),
                Color.WHITE, Color.BLACK, Shader.TileMode.CLAMP,
            )
        shader = ComposeShader(saturationGradient, valueGradient, PorterDuff.Mode.MULTIPLY)
        fillPaint.shader = shader
    }

    override fun onDraw(canvas: Canvas) {
        ensureShader(width, height)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), fillPaint)

        val thumbX = saturation * width
        val thumbY = (1f - value) * height
        canvas.drawCircle(thumbX, thumbY, 16f, thumbShadowPaint)
        canvas.drawCircle(thumbX, thumbY, 16f, thumbRingPaint)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        shader = null
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                saturation = (event.x / width).coerceIn(0f, 1f)
                value = (1f - event.y / height).coerceIn(0f, 1f)
                invalidate()
                onColorChanged?.invoke(saturation, value)
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
        }
        return true
    }
}
