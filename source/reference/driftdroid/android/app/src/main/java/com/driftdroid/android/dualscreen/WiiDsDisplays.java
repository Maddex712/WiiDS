package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.hardware.display.DeviceProductInfo;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.view.Display;

/** Finds a built-in companion panel Android allows this activity to present on. */
public final class WiiDsDisplays {
    private WiiDsDisplays() {}

    public static Display companion(Activity activity, DisplayManager displays) {
        if (displays == null) return null;
        Display current = activity.getDisplay();
        if (current == null) return null;

        Display candidate = null;
        int available = 0;
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (!display.isValid() || display.getDisplayId() == current.getDisplayId()
                    || (display.getFlags() & Display.FLAG_PRIVATE) != 0) continue;
            if ("Screen-2".equals(display.getName())) return display;
            if (!isBuiltIn(current, display)) continue;
            candidate = display;
            available++;
        }
        return available == 1 ? candidate : null;
    }

    private static boolean isBuiltIn(Display current, Display display) {
        if (Build.VERSION.SDK_INT >= 31) {
            DeviceProductInfo info = display.getDeviceProductInfo();
            if (info != null && info.getConnectionToSinkType() != DeviceProductInfo.CONNECTION_TO_SINK_UNKNOWN) {
                return info.getConnectionToSinkType() == DeviceProductInfo.CONNECTION_TO_SINK_BUILT_IN;
            }
        }
        // Stock Pocket DS firmware may omit panel product info. Match its two physical panel modes.
        if (!"AYANEO_PocketDS".equalsIgnoreCase(Build.PRODUCT)) return false;
        return (hasSize(current, 1920, 1080) && hasSize(display, 1024, 768))
                || (hasSize(current, 1024, 768) && hasSize(display, 1920, 1080));
    }

    private static boolean hasSize(Display display, int width, int height) {
        Display.Mode mode = display.getMode();
        int x = mode.getPhysicalWidth();
        int y = mode.getPhysicalHeight();
        return Math.max(x, y) == width && Math.min(x, y) == height;
    }
}
