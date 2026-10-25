package com.driftdroid.android.achievements;

import android.app.Activity;
import android.util.Log;
import android.widget.Toast;
import java.io.File;
import java.util.*;
import java.util.concurrent.*;

/** Consumes guest-thread events without depending on the lower display being present. */
public final class NsmbAchievementSession {
    private final Activity activity;
    private final NsmbAchievements.Ledger ledger;
    private final ExecutorService writer = Executors.newSingleThreadExecutor();
    private int cursor;
    private volatile boolean failed;
    /** Receives only IDs the ledger newly recorded, in order, on the UI thread. */
    public interface Listener { void unlocked(List<String> ids, long time); }
    private volatile Listener listener;
    public void setListener(Listener value) { listener = value; }
    public NsmbAchievementSession(Activity activity) {
        this.activity = activity;
        ledger = new NsmbAchievements.Ledger(new File(activity.getNoBackupFilesDir(), "achievements/local"));
    }
    public int cursor() { return cursor; }
    public void accept(int[] words) {
        if (failed || words == null || words.length < 3 || words[0] != 1 || (words.length - 3) % 6 != 0) return;
        final long hash = ((long)words[1] << 32) | Integer.toUnsignedLong(words[2]);
        if (hash == 0) return;
        for (int i = 3; i < words.length; i += 6) {
            if (words[i] <= cursor) continue;
            if (words[i] != cursor + 1) {
                // An overflow cannot safely be interpreted as a complete event history.
                cursor = words[i];
                Log.w("WiiDSAchievements", "Missed achievement events; skipped incomplete history");
                continue;
            }
            cursor = words[i];
            final int slot = words[i+2];
            final List<String> ids = NsmbAchievements.evaluate(words[i+1], slot, words[i+3], words[i+4], words[i+5]);
            if (ids.isEmpty()) continue;
            final String profile = NsmbAchievements.profile(hash, slot);
            final long time = System.currentTimeMillis();
            writer.execute(() -> {
                try {
                    List<String> earned = ledger.record(profile, ids, time);
                    if (!earned.isEmpty()) activity.runOnUiThread(() -> {
                        Listener target = listener;
                        if (target != null && !activity.isFinishing() && !activity.isDestroyed()) target.unlocked(earned, time);
                    });
                } catch (Exception error) {
                    failed = true;
                    activity.runOnUiThread(() -> Toast.makeText(activity,
                        "Achievements could not be saved. Your game save is unaffected.", Toast.LENGTH_LONG).show());
                }
            });
        }
    }
    public void close() {
        writer.shutdown();
        try { writer.awaitTermination(500, TimeUnit.MILLISECONDS); }
        catch (InterruptedException interrupted) { Thread.currentThread().interrupt(); }
    }
}
