// WiiDS: gamepad rumble for SDL 2.32.10, whose Android joystick driver had none
// (SDL_sysjoystick.c ANDROID_JoystickRumble now calls SDLControllerManager.hapticRumble, as SDL 3 does).
package org.libsdl.sdl2.app;

import java.util.HashMap;
import java.util.Map;

import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;

final class SDLGamepadRumble {
    // The games pulse the motor (a GameCube motor is only on or off), often stopping within a
    // frame of starting; a real motor needs time to spin up, so a stop that early is held back to
    // this minimum, as org.libsdl.app's SDLHapticHandler_API31 does for the other games.
    private static final long MIN_VIBRATION_MS = 80;
    // A handheld's built-in controls with no motor of their own: the AYN Thor's "Odin Controller"
    // (no force feedback; Android counts it as external since it sits on USB) uses the device's vibrator.
    private static final String[] BUILT_IN = { "Odin Controller" };

    private static final Map<Vibrator, Long> sStartTimes = new HashMap<>();
    private static final Map<Vibrator, Runnable> sPendingStops = new HashMap<>();
    private static final Handler sHandler = new Handler(Looper.getMainLooper());
    private static Vibrator sSystemVibrator;

    private SDLGamepadRumble() {}

    static synchronized void rumble(int device_id, float low_frequency_intensity, float high_frequency_intensity, int length) {
        InputDevice device = InputDevice.getDevice(device_id);
        if (device == null) {
            return;
        }
        float intensity = (low_frequency_intensity * 0.6f) + (high_frequency_intensity * 0.4f);
        if (Build.VERSION.SDK_INT >= 31 /* Android 12.0 (S) */) {
            VibratorManager manager = device.getVibratorManager();
            int[] vibrators = manager.getVibratorIds();
            if (vibrators.length >= 2) {
                vibrate(manager.getVibrator(vibrators[0]), low_frequency_intensity, length);
                vibrate(manager.getVibrator(vibrators[1]), high_frequency_intensity, length);
                return;
            }
            if (vibrators.length == 1) {
                vibrate(manager.getVibrator(vibrators[0]), intensity, length);
                return;
            }
        } else {
            Vibrator own = device.getVibrator();
            if (own != null && own.hasVibrator()) {
                vibrate(own, intensity, length);
                return;
            }
        }
        Vibrator builtIn = builtInVibrator(device);
        if (builtIn != null) {
            vibrate(builtIn, intensity, length);
        }
    }

    private static Vibrator builtInVibrator(InputDevice device) {
        boolean builtIn = false;
        for (String name : BUILT_IN) {
            builtIn |= name.equals(device.getName());
        }
        if (!builtIn) {
            return null;
        }
        if (sSystemVibrator == null) {
            Context context = SDL.getContext();
            if (context == null) {
                return null;
            }
            Vibrator vibrator;
            if (Build.VERSION.SDK_INT >= 31 /* Android 12.0 (S) */) {
                VibratorManager manager = context.getSystemService(VibratorManager.class);
                vibrator = manager != null ? manager.getDefaultVibrator() : null;
            } else {
                vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
            }
            sSystemVibrator = vibrator != null && vibrator.hasVibrator() ? vibrator : null;
        }
        return sSystemVibrator;
    }

    private static void vibrate(Vibrator vibrator, float intensity, int length) {
        Runnable pendingStop = sPendingStops.remove(vibrator);
        if (pendingStop != null) {
            sHandler.removeCallbacks(pendingStop);
        }
        int value = Math.min(255, Math.round(intensity * 255));
        if (value < 1) {
            Long startedAt = sStartTimes.get(vibrator);
            long elapsed = startedAt != null ? SystemClock.uptimeMillis() - startedAt : Long.MAX_VALUE;
            if (elapsed >= MIN_VIBRATION_MS) {
                vibrator.cancel();
                return;
            }
            Runnable stop = new Runnable() {
                @Override public void run() {
                    synchronized (SDLGamepadRumble.class) {
                        // A newer request may arrive after this callback was dequeued.
                        if (sPendingStops.get(vibrator) != this) return;
                        sPendingStops.remove(vibrator);
                        vibrator.cancel();
                    }
                }
            };
            sPendingStops.put(vibrator, stop);
            sHandler.postDelayed(stop, MIN_VIBRATION_MS - elapsed);
            return;
        }
        sStartTimes.put(vibrator, SystemClock.uptimeMillis());
        try {
            VibrationEffect effect = VibrationEffect.createOneShot(length, value);
            if (vibrator == sSystemVibrator && Build.VERSION.SDK_INT >= 33 /* Android 13 (T) */) {
                // Game rumble is media vibration, which Android's own Vibration setting still turns off.
                vibrator.vibrate(effect, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA));
            } else {
                vibrator.vibrate(effect);
            }
        } catch (Exception e) {
            vibrator.vibrate(length);
        }
    }
}
