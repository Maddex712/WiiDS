package dev.chasem.eclipselauncher;

import java.util.concurrent.atomic.AtomicBoolean;

/** Bounds a terminal session teardown; never armed for pause, Home or sleep. */
final class ThorSessionShutdown implements AutoCloseable {
    private final AtomicBoolean complete = new AtomicBoolean();
    private final Thread watchdog;
    ThorSessionShutdown(long graceMillis, Runnable diagnostics, Runnable terminate) {
        watchdog = new Thread(() -> {
            try { Thread.sleep(graceMillis); }
            catch (InterruptedException done) { return; }
            if (complete.compareAndSet(false, true)) {
                try { diagnostics.run(); }
                finally { terminate.run(); }
            }
        }, "ThorSessionExit");
        watchdog.setDaemon(true);
        watchdog.start();
    }
    @Override public void close() {
        if (complete.compareAndSet(false, true)) watchdog.interrupt();
    }
}
