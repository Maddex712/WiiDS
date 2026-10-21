package dev.chasem.smslauncher;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class ThorSessionShutdownTest {
    public static void main(String[] args) throws Exception {
        AtomicInteger diagnostics = new AtomicInteger(), exits = new AtomicInteger();
        CountDownLatch timedOut = new CountDownLatch(1);
        ThorSessionShutdown stalled = new ThorSessionShutdown(25, () -> {
            if (!Thread.currentThread().isDaemon()) throw new AssertionError("Watchdog is not a daemon");
            diagnostics.incrementAndGet();
        }, () -> { exits.incrementAndGet(); timedOut.countDown(); });
        if (!timedOut.await(3, TimeUnit.SECONDS)) throw new AssertionError("Stalled teardown did not terminate");
        stalled.close(); stalled.close();
        if (diagnostics.get() != 1 || exits.get() != 1) throw new AssertionError("Termination was not exactly once");
        CountDownLatch unexpected = new CountDownLatch(1);
        ThorSessionShutdown normal = new ThorSessionShutdown(50, unexpected::countDown, unexpected::countDown);
        normal.close(); normal.close();
        if (unexpected.await(250, TimeUnit.MILLISECONDS)) throw new AssertionError("Cancelled normal teardown ran watchdog");
        System.out.println("PASS actual-source shutdown guard: stalled teardown diagnostics/exit once, daemon, normal cancellation");
    }
}
