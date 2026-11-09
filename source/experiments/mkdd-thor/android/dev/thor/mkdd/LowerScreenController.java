package dev.thor.mkdd;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Choreographer;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/** Display/lifecycle wiring only. Claude supplies Renderer and owns all visual decisions. */
public final class LowerScreenController implements DisplayManager.DisplayListener, AutoCloseable {
    public interface Source { int[] read(); }
    public interface Renderer {
        View view();
        void render(LowerState state);
        /** The game is frozen under a menu: keep showing the last frame (marked as paused). */
        default void hold(boolean held) {}
        void close();
    }
    public interface Factory { Renderer create(Context lowerDisplayContext); }
    private final Activity activity;
    private final DisplayManager displays;
    private final Source source;
    private final Factory factory;
    private final LowerState.Feed feed = new LowerState.Feed();
    private Panel panel;
    private boolean resumed, closed, queued, loggedMissingNative, live, held;
    private long lastFrameNs;
    private final Choreographer.FrameCallback callback = this::frame;

    public LowerScreenController(Activity activity, Source source, Factory factory) {
        mainThread();
        this.activity=activity; this.source=source; this.factory=factory;
        displays=(DisplayManager)activity.getSystemService(Context.DISPLAY_SERVICE);
    }
    public void resume() {
        mainThread();
        if (closed || resumed) return;
        resumed=true;
        displays.registerDisplayListener(this,new Handler(Looper.getMainLooper()));
        reconcile();
    }
    public void pause() {
        mainThread();
        if (!resumed) return;
        resumed=false; displays.unregisterDisplayListener(this);
        removePanel();
    }
    @Override public void close() { mainThread(); pause(); closed=true; }
    /** While held the game is frozen: no polling, so the frozen frame doesn't expire to "unavailable". */
    public void hold(boolean held) {
        mainThread();
        if (this.held == held) return;
        this.held=held;
        if (!held) feed.clear();
        if (panel != null) panel.renderer.hold(held);
    }
    /** Future upper-HUD suppression must require this AND verified field coverage. */
    public boolean isVisibleWithLiveData() { mainThread(); return live; }
    @Override public void onDisplayAdded(int id) { reconcile(); }
    @Override public void onDisplayChanged(int id) { reconcile(); }
    @Override public void onDisplayRemoved(int id) { reconcile(); }

    private void reconcile() {
        mainThread();
        if (!resumed || closed) return;
        Display upper=activity.getDisplay(), lower=null;
        if (upper == null) { removePanel(); return; }
        long upperArea=area(upper), best=Long.MAX_VALUE;
        for (Display d : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            // Dynamic IDs, matching the existing Thor smaller-presentation policy.
            if (!d.isValid() || d.getDisplayId()==upper.getDisplayId() ||
                    (d.getFlags() & Display.FLAG_PRIVATE) != 0 || area(d)>=upperArea) continue;
            if (area(d)<best) { lower=d; best=area(d); }
        }
        if (panel != null && lower != null && panel.getDisplay().getDisplayId()==lower.getDisplayId()
                && panel.isShowing()) return;
        removePanel();
        if (lower == null) return;
        try {
            panel=new Panel(activity,lower,factory); panel.show(); panel.renderer.hold(held);
            if (held) poll();  // the game is frozen, so its state is still current
            schedule();
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w("WiiDS-MKDD","Lower display disappeared",e); removePanel();
        }
    }
    private static long area(Display d) {
        Display.Mode m=d.getMode(); return (long)m.getPhysicalWidth()*m.getPhysicalHeight();
    }
    private void schedule() {
        if (resumed && panel != null && panel.isShowing() && !queued) {
            queued=true; Choreographer.getInstance().postFrameCallback(callback);
        }
    }
    private void frame(long frameNs) {
        queued=false;
        if (!resumed || panel == null || !panel.isShowing()) return;
        // Cap polling at 60 Hz on devices whose Activity display runs at 120 Hz.
        if (!held && (lastFrameNs==0 || frameNs-lastFrameNs>=16_000_000L)) {
            lastFrameNs=frameNs;
            poll();
        }
        schedule();
    }
    private void poll() {
        int[] packet=null;
        try { packet=source.read(); }
        catch (UnsatisfiedLinkError e) {
            if (!loggedMissingNative) { Log.w("WiiDS-MKDD","Native lower state unavailable",e); loggedMissingNative=true; }
        }
        LowerState state=feed.accept(packet,SystemClock.elapsedRealtimeNanos());
        live=state.available;
        panel.renderer.render(state);
    }
    private void removePanel() {
        if (queued) Choreographer.getInstance().removeFrameCallback(callback);
        queued=false; live=false; lastFrameNs=0; feed.clear();
        Panel old=panel; panel=null;
        if (old != null) {
            try { old.dismiss(); } finally { old.renderer.close(); }
        }
    }
    private static void mainThread() {
        if (Looper.myLooper()!=Looper.getMainLooper()) throw new IllegalStateException("Use the Activity thread");
    }
    // Static, not inner: build-tools 34 d8 crashes on javac 21's MethodParameters for inner ctors.
    private static final class Panel extends Presentation {
        final Renderer renderer;
        Panel(Activity activity, Display display, Factory factory) {
            super(activity,display);
            renderer=factory.create(getContext());
            requestWindowFeature(Window.FEATURE_NO_TITLE);
            Window window=getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            }
            setContentView(renderer.view());
        }
    }
}
