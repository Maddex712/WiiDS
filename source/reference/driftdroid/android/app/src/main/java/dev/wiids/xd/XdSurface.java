package dev.wiids.xd;
import android.content.Context;
import android.view.SurfaceHolder;
import dev.wiids.xd.sdl.SDLSurface;
/** Tell Aurora before SDL destroys the native window, including background/hotplug. */
public final class XdSurface extends SDLSurface {
    private static native void nativeSetReady(boolean ready);
    public XdSurface(Context context) { super(context); }
    @Override public void surfaceCreated(SurfaceHolder holder) {
        nativeSetReady(false); super.surfaceCreated(holder);
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSetReady(false); super.surfaceDestroyed(holder);
    }
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height) {
        nativeSetReady(false); super.surfaceChanged(holder,format,width,height);
        nativeSetReady(mIsSurfaceReady);
    }
}
