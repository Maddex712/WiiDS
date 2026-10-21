package dev.chasem.smslauncher;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.hardware.input.InputManager;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.*;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Receives Android events directly, before SDL mapping or game bindings. */
public class InputMonitorActivity extends Activity implements InputManager.InputDeviceListener {
    private final InputMonitorLog log = new InputMonitorLog();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final long started = SystemClock.uptimeMillis();
    private InputManager input;
    private TextView devices, events;
    private String inventory = "", exportSnapshot;
    private boolean active, pending;
    private final Runnable redraw = () -> {
        pending = false;
        if (events != null) events.setText(log.live() + "\n" + log.recent());
    };
    private long time() { return SystemClock.uptimeMillis() - started; }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(Color.WHITE);
        v.setPadding(dp(12), dp(6), dp(12), dp(6)); return v;
    }
    private void button(LinearLayout row, String label, Runnable action) {
        Button b = new Button(this); b.setText(label); b.setAllCaps(false);
        b.setOnClickListener(v -> action.run()); row.addView(b);
    }
    private TextView panel(LinearLayout columns, String title) {
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        card.setBackgroundColor(Color.rgb(18,36,52));
        LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, -1, 1);
        size.setMargins(dp(6),0,dp(6),0); columns.addView(card,size);
        LinearLayout heading = new LinearLayout(this);
        heading.addView(text(title,18),new LinearLayout.LayoutParams(0,-2,1));
        if (title.startsWith("Connected")) button(heading,"Refresh",() -> { inventory(); log.note(time(),"Device inventory refreshed"); schedule(); });
        card.addView(heading);
        ScrollView scroll = new ScrollView(this); scroll.setScrollbarFadingEnabled(false);
        scroll.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_INSET);
        android.graphics.drawable.GradientDrawable thumb = new android.graphics.drawable.GradientDrawable();
        thumb.setColor(Color.rgb(140,170,190)); thumb.setCornerRadius(dp(2)); thumb.setSize(dp(4),dp(24));
        scroll.setVerticalScrollbarThumbDrawable(thumb);
        card.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        TextView body = text("",12); body.setTypeface(Typeface.MONOSPACE);
        scroll.addView(body); return body;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state); input = getSystemService(InputManager.class);
        if (state != null) exportSnapshot = state.getString("inputExport");
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(12,26,42));
        LinearLayout top = new LinearLayout(this);
        top.addView(text("Input test",20),new LinearLayout.LayoutParams(0,-2,1));
        button(top,"Clear",() -> { log.clear(); schedule(); });
        button(top,"Export",this::export);
        button(top,"Done",this::finish); root.addView(top);
        root.addView(text("Press keys/buttons or move sticks. Captures only while this screen is open. Controller Back is shown here; use Done to close.",13));
        LinearLayout columns = new LinearLayout(this);
        root.addView(columns,new LinearLayout.LayoutParams(-1,0,1));
        devices = panel(columns,"Connected devices · scroll");
        events = panel(columns,"Live input · scroll");
        root.addView(text(BuildInfo.summary(),12));
        root.setOnApplyWindowInsetsListener((view,insets) -> {
            android.graphics.Insets safe = Build.VERSION.SDK_INT >= 30
                ? insets.getInsets(android.view.WindowInsets.Type.systemBars() | android.view.WindowInsets.Type.displayCutout())
                : android.graphics.Insets.of(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            view.setPadding(safe.left,safe.top,safe.right,safe.bottom); return insets;
        });
        setContentView(root);
    }
    private void schedule() {
        if (!pending) { pending = true; handler.postDelayed(redraw,50); }
    }
    private static String sources(int source) {
        StringBuilder s = new StringBuilder();
        int[] masks = {InputDevice.SOURCE_KEYBOARD,InputDevice.SOURCE_GAMEPAD,InputDevice.SOURCE_JOYSTICK,
            InputDevice.SOURCE_DPAD,InputDevice.SOURCE_MOUSE,InputDevice.SOURCE_TOUCHSCREEN};
        String[] names = {"keyboard","gamepad","joystick","d-pad","mouse","touch"};
        for (int i=0;i<masks.length;i++) if ((source & masks[i]) == masks[i]) s.append(names[i]).append(' ');
        return s + String.format(Locale.ROOT,"(0x%x)",source);
    }
    private static String deviceName(int id) {
        InputDevice d = InputDevice.getDevice(id);
        return "Device " + id + " " + (d == null ? "unknown" : d.getName());
    }
    private static String modifiers(KeyEvent event) {
        StringBuilder out = new StringBuilder("modifiers ");
        if (event.isShiftPressed()) out.append("Shift ");
        if (event.isCtrlPressed()) out.append("Ctrl ");
        if (event.isAltPressed()) out.append("Alt ");
        if (event.isMetaPressed()) out.append("Meta ");
        if (event.isSymPressed()) out.append("Sym ");
        if (event.isFunctionPressed()) out.append("Fn ");
        if (event.isCapsLockOn()) out.append("CapsLock ");
        if (event.isNumLockOn()) out.append("NumLock ");
        if (event.isScrollLockOn()) out.append("ScrollLock ");
        return out + String.format(Locale.ROOT,"(0x%x)",event.getMetaState());
    }
    private void inventory() {
        StringBuilder out = new StringBuilder(BuildInfo.summary()).append('\n')
            .append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" · Android ").append(Build.VERSION.RELEASE).append('\n');
        int[] ids = InputDevice.getDeviceIds();
        out.append("\nAndroid input devices: ").append(ids.length).append('\n');
        for (int id : ids) {
            InputDevice d = InputDevice.getDevice(id); if (d == null) continue;
            out.append('\n').append(deviceName(id)).append('\n')
                .append(String.format(Locale.ROOT,"VID:PID %04x:%04x",d.getVendorId(),d.getProductId()))
                .append(d.isVirtual() ? " · virtual" : " · physical").append('\n')
                .append(sources(d.getSources())).append('\n');
            for (InputDevice.MotionRange r : d.getMotionRanges())
                out.append(MotionEvent.axisToString(r.getAxis())).append(String.format(Locale.ROOT," %.2f..%.2f flat %.3f\n",r.getMin(),r.getMax(),r.getFlat()));
        }
        out.append("\nUSB devices (enumeration only):\n");
        UsbManager usb = getSystemService(UsbManager.class);
        if (usb == null || usb.getDeviceList().isEmpty()) out.append("None listed. Bluetooth keyboards/controllers appear above.\n");
        else for (UsbDevice d : usb.getDeviceList().values())
            out.append(d.getProductName() == null ? d.getDeviceName() : d.getProductName())
                .append(String.format(Locale.ROOT," · %04x:%04x · class %d\n",d.getVendorId(),d.getProductId(),d.getDeviceClass()));
        out.append("\nA listed USB adapter without an Android input device may need a device-specific driver. Events here confirm Android input, not game mapping.\n");
        inventory = out.toString(); devices.setText(inventory);
    }
    @Override protected void onResume() {
        super.onResume(); active = true;
        if (input != null) input.registerInputDeviceListener(this,handler);
        inventory(); schedule();
    }
    @Override protected void onPause() {
        active = false;
        if (input != null) input.unregisterInputDeviceListener(this);
        log.release(); handler.removeCallbacks(redraw); pending = false;
        super.onPause();
    }
    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        if (exportSnapshot != null) state.putString("inputExport",exportSnapshot);
    }
    @Override protected void onDestroy() { handler.removeCallbacksAndMessages(null); io.shutdown(); super.onDestroy(); }
    @Override public void onInputDeviceAdded(int id) { log.note(time(),deviceName(id)+" added"); inventory(); schedule(); }
    @Override public void onInputDeviceChanged(int id) { log.note(time(),deviceName(id)+" changed"); inventory(); schedule(); }
    @Override public void onInputDeviceRemoved(int id) { log.removeDevice(time(),id); inventory(); schedule(); }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused) { log.release(); schedule(); }
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (!active) return super.dispatchKeyEvent(event);
        String action = event.getAction() == KeyEvent.ACTION_DOWN ? "DOWN" : event.getAction() == KeyEvent.ACTION_UP ? "UP" : "MULTIPLE";
        String description = deviceName(event.getDeviceId()) + "\n" + action + " " + KeyEvent.keyCodeToString(event.getKeyCode())
            + " (" + event.getKeyCode() + "), scan " + event.getScanCode() + ", repeat " + event.getRepeatCount()
            + ", " + modifiers(event) + ", " + sources(event.getSource());
        log.key(time(),event.getDeviceId(),event.getKeyCode(),event.getScanCode(),action,description); schedule();
        InputDevice d = event.getDevice();
        boolean controller = d != null && (d.supportsSource(InputDevice.SOURCE_GAMEPAD) || d.supportsSource(InputDevice.SOURCE_JOYSTICK)
            || (d.supportsSource(InputDevice.SOURCE_DPAD) && d.getKeyboardType() != InputDevice.KEYBOARD_TYPE_ALPHABETIC));
        if (controller) return true; // show Back/B and d-pad without closing or navigating the test
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK || event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_UP
            || event.getKeyCode() == KeyEvent.KEYCODE_VOLUME_DOWN) return super.dispatchKeyEvent(event);
        return true; // keyboard presses are measured instead of moving UI focus
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (!active || (!event.isFromSource(InputDevice.SOURCE_JOYSTICK) && !event.isFromSource(InputDevice.SOURCE_GAMEPAD)))
            return super.dispatchGenericMotionEvent(event);
        InputDevice d = event.getDevice();
        StringBuilder values = new StringBuilder(deviceName(event.getDeviceId())).append('\n');
        if (d != null) for (InputDevice.MotionRange r : d.getMotionRanges())
            if (event.isFromSource(r.getSource())) values.append(MotionEvent.axisToString(r.getAxis()))
                .append(String.format(Locale.ROOT,"=%.3f ",event.getAxisValue(r.getAxis())));
        log.motion(time(),event.getDeviceId(),values.toString()); schedule(); return true;
    }
    private void export() {
        exportSnapshot = log.report(inventory);
        startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("text/plain").putExtra(Intent.EXTRA_TITLE,"sunshine-input.log"),1);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if (request != 1) return;
        if (result != RESULT_OK || data == null || data.getData() == null || exportSnapshot == null) { exportSnapshot = null; return; }
        final String snapshot = exportSnapshot;
        exportSnapshot = null;
        io.execute(() -> {
            String resultText;
            try (OutputStream out = getContentResolver().openOutputStream(data.getData(),"wt")) {
                if (out == null) throw new java.io.IOException("Destination could not be opened.");
                out.write(snapshot.getBytes(StandardCharsets.UTF_8)); resultText = "Input log exported.";
            } catch (Exception e) { resultText = "Could not export: "+e.getMessage(); }
            final String message = resultText;
            runOnUiThread(() -> { if (!isDestroyed()) Toast.makeText(this,message,Toast.LENGTH_LONG).show(); });
        });
    }
}
