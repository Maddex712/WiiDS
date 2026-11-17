package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityOptions;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.AtomicFile;
import android.view.Display;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONObject;

/** Shared screen roles. Read on demand: each game runs in a separate process. */
public final class WiiDsScreens {
    private WiiDsScreens() {}

    static Display current(Activity activity) {
        try { return activity.getDisplay(); }
        catch (UnsupportedOperationException missingDisplayContext) { return activity.getWindowManager().getDefaultDisplay(); }
    }

    private static AtomicFile file(Context context) {
        // Game ContextWrappers redirect filesDir; always use the package's application directory.
        Context app = context.getApplicationContext();
        return new AtomicFile(new File((app != null ? app : context).getFilesDir(), "wiids-screen-roles.json"));
    }

    public static List<Display> available(Context context) {
        DisplayManager manager = context.getSystemService(DisplayManager.class);
        List<Display> result = new ArrayList<>();
        if (manager == null) return result;
        for (Display display : manager.getDisplays()) {
            if (!display.isValid() || (display.getFlags() & Display.FLAG_PRIVATE) != 0) continue;
            result.add(display);
        }
        result.sort((a,b) -> Integer.compare(a.getDisplayId(), b.getDisplayId()));
        return result;
    }

    // Public Android APIs do not expose a durable display connector ID. Use a conservative
    // descriptor; identical attachments are ambiguous and require setup again, never ID reuse.
    static String key(Display display) {
        if (display.getDisplayId() == Display.DEFAULT_DISPLAY) return "primary";
        int width = 0, height = 0;
        for (Display.Mode mode : display.getSupportedModes()) {
            int x = Math.max(mode.getPhysicalWidth(), mode.getPhysicalHeight());
            int y = Math.min(mode.getPhysicalWidth(), mode.getPhysicalHeight());
            if ((long)x * y > (long)width * height) { width = x; height = y; }
        }
        return display.getName() + "|" + width + "x" + height;
    }

    static Display resolve(List<Display> displays, String key) {
        Display result = null;
        for (Display display : displays) if (key(display).equals(key)) {
            if (result != null) return null;
            result = display;
        }
        return result;
    }

    static JSONObject read(Context context) {
        try {
            JSONObject roles = new JSONObject(new String(file(context).readFully(), StandardCharsets.UTF_8));
            if (roles.getInt("version") != 1 || roles.getString("top").isEmpty()
                    || roles.getString("top").equals(roles.getString("bottom"))) return null;
            return roles;
        } catch (Exception missing) { return null; }
    }

    public static void save(Context context, Display top, Display bottom) throws Exception {
        List<Display> displays = available(context);
        if (top == null || !top.isValid() || resolve(displays, key(top)) == null
                || resolve(displays, key(top)).getDisplayId() != top.getDisplayId())
            throw new IllegalArgumentException("The top screen changed. Choose it again.");
        if (bottom != null && (!bottom.isValid() || bottom.getDisplayId() == top.getDisplayId()
                || resolve(displays, key(bottom)) == null
                || resolve(displays, key(bottom)).getDisplayId() != bottom.getDisplayId()))
            throw new IllegalArgumentException("The bottom screen changed. Choose it again.");
        if (bottom != null && !canHostBottom(context, bottom))
            throw new IllegalArgumentException("Android does not allow companion information on that screen.");
        JSONObject roles = new JSONObject().put("version", 1).put("top", key(top))
                .put("bottom", bottom == null ? "" : key(bottom));
        AtomicFile target = file(context);
        FileOutputStream stream = null;
        try {
            stream = target.startWrite();
            stream.write(roles.toString().getBytes(StandardCharsets.UTF_8));
            target.finishWrite(stream);
        } catch (Exception failure) { if (stream != null) target.failWrite(stream); throw failure; }
    }

    public static Display top(Context context) {
        List<Display> displays = available(context);
        JSONObject roles = read(context);
        Display selected = roles == null ? null : resolve(displays, roles.optString("top"));
        if (selected != null) return selected;
        return resolve(displays, "primary");
    }

    public static boolean needsSetup(Activity activity) {
        List<Display> displays = available(activity);
        if (displays.size() < 2) return false;
        JSONObject roles = read(activity);
        if (roles != null) {
            // A detached configured attachment is a normal single-screen fallback. An unknown
            // replacement or ambiguous pair needs a new choice before any game is started.
            return resolve(displays, roles.optString("top")) == null
                    || (!roles.optString("bottom").isEmpty() && resolve(displays, roles.optString("bottom")) == null);
        }
        // Preserve recognized built-in automatic configuration, but never guess an add-on/TV.
        return WiiDsDisplays.builtInCompanion(activity, activity.getSystemService(DisplayManager.class)) == null;
    }

    public static Display companion(Activity activity, DisplayManager manager) {
        Display current = current(activity);
        if (current == null) return null;
        JSONObject roles = read(activity);
        if (roles == null) return WiiDsDisplays.builtInCompanion(activity, manager);
        List<Display> displays = available(activity);
        Display selectedTop = top(activity);
        Display bottom = resolve(displays, roles.optString("bottom"));
        if (selectedTop == null || selectedTop.getDisplayId() != current.getDisplayId()
                || bottom == null || bottom.getDisplayId() == current.getDisplayId()) return null;
        return bottom;
    }

    public static boolean canLaunch(Context activity, Display display, Intent intent) {
        ActivityManager manager = activity.getSystemService(ActivityManager.class);
        try { return manager != null && display != null && display.isValid()
                && (display.getDisplayId() == Display.DEFAULT_DISPLAY
                || activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_ACTIVITIES_ON_SECONDARY_DISPLAYS))
                && manager.isActivityStartAllowedOnDisplay(activity, display.getDisplayId(), intent);
        } catch (IllegalArgumentException | SecurityException unavailable) { return false; }
    }

    public static boolean canHostBottom(Context context, Display display) {
        if ((display.getFlags() & Display.FLAG_PRESENTATION) != 0) return true;
        return canLaunch(context, display, new Intent(context, CompanionHostActivity.class));
    }

    /** Only internal explicit launches receive our screen choice; external pickers keep theirs. */
    public static Bundle launchOptions(Activity activity, Intent intent, Bundle supplied) {
        if (intent == null || intent.getComponent() == null
                || !activity.getPackageName().equals(intent.getComponent().getPackageName())) return supplied;
        if (intent.getComponent().getClassName().startsWith(CompanionHostActivity.class.getName())) return supplied;
        Display target = top(activity);
        if (!canLaunch(activity, target, intent)) return supplied;
        Bundle options = supplied == null ? new Bundle() : new Bundle(supplied);
        options.putAll(ActivityOptions.makeBasic().setLaunchDisplayId(target.getDisplayId()).toBundle());
        return options;
    }
}
