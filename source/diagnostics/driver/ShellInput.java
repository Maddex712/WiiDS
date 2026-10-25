import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.lang.reflect.Method;

/** Bounded shell-only input fixture. Not packaged in WiiDS; always releases held input. */
public final class ShellInput {
    private static Object manager;
    private static Method inject;
    private static int device;
    private static long down;
    private static void send(InputEvent event) throws Exception {
        if (!(Boolean) inject.invoke(manager, event, 2)) throw new IllegalStateException("Input rejected");
    }
    private static void motion(float x, float y, float rx, float ry) throws Exception {
        MotionEvent.PointerProperties p = new MotionEvent.PointerProperties(); p.id = 0;
        MotionEvent.PointerCoords c = new MotionEvent.PointerCoords();
        c.setAxisValue(MotionEvent.AXIS_X, x); c.setAxisValue(MotionEvent.AXIS_Y, y);
        c.setAxisValue(MotionEvent.AXIS_Z, rx); c.setAxisValue(MotionEvent.AXIS_RZ, ry);
        MotionEvent e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_MOVE, 1,
                new MotionEvent.PointerProperties[]{p}, new MotionEvent.PointerCoords[]{c}, 0, 0, 1, 1,
                device, 0, InputDevice.SOURCE_JOYSTICK, 0);
        try { send(e); } finally { e.recycle(); }
    }
    private static void key(int code, boolean pressed) throws Exception {
        send(new KeyEvent(down, SystemClock.uptimeMillis(), pressed ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP,
                code, 0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD));
    }
    public static void main(String[] args) throws Exception {
        if (args.length < 5) throw new IllegalArgumentException("duration, left X/Y, right X/Y, optional keys");
        int duration = Integer.parseInt(args[0]);
        if (duration < 1 || duration > 5000) throw new IllegalArgumentException("duration exceeds limit");
        float[] axes = new float[4];
        for (int i = 0; i < 4; i++) {
            axes[i] = Float.parseFloat(args[i + 1]);
            if (!Float.isFinite(axes[i]) || Math.abs(axes[i]) > 1) throw new IllegalArgumentException("invalid axis");
        }
        int[] keys = new int[args.length - 5];
        for (int i = 0; i < keys.length; i++) {
            keys[i] = Integer.parseInt(args[i + 5]);
            if (keys[i] < KeyEvent.KEYCODE_BUTTON_A || keys[i] > KeyEvent.KEYCODE_BUTTON_MODE)
                throw new IllegalArgumentException("gamepad buttons only");
        }
        Class<?> type = Class.forName("android.hardware.input.InputManager");
        manager = type.getMethod("getInstance").invoke(null);
        inject = type.getMethod("injectInputEvent", InputEvent.class, int.class);
        device = -1;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice input = InputDevice.getDevice(id);
            if (input != null && (input.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                device = id;
                if (input.getName().contains("Odin") || input.getName().contains("Thor")) break;
            }
        }
        down = SystemClock.uptimeMillis(); int sent = 0;
        try {
            for (int code : keys) key(code, true);
            while (SystemClock.uptimeMillis() - down < duration) {
                motion(axes[0], axes[1], axes[2], axes[3]); sent++; SystemClock.sleep(16);
            }
        } finally {
            Exception releaseError = null;
            try { motion(0, 0, 0, 0); } catch (Exception failure) { releaseError = failure; }
            for (int code : keys) try { key(code, false); } catch (Exception failure) { releaseError = failure; }
            if (releaseError != null) throw releaseError;
        }
        System.out.println("Injected " + sent + " joystick frames; all input released");
    }
}
