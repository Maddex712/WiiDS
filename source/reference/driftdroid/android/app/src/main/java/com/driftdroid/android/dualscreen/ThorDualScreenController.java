package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Choreographer;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;

/** One guest runtime, an independent read-only race map on the lower display. */
public final class ThorDualScreenController implements DisplayManager.DisplayListener {
    private final Activity activity;
    private final DisplayManager displays;
    private final boolean retroRewind;
    private final String expectedCodePulSha;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private MapPresentation presentation;
    private boolean running;
    private int epoch;

    public ThorDualScreenController(Activity activity) {
        this(activity, false, "");
    }
    public ThorDualScreenController(Activity activity, boolean retroRewind, String expectedCodePulSha) {
        this.activity = activity;
        this.retroRewind = retroRewind;
        this.expectedCodePulSha = expectedCodePulSha;
        displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }
    public void start() {
        if (running) return;
        running = true;
        displays.registerDisplayListener(this, handler);
        refresh();
    }
    public void stop() {
        if (!running) return;
        running = false;
        displays.unregisterDisplayListener(this);
        close();
    }
    private void close() {
        epoch++;
        MapPresentation old = presentation;
        presentation = null;
        if (old != null) { old.map.close(); old.dismiss(); }
    }
    private void refresh() {
        if (!running) return;
        int current = activity.getWindowManager().getDefaultDisplay().getDisplayId();
        Display target = null;
        for (Display d : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (!d.isValid() || d.getDisplayId() == current) continue;
            if (target == null || "Screen-2".equals(d.getName())) target = d;
        }
        if (target != null && presentation != null && presentation.isShowing()
                && presentation.getDisplay().getDisplayId() == target.getDisplayId()) return;
        close();
        if (target == null) return;
        MapPresentation next = new MapPresentation(target);
        presentation = next;
        next.setOnDismissListener(dialog -> {
            if (presentation == next) { epoch++; presentation = null; next.map.close(); }
        });
        try { next.show(); next.startFrames(epoch); }
        catch (WindowManager.InvalidDisplayException | SecurityException e) {
            Log.w("ThorDualScreen", "Lower display unavailable", e); close();
        }
    }
    @Override public void onDisplayAdded(int id) { refresh(); }
    @Override public void onDisplayRemoved(int id) { refresh(); }
    @Override public void onDisplayChanged(int id) { refresh(); }

    private final class MapPresentation extends Presentation {
        private final ThorRaceMapView map;
        private Choreographer.FrameCallback frameCallback;
        MapPresentation(Display display) {
            super(activity, display);
            map = new ThorRaceMapView(getContext(), new File(activity.getFilesDir(), "WiiCompiled/ThorMap"),
                    retroRewind, expectedCodePulSha);
        }
        @Override protected void onCreate(Bundle state) {
            super.onCreate(state);
            Window window = getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
            setContentView(map);
            if (window != null) {
                window.setLayout(-1, -1);
                Display.Mode current = getDisplay().getMode();
                for (Display.Mode mode : getDisplay().getSupportedModes()) {
                    if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                            && mode.getPhysicalHeight() == current.getPhysicalHeight()
                            && Math.abs(mode.getRefreshRate() - 60) < 1) {
                        WindowManager.LayoutParams attrs = window.getAttributes();
                        attrs.preferredDisplayModeId = mode.getModeId(); window.setAttributes(attrs); break;
                    }
                }
            }
        }
        private void startFrames(int token) {
            frameCallback = time -> {
                if (!running || token != epoch || presentation != this || !isShowing()) return;
                map.update(time);
                Choreographer.getInstance().postFrameCallback(frameCallback);
            };
            Choreographer.getInstance().postFrameCallback(frameCallback);
        }
        @Override public void dismiss() {
            if (frameCallback != null) {
                Choreographer.getInstance().removeFrameCallback(frameCallback);
                frameCallback = null;
            }
            super.dismiss();
        }
    }
}
