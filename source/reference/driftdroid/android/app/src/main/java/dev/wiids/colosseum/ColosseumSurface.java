package dev.wiids.colosseum;

import android.content.Context;
import android.view.SurfaceHolder;
import dev.wiids.xd.sdl.SDLSurface;

/** Mirrors SDL's drawable state into the Colosseum renderer. */
final class ColosseumSurface extends SDLSurface {
    private static native void nativeSetReady(boolean ready);

    ColosseumSurface(Context context) { super(context); }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        nativeSetReady(false);
        super.surfaceCreated(holder);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeSetReady(false);
        super.surfaceChanged(holder, format, width, height);
        com.driftdroid.android.dualscreen.WiiDsFullscreen.frameRate(holder);
    }

    @Override protected void onSurfaceReady() {
        nativeSetReady(true);
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSetReady(false);
        super.surfaceDestroyed(holder);
    }
}
