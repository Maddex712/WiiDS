package com.driftdroid.android.dualscreen;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class WiiDsGameMenu {
    public enum Palette {
        ORRE(0xff252625, 0xff30312f, 0xff464842, 0xffedece6, 0xffb7b9af, 0xffe7b66a),
        TOWN(0xfff1eee4, 0xffe6e2d6, 0xffcbc6b7, 0xff232b23, 0xff475346, 0xff28573e);

        public final int background, surface, line, ink, muted, accent;

        Palette(int background, int surface, int line, int ink, int muted, int accent) {
            this.background = background;
            this.surface = surface;
            this.line = line;
            this.ink = ink;
            this.muted = muted;
            this.accent = accent;
        }
    }

    public static final class Action {
        final String label;
        final Runnable invoke;

        public Action(String label, Runnable invoke) {
            this.label = label;
            this.invoke = invoke;
        }
    }

    private WiiDsGameMenu() {}

    public static AlertDialog create(Activity activity, Palette palette, String game, Bitmap art,
                                     String title, String message, Action... actions) {
        LinearLayout panel = new LinearLayout(activity);
        int inset = dp(activity, 20);
        int gap = dp(activity, 20);
        panel.setPadding(inset, inset, inset, inset);
        panel.setBackground(surface(activity, palette.background, palette.line, 12));
        panel.setBaselineAligned(false);

        LinearLayout identity = new LinearLayout(activity);
        identity.setOrientation(LinearLayout.VERTICAL);
        identity.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams identitySize = new LinearLayout.LayoutParams(0, -1, 4);
        identitySize.rightMargin = gap;
        panel.addView(identity, identitySize);

        TextView brand = text(activity, "WiiDS", 13, palette.accent);
        brand.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        identity.addView(brand);
        TextView heading = text(activity, title, 25, palette.ink);
        heading.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        identity.addView(heading, spaced(activity, 4));
        identity.addView(text(activity, game, 13, palette.muted), spaced(activity, 4));
        if (art != null) {
            ImageView image = banner(activity, art);
            LinearLayout.LayoutParams imageSize = new LinearLayout.LayoutParams(-1, dp(activity, 64));
            imageSize.topMargin = dp(activity, 16);
            identity.addView(image, imageSize);
        }

        LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setGravity(Gravity.CENTER_VERTICAL);
        panel.addView(list, new LinearLayout.LayoutParams(0, -2, 5));
        AlertDialog dialog = new AlertDialog.Builder(activity).setView(panel).create();
        if (message != null) {
            TextView body = text(activity, message, 14, palette.ink);
            LinearLayout.LayoutParams size = spaced(activity, 0);
            size.bottomMargin = dp(activity, 12);
            list.addView(body, size);
        }
        Button[] rows = new Button[actions.length];
        for (int i = 0; i < actions.length; i++) {
            Action action = actions[i];
            Button button = button(activity, palette, action.label, i == 0);
            button.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            button.setOnClickListener(view -> {
                if (action.invoke != null) action.invoke.run();
                dialog.dismiss();
            });
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(-1, dp(activity, 44));
            size.topMargin = i == 0 ? 0 : dp(activity, 4);
            list.addView(button, size);
            rows[i] = button;
        }
        Button initial = rows.length == 0 ? null : rows[0];
        dialog.setOnKeyListener((ignored, code, event) -> {
            if (code == KeyEvent.KEYCODE_BUTTON_START) {
                if (event.getAction() == KeyEvent.ACTION_UP) dialog.dismiss();
                return true;
            }
            if (isBack(code) && initial != null) {
                if (event.getAction() == KeyEvent.ACTION_UP) initial.performClick();
                return true;
            }
            if (rows.length != 0 && (code == KeyEvent.KEYCODE_DPAD_DOWN || code == KeyEvent.KEYCODE_DPAD_UP)) {
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    int current = focusedRow(rows);
                    int direction = code == KeyEvent.KEYCODE_DPAD_DOWN ? 1 : -1;
                    int next = current < 0 ? 0 : Math.min(rows.length - 1, Math.max(0, current + direction));
                    rows[next].requestFocus();
                }
                return true;
            }
            if (rows.length != 0 && (code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_DPAD_CENTER)) {
                if (event.getAction() == KeyEvent.ACTION_UP) rows[Math.max(0, focusedRow(rows))].performClick();
                return true;
            }
            return handleController(dialog, code, event);
        });
        dialog.setOnShowListener(ignored -> {
            if (dialog.getWindow() != null) dialog.getWindow().setBackgroundDrawableResource(android.R.color.transparent);
            if (initial != null) initial.requestFocusFromTouch();
        });
        return dialog;
    }

    private static int focusedRow(Button[] rows) {
        for (int index = 0; index < rows.length; index++) {
            if (rows[index].isFocused()) return index;
        }
        return -1;
    }

    public static ImageView banner(Context context, Bitmap image) {
        ImageView view = new ImageView(context);
        view.setScaleType(ImageView.ScaleType.FIT_START);
        view.setImageBitmap(image);
        view.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        return view;
    }

    public static Button button(Context context, Palette palette, String label, boolean primary) {
        Button button = new Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setMinWidth(0);
        button.setMinimumWidth(0);
        button.setMinHeight(0);
        button.setMinimumHeight(0);
        button.setTextSize(14);
        button.setTextColor(primary ? (palette == Palette.TOWN ? Color.WHITE : palette.background) : palette.ink);
        button.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        int padding = dp(context, 12);
        button.setPadding(padding, 0, padding, 0);
        int fill = primary ? palette.accent : palette.surface;
        StateListDrawable states = new StateListDrawable();
        states.addState(new int[]{android.R.attr.state_pressed}, surface(context, palette.background, palette.accent, 6));
        states.addState(new int[]{android.R.attr.state_focused}, surface(context, fill, palette.ink, 6));
        states.addState(new int[0], surface(context, fill, fill, 6));
        button.setBackground(states);
        if (primary) {
            int text = palette == Palette.TOWN ? Color.WHITE : palette.background;
            button.setTextColor(new android.content.res.ColorStateList(
                    new int[][]{{android.R.attr.state_pressed}, {}}, new int[]{palette.ink, text}));
        }
        button.setStateListAnimator(null);
        return button;
    }

    public static TextView text(Context context, String label, int size, int color) {
        TextView text = new TextView(context);
        text.setText(label);
        text.setTextSize(size);
        text.setTextColor(color);
        text.setIncludeFontPadding(false);
        return text;
    }

    public static GradientDrawable surface(Context context, int fill, int border, int radius) {
        GradientDrawable background = new GradientDrawable();
        background.setColor(fill);
        background.setCornerRadius(dp(context, radius));
        if (fill != border) background.setStroke(dp(context, 1), border);
        return background;
    }

    public static int dp(Context context, int size) {
        return Math.round(size * context.getResources().getDisplayMetrics().density);
    }

    private static LinearLayout.LayoutParams spaced(Context context, int margin) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.topMargin = dp(context, margin);
        return params;
    }

    public static boolean handleController(AlertDialog dialog, int code, KeyEvent event) {
        if (isBack(code)) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                Button back = dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
                if (back != null) back.performClick(); else dialog.dismiss();
            }
            return true;
        }
        View focus = dialog.getCurrentFocus();
        if (code == KeyEvent.KEYCODE_BUTTON_A && focus != null) {
            return focus.dispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
                    KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
        }
        return false;
    }

    private static boolean isBack(int code) {
        return code == KeyEvent.KEYCODE_BUTTON_B || code == KeyEvent.KEYCODE_BUTTON_SELECT ||
                code == KeyEvent.KEYCODE_ESCAPE || code == KeyEvent.KEYCODE_BACK;
    }
}
