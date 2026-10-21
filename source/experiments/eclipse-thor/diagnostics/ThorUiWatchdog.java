package dev.chasem.smslauncher;

import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import java.util.function.Supplier;

/** Short, observational Java-stack capture for the Eclipse startup ANR investigation. */
final class ThorUiWatchdog {
    private static final String TAG = "EclipseUiWatch";
    private ThorUiWatchdog() {}

    static void start(Supplier<Thread> sdlThread) {
        if (!BuildConfig.DEBUG) return;
        final Thread main = Looper.getMainLooper().getThread();
        Thread observer = new Thread(() -> {
            long began = SystemClock.uptimeMillis();
            int[] samples = {2, 5, 10, 20, 30};
            Log.i(TAG, "Java startup observation begin pid=" + android.os.Process.myPid());
            for (int seconds : samples) {
                long remaining = began + seconds * 1000L - SystemClock.uptimeMillis();
                if (remaining > 0) {
                    try { Thread.sleep(remaining); }
                    catch (InterruptedException stopped) { Thread.currentThread().interrupt(); return; }
                }
                long elapsed = SystemClock.uptimeMillis() - began;
                if (elapsed > 60000) break;
                Log.i(TAG, "sample elapsed_ms=" + elapsed);
                dump("main", main);
                dump("SDLThread", sdlThread.get());
            }
            Log.i(TAG, "Java startup observation complete");
        }, "EclipseUiWatch");
        observer.setDaemon(true);
        observer.start();
    }

    private static void dump(String label, Thread thread) {
        if (thread == null) { Log.i(TAG, label + " not started"); return; }
        Log.i(TAG, label + " name=" + thread.getName() + " state=" + thread.getState());
        StackTraceElement[] stack = thread.getStackTrace();
        for (int i = 0; i < Math.min(25, stack.length); i++) Log.i(TAG, label + " at " + stack[i]);
        if (stack.length > 25) Log.i(TAG, label + " remaining_frames=" + (stack.length - 25));
    }
}
