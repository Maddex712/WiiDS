package com.driftdroid.android.dualscreen;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.Window;
import android.view.WindowManager;
import java.util.Map;
import java.util.WeakHashMap;

/** UI-thread display votes. A connected TV owns its timing; never force a link retrain. */
public final class WiiDsDisplayPolicy {
    private WiiDsDisplayPolicy() {}
    private static DisplayManager displays;
    private static boolean external;
    // Weak keys: a process-wide hotplug listener must not retain Activities or old surfaces.
    private static final Map<Window, ModeVote> windows = new WeakHashMap<>();
    private static final Map<Surface, Float> surfaces = new WeakHashMap<>();
    private static final DisplayManager.DisplayListener listener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) { refresh(); }
        @Override public void onDisplayRemoved(int id) { refresh(); }
        @Override public void onDisplayChanged(int id) { refresh(); }
    };

    private static void init(Context context) {
        if (displays != null) return;
        Context app = context.getApplicationContext();
        displays = (app != null ? app : context).getSystemService(DisplayManager.class);
        if (displays == null) return;
        external = hasExternal();
        displays.registerDisplayListener(listener, new Handler(Looper.getMainLooper()));
    }

    private static boolean hasExternal() {
        Display primary = displays.getDisplay(Display.DEFAULT_DISPLAY);
        int unidentifiedPanels = 0;
        for (Display display : displays.getDisplays()) {
            if (!display.isValid() || display.getDisplayId() == Display.DEFAULT_DISPLAY
                    || WiiDsDisplays.isBuiltInPanel(display)) continue;
            if (WiiDsDisplays.isBuiltIn(primary, display)) unidentifiedPanels++;
            else return true;
        }
        // Pocket DS's metadata-free fallback is unambiguous only with one matching panel.
        return unidentifiedPanels > 1;
    }

    private static void refresh() {
        boolean connected = hasExternal();
        if (external == connected) return; // Our own attribute changes also send display events.
        external = connected;
        Log.i("WiiDsDisplay", connected ? "External display: releasing panel timing votes"
                : "External display removed: restoring panel timing votes");
        for (Map.Entry<Window, ModeVote> entry : windows.entrySet()) apply(entry.getKey(), entry.getValue());
        for (Map.Entry<Surface, Float> entry : surfaces.entrySet()) apply(entry.getKey(), entry.getValue());
    }

    public static void requestMode(Window window, Display.Mode mode) {
        if (window == null || mode == null) return;
        init(window.getContext());
        ModeVote vote = new ModeVote(window.getContext().getDisplay(), mode, 0);
        windows.put(window, vote);
        apply(window, vote);
    }

    public static void requestRefreshRate(Window window, float rate) {
        if (window == null) return;
        init(window.getContext());
        ModeVote vote = new ModeVote(null, null, rate);
        windows.put(window, vote);
        apply(window, vote);
    }

    public static void requestFrameRate(Context context, Surface surface, float rate) {
        if (surface == null || Build.VERSION.SDK_INT < 30) return;
        init(context);
        surfaces.put(surface, rate);
        apply(surface, rate);
    }

    private static void apply(Window window, ModeVote vote) {
        if (window == null) return;
        int modeId = external ? 0 : vote.modeId();
        float rate = external ? 0 : vote.rate;
        WindowManager.LayoutParams attrs = window.getAttributes();
        if (attrs.preferredDisplayModeId == modeId && attrs.preferredRefreshRate == rate) return;
        attrs.preferredDisplayModeId = modeId;
        attrs.preferredRefreshRate = rate;
        window.setAttributes(attrs);
    }

    private static void apply(Surface surface, float rate) {
        if (surface == null || !surface.isValid()) return;
        try {
            float requested = external ? 0 : rate;
            if (Build.VERSION.SDK_INT >= 31) {
                surface.setFrameRate(requested, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE,
                        Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
            } else {
                surface.setFrameRate(requested, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
            }
        } catch (RuntimeException unavailable) {
            Log.w("WiiDsDisplay", "Surface frame-rate vote unavailable", unavailable);
        }
    }

    private static final class ModeVote {
        final Display display;
        final Display.Mode mode;
        final float rate;
        ModeVote(Display display, Display.Mode mode, float rate) {
            this.display = display; this.mode = mode; this.rate = rate;
        }
        int modeId() {
            if (mode == null || display == null || !display.isValid()) return 0;
            // A dock can renumber mode IDs. Resolve the requested physical mode again on detach.
            for (Display.Mode supported : display.getSupportedModes()) {
                if (supported.getPhysicalWidth() == mode.getPhysicalWidth()
                        && supported.getPhysicalHeight() == mode.getPhysicalHeight()
                        && Math.abs(supported.getRefreshRate() - mode.getRefreshRate()) < 0.5f)
                    return supported.getModeId();
            }
            return 0;
        }
    }
}
