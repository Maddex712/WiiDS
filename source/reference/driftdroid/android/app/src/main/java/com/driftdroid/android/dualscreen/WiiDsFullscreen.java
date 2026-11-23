package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.os.Build;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/** Edge-to-edge game window, so the game surface is the panel's full 1920 x 1080, and a steady 60 Hz hint. */
public final class WiiDsFullscreen {
    private WiiDsFullscreen() {}

    /** Call from onCreate (before the surface exists) and again on focus/resume. */
    public static void apply(Activity activity) {
        Window window = activity.getWindow();
        window.setDecorFitsSystemWindows(false);
        WindowManager.LayoutParams params = window.getAttributes();
        if (params.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS) {
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(params);
        }
        WindowInsetsController bars = window.getInsetsController();
        if (bars != null) {
            bars.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            bars.hide(WindowInsets.Type.systemBars());
        }
    }

    /** The games present at 60 frames a second; ask the compositor for even 60 Hz pacing on the 120 Hz panel. */
    public static void frameRate(SurfaceHolder holder) {
        Surface surface = holder == null ? null : holder.getSurface();
        if (surface == null || !surface.isValid()) return;
        try {
            if (Build.VERSION.SDK_INT >= 31)
                surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
            else surface.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        } catch (RuntimeException unsupported) { /* the compositor picks its own rate */ }
    }
}
