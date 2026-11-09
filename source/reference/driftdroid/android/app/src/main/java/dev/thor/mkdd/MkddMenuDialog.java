package dev.thor.mkdd;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The WiiDS menu's own window. NativeActivity owns its window's surface (the game draws there),
 * so views added to it never show: the menu is a full-screen, transparent window above it, with
 * the frozen game frame visible underneath.
 */
final class MkddMenuDialog extends Dialog {
    MkddMenuDialog(Activity activity, DuoMenu menu) {
        super(activity, android.R.style.Theme_Translucent_NoTitleBar_Fullscreen);
        setCancelable(false);  // Back reaches DuoMenu, which goes back a page or closes the menu
        setContentView(menu);
        Window window = getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT);
            window.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController insets = window.getInsetsController();
            if (insets != null) {
                insets.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                insets.hide(WindowInsets.Type.systemBars());
            }
        }
        menu.requestFocus();
    }
}
