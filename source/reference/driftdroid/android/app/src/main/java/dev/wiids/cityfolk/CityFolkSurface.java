package dev.wiids.cityfolk;

import android.content.Context;
import android.view.SurfaceHolder;

import dev.wiids.xd.sdl.SDLSurface;

final class CityFolkSurface extends SDLSurface {
    private static native void setRendererReady(boolean ready);

    CityFolkSurface(Context context) { super(context); }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        setRendererReady(false);
        super.surfaceCreated(holder);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        setRendererReady(false);
        super.surfaceChanged(holder, format, width, height);
        com.driftdroid.android.dualscreen.WiiDsFullscreen.frameRate(holder);
    }

    @Override protected void onSurfaceReady() { setRendererReady(true); }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        setRendererReady(false);
        super.surfaceDestroyed(holder);
    }
}
