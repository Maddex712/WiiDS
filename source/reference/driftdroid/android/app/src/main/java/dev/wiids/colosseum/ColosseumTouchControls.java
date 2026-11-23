package dev.wiids.colosseum;

import android.content.Context;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import com.driftdroid.android.dualscreen.WiiDsGameMenu;

import java.util.ArrayList;
import java.util.List;

/** The touch controller sends ordinary GameCube input through the Colosseum host. */
final class ColosseumTouchControls extends LinearLayout {
    interface Input {
        void send(int key, boolean pressed);
    }

    private final List<TouchKey> keys = new ArrayList<>();

    ColosseumTouchControls(Context context, Input input) {
        super(context);
        setOrientation(VERTICAL);
        row(input, new String[]{"↑", "↓"}, new String[]{"Up", "Down"},
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN);
        row(input, new String[]{"←", "→"}, new String[]{"Left", "Right"},
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT);
        row(input, new String[]{"A", "B"}, new String[]{"Confirm", "Cancel"},
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B);
        row(input, new String[]{"Start"}, new String[]{"Start"},
                KeyEvent.KEYCODE_BUTTON_START);
    }

    private void row(Input input, String[] labels, String[] descriptions, int... keyCodes) {
        LinearLayout row = new LinearLayout(getContext());
        addView(row, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        for (int i = 0; i < keyCodes.length; ++i) {
            Button button = WiiDsGameMenu.button(getContext(), WiiDsGameMenu.Palette.ORRE, labels[i], false);
            button.setContentDescription(descriptions[i]);
            // Physical controller events should stay routed to the game.
            button.setFocusable(false);
            LayoutParams size = new LayoutParams(0, WiiDsGameMenu.dp(getContext(), 44), 1);
            size.leftMargin = i == 0 ? 0 : WiiDsGameMenu.dp(getContext(), 8);
            size.bottomMargin = WiiDsGameMenu.dp(getContext(), 8);
            row.addView(button, size);
            TouchKey key = new TouchKey(button, input, keyCodes[i]);
            keys.add(key);
            button.setOnTouchListener(key);
            button.setOnClickListener(ignored -> {
                key.press();
                key.scheduleRelease();
            });
        }
    }

    void releaseAll() {
        for (TouchKey key : keys) key.release();
    }

    @Override protected void onDetachedFromWindow() {
        releaseAll();
        super.onDetachedFromWindow();
    }

    private static final class TouchKey implements View.OnTouchListener {
        private final Button button;
        private final Input input;
        private final int code;
        private final Runnable release = this::release;
        private long pressedAt;

        TouchKey(Button button, Input input, int code) {
            this.button = button;
            this.input = input;
            this.code = code;
        }

        void press() {
            button.removeCallbacks(release);
            pressedAt = SystemClock.uptimeMillis();
            input.send(code, true);
            button.setPressed(true);
        }

        void scheduleRelease() {
            // A short tap must still span more than one guest controller poll.
            button.postDelayed(release, Math.max(0, 70 - (SystemClock.uptimeMillis() - pressedAt)));
        }

        void release() {
            button.removeCallbacks(release);
            if (pressedAt != 0) input.send(code, false);
            pressedAt = 0;
            button.setPressed(false);
        }

        @Override public boolean onTouch(View view, MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    press();
                    break;
                case MotionEvent.ACTION_UP:
                    scheduleRelease();
                    break;
                case MotionEvent.ACTION_CANCEL:
                    release();
                    break;
                default:
                    break;
            }
            return true;
        }
    }
}
