package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.view.Display;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import com.driftdroid.android.LibraryActivity;
import com.driftdroid.android.SaveManagerProfiles;
import java.util.List;

/** Screen setup runs before games; moving a library never restarts a native engine. */
public final class ScreenSetupActivity extends Activity implements DisplayManager.DisplayListener {
    private static final String RETURN = "wiids_screen_return";
    private static final String TARGET = "wiids_screen_target";
    private DisplayManager manager;
    private LinearLayout content;

    public static void open(Activity activity) {
        if (!idle(activity)) {
            Toast.makeText(activity, "Close the running game before changing screens.", Toast.LENGTH_LONG).show();
            return;
        }
        activity.startActivity(new Intent(activity, ScreenSetupActivity.class)
                .putExtra(RETURN, new Intent(activity.getIntent())));
        activity.finish();
    }

    private static boolean idle(Activity activity) {
        try {
            if (SaveManagerProfiles.INSTANCE.launchBlocker(activity) != null) return false;
            SaveManagerProfiles.INSTANCE.requireStopped(activity); return true;
        }
        catch (Exception busy) { return false; }
    }

    /** Called before the library's import/art workers or deferred frontend launch. */
    public static boolean enterLibrary(Activity activity) {
        int attempted = activity.getIntent().getIntExtra(TARGET, -1);
        activity.getIntent().removeExtra(TARGET);
        if (attempted >= 0) {
            if (WiiDsScreens.current(activity) == null || WiiDsScreens.current(activity).getDisplayId() != attempted)
                Toast.makeText(activity, "Android kept WiiDS on this screen. Check your device's multi-display settings.", Toast.LENGTH_LONG).show();
            return false; // OEMs can ignore launchDisplayId; never create a relaunch loop.
        }
        if (!idle(activity)) return false;
        if (WiiDsScreens.needsSetup(activity)) {
            activity.startActivity(new Intent(activity, ScreenSetupActivity.class)
                    .putExtra(RETURN, new Intent(activity.getIntent())));
            activity.finish();
            return true;
        }
        Display target = WiiDsScreens.top(activity);
        Display current = WiiDsScreens.current(activity);
        if (target == null || current == null || target.getDisplayId() == current.getDisplayId()) return false;
        return moveLibrary(activity, new Intent(activity.getIntent()), target);
    }

    private static boolean moveLibrary(Activity source, Intent request, Display target) {
        request.setClass(source, LibraryActivity.class);
        // A distinct task prevents Android from simply returning the old instance on the wrong
        // display. Finish only this setup/library instance, after Android accepts the launch.
        request.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
        request.putExtra(TARGET, target.getDisplayId());
        if (!WiiDsScreens.canLaunch(source, target, request)) {
            Toast.makeText(source, "Android does not allow WiiDS to open on that screen.", Toast.LENGTH_LONG).show();
            return false;
        }
        try {
            source.startActivity(request, android.app.ActivityOptions.makeBasic()
                    .setLaunchDisplayId(target.getDisplayId()).toBundle());
            source.finish();
            return true;
        } catch (IllegalArgumentException | SecurityException refused) {
            Toast.makeText(source, "That screen is unavailable. Reconnect it and try again.", Toast.LENGTH_LONG).show();
            return false;
        }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!idle(this)) { finish(); return; }
        manager = getSystemService(DisplayManager.class);
        if (manager != null) manager.registerDisplayListener(this, null);
        question();
    }

    private void page(String title, String description) {
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = Math.round(20 * getResources().getDisplayMetrics().density);
        content.setPadding(pad, pad, pad, pad);
        TextView heading = new TextView(this); heading.setText(title); heading.setTextSize(24); content.addView(heading);
        TextView text = new TextView(this); text.setText(description); text.setTextSize(17); content.addView(text);
        ScrollView scroll = new ScrollView(this); scroll.addView(content); setContentView(scroll);
    }

    private void button(String text, Runnable action) {
        Button button = new Button(this); button.setText(text);
        button.setOnClickListener(v -> action.run()); content.addView(button);
    }

    private String label(Display display) {
        Display.Mode mode = display.getMode();
        return display.getName() + " — " + mode.getPhysicalWidth() + " × " + mode.getPhysicalHeight()
                + (WiiDsScreens.current(this) != null && display.getDisplayId() == WiiDsScreens.current(this).getDisplayId() ? " (this screen)" : "");
    }

    private void question() {
        List<Display> displays = WiiDsScreens.available(this);
        Display current = WiiDsScreens.current(this);
        page("Is this the top screen?", "The library and game picture go on the top screen. Maps and companion information go on the bottom.\n\n"
                + "Connect and enable your second screen in Android first. On devices with one screen, all game menus remain available here.");
        if (current != null && displays.stream().anyMatch(d -> d.getDisplayId() == current.getDisplayId())) {
            button("Yes — use this as the top screen", () -> chooseBottom(current));
            if (displays.size() == 2) {
                Display other = displays.stream().filter(d -> d.getDisplayId() != current.getDisplayId()).findFirst().get();
                button("No — use the other screen on top", () -> chooseBottom(other));
            }
        }
        button("Choose top screen…", this::chooseTop);
        button("Back", this::cancel);
    }

    private void chooseTop() {
        page("Choose top screen", "Choose the screen for the library and game picture.");
        for (Display display : WiiDsScreens.available(this)) button(label(display), () -> chooseBottom(display));
        button("Back", this::question);
    }

    private void chooseBottom(Display top) {
        Intent test = new Intent(this, LibraryActivity.class);
        if (!WiiDsScreens.canLaunch(this, top, test)) {
            Toast.makeText(this, "Enable app launches on this display in your device settings, or choose another top screen.", Toast.LENGTH_LONG).show();
            chooseTop(); return;
        }
        page("Choose bottom screen", "Top: " + label(top) + "\nChoose a separate screen for maps and companion information.");
        for (Display display : WiiDsScreens.available(this))
            if (display.getDisplayId() != top.getDisplayId() && WiiDsScreens.canHostBottom(this, display))
                button(label(display), () -> apply(top, display));
        button("Use top screen only", () -> apply(top, null));
        button("Back", this::question);
    }

    private void apply(Display top, Display bottom) {
        if (!idle(this)) { Toast.makeText(this, "Close the running game before changing screens.", Toast.LENGTH_LONG).show(); return; }
        try {
            WiiDsScreens.save(this, top, bottom);
            Intent request = getIntent().getParcelableExtra(RETURN);
            if (request == null) request = new Intent(this, LibraryActivity.class);
            moveLibrary(this, request, top);
        } catch (Exception failure) {
            Toast.makeText(this, "Could not save screen setup. " + failure.getMessage(), Toast.LENGTH_LONG).show();
            question();
        }
    }

    private void cancel() {
        if (getIntent().hasExtra(RETURN)) {
            Intent request = getIntent().getParcelableExtra(RETURN);
            Display target = WiiDsScreens.top(this);
            if (request != null && target != null && moveLibrary(this, request, target)) return;
            Display current = WiiDsScreens.current(this);
            if (request != null && current != null) moveLibrary(this, request, current);
        } else finish();
    }
    @Override public void onBackPressed() { cancel(); }
    @Override public void onDisplayAdded(int id) { question(); }
    @Override public void onDisplayRemoved(int id) { question(); }
    @Override public void onDisplayChanged(int id) { /* refresh-rate notifications do not reset a choice */ }
    @Override public void onDestroy() { if (manager != null) manager.unregisterDisplayListener(this); super.onDestroy(); }
}
