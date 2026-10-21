package com.driftdroid.android.touch

import android.content.Context
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import kotlin.math.abs

/**
 * Shared one-finger-drag / two-finger-pinch / tap-to-toggle handling for edit mode. Both
 * TouchButtonView and TouchJoystickView delegate to this so every control edits the same way
 * regardless of which one it is, instead of duplicating the same gesture bookkeeping twice.
 *
 * [onEditEnd] fires on release with whether the touch actually moved/resized the view: a plain
 * tap (false) means "toggle this control's enabled state", a drag or pinch (true) means "just
 * save the new position/size" - see TouchControlsOverlay.
 */
class EditGestureHelper(context: Context, private val view: View, private val onEditEnd: (moved: Boolean) -> Unit) {
    private var downX = 0f
    private var downY = 0f
    private var moved = false

    private val scaleDetector =
        ScaleGestureDetector(
            context,
            object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
                override fun onScale(detector: ScaleGestureDetector): Boolean {
                    val newScale = (view.scaleX * detector.scaleFactor).coerceIn(0.5f, 2.5f)
                    view.scaleX = newScale
                    view.scaleY = newScale
                    moved = true
                    return true
                }
            },
        )

    fun onTouchEvent(event: MotionEvent) {
        scaleDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = event.rawX
                downY = event.rawY
                moved = false
            }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount == 1 && !scaleDetector.isInProgress) {
                    val dx = event.rawX - downX
                    val dy = event.rawY - downY
                    // A small dead zone so a slightly-shaky tap doesn't get mistaken for a drag.
                    if (abs(dx) > 3f || abs(dy) > 3f) {
                        view.x += dx
                        view.y += dy
                        downX = event.rawX
                        downY = event.rawY
                        moved = true
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> onEditEnd(moved)
        }
    }
}
