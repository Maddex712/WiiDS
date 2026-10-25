package com.driftdroid.android.achievements;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;

/**
 * Each WiiDS game's RetroAchievements sets (base set and subsets, or a mod's own hack set) with
 * this account's completions, read through the official Web API and kept on the device. RA's
 * achievements are what WiiDS shows; WiiDS never submits unlocks or changes RA points.
 */
public final class RaLibrary {
    private RaLibrary() {}
    public static boolean known(String game) { return RaSets.known(game); }
    static final int WII = RaSets.WII, GAMECUBE = RaSets.GAMECUBE;
    static final long DAY = 24L * 60 * 60 * 1000, WEEK = 7 * DAY, MANUAL_GAP = 60_000;

    // ------------------------------------------------------------------ sync
    private static final ExecutorService worker = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "RaLibrarySync"); thread.setDaemon(true); return thread;
    });
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static volatile boolean running;
    public static boolean running() { return running; }

    public interface Done { void finished(boolean ok, String message, JSONObject library); }

    private static SharedPreferences prefs(Context context) { return context.getSharedPreferences("ra_library", Context.MODE_PRIVATE); }

    /** Library opened: refresh at most once a day, quietly, when an account is connected. */
    public static void syncIfDue(Context context, Done done) {
        Context app = context.getApplicationContext();
        long now = System.currentTimeMillis(), last = prefs(app).getLong("synced", 0);
        if (running || (now >= last && now - last < DAY)) return;
        try { if (new RaCredentials(app).read() == null) return; } catch (IOException broken) { return; }
        start(app, done);
    }

    /** The Refresh button (or a new connection): at most once a minute. Returns a message when refused. */
    public static String syncNow(Context context, Done done) {
        Context app = context.getApplicationContext();
        long now = System.currentTimeMillis(), last = prefs(app).getLong("manual", 0);
        if (running) return "Already refreshing your RetroAchievements progress.";
        if (now >= last && now - last < MANUAL_GAP) return "Please wait a minute between refreshes.";
        prefs(app).edit().putLong("manual", now).apply();
        start(app, done);
        return null;
    }

    private static void start(Context app, Done done) {
        running = true;
        worker.execute(() -> {
            JSONObject result = null;
            String message;
            boolean ok = false;
            try {
                result = sync(app);
                int earned = 0, total = 0;
                for (int i = 0; i < result.getJSONArray("sets").length(); i++) {
                    JSONArray rows = result.getJSONArray("sets").getJSONObject(i).getJSONArray("achievements");
                    total += rows.length();
                    for (int k = 0; k < rows.length(); k++) if (rows.getJSONObject(k).getBoolean("earned_on_ra")) earned++;
                }
                message = "RetroAchievements updated: " + earned + " of " + total + " earned. No unlocks were submitted.";
                ok = true;
            } catch (Exception failure) {
                message = failure instanceof IOException && failure.getMessage() != null && failure.getMessage().startsWith("RA connection")
                    ? failure.getMessage() : RaProgress.FAILED;
            }
            final boolean success = ok; final String text = message; final JSONObject library = result;
            main.post(() -> { if (done != null) done.finished(success, text, library); });
            if (success) {
                // Pictures after the progress is on screen; done runs again so badges appear.
                // Each batch of new pictures shows straight away (done runs again; screens re-read badges).
                Runnable shown = () -> main.post(() -> { if (done != null) done.finished(true, text, library); });
                try { fetchBadges(app, library, shown); } catch (Exception skipped) { /* fallback art stays */ }
                main.post(() -> { if (done != null) done.finished(true, text, library); });
            }
            running = false;
        });
    }

    /** One full refresh; the previous copy stays unless every request succeeds. Worker thread only. */
    static JSONObject sync(Context app) throws Exception {
        RaCredentials credentials = new RaCredentials(app);
        String[] connection = credentials.read();
        if (connection == null) throw new IOException("RA connection missing. Connect RetroAchievements first.");
        RaSets.Store store = new RaSets.Store(new File(app.getNoBackupFilesDir(), "achievements/ra"));
        JSONObject previous = null;
        try { previous = store.read(connection[0]); } catch (IOException damaged) { /* rebuilt below */ }
        long now = System.currentTimeMillis();
        JSONArray plan;
        long catalogAt;
        if (previous != null && now - previous.getLong("catalog_at") < WEEK && now >= previous.getLong("catalog_at")) {
            plan = previous.getJSONArray("sets");
            catalogAt = previous.getLong("catalog_at");
        } else {
            Map<Integer, JSONArray> lists = new HashMap<>();
            lists.put(WII, RaProgress.fetchGameList(connection[1], WII));
            pause();
            lists.put(GAMECUBE, RaProgress.fetchGameList(connection[1], GAMECUBE));
            plan = RaSets.plan(lists);
            catalogAt = now;
        }
        JSONArray sets = new JSONArray();
        for (int i = 0; i < plan.length(); i++) {
            JSONObject entry = plan.getJSONObject(i);
            pause();
            JSONObject progress = RaProgress.fetchGame(connection[0], connection[1], entry.getInt("id"));
            JSONArray rows = progress.getJSONArray("achievements");
            sets.put(new JSONObject().put("game", entry.getString("game")).put("id", entry.getInt("id"))
                .put("title", entry.getString("title")).put("kind", entry.getInt("kind")).put("achievements", rows));
        }
        JSONObject library = new JSONObject().put("schema", 2).put("account", connection[0]).put("synced_at", now)
            .put("catalog_at", catalogAt).put("sets", sets);
        synchronized (RaCredentials.class) {
            // A request finishing after disconnect/account change must not restore removed history.
            String[] current = credentials.read();
            if (current == null || !Arrays.equals(current, connection)) throw new IOException("RA connection changed during the refresh. Nothing was saved.");
            store.write(library);
        }
        prefs(app).edit().putLong("synced", now).apply();
        return library;
    }

    /** Public badge pictures: no credentials, official media host only, bounded. */
    static void fetchBadges(Context app, JSONObject library, Runnable progress) throws JSONException {
        List<String> badges = new ArrayList<>();
        JSONArray sets = library.getJSONArray("sets");
        for (int i = 0; i < sets.length(); i++) {
            JSONArray rows = sets.getJSONObject(i).getJSONArray("achievements");
            for (int k = 0; k < rows.length(); k++) { String badge = rows.getJSONObject(k).optString("badge"); if (!badge.isEmpty()) badges.add(badge); }
        }
        new RaBadges(new File(app.getNoBackupFilesDir(), "achievements/ra-badges")).fetchMissing(badges, progress);
    }

    /** A short gap between requests, as RA asks API users to keep load low. */
    private static void pause() throws InterruptedException { Thread.sleep(350); }

    /** What the device holds now: the connected account (or null), its cached library (or null) and any repair notice. */
    public static final class Loaded {
        public final String account, warning; public final JSONObject library;
        Loaded(String account, JSONObject library, String warning) { this.account = account; this.library = library; this.warning = warning; }
    }

    /** Reads the connection and cached library; never contacts RA. Worker thread only. */
    public static Loaded load(Context context) {
        Context app = context.getApplicationContext();
        String account = null, warning = "";
        try { String[] connection = new RaCredentials(app).read(); account = connection == null ? null : connection[0]; }
        catch (IOException broken) { warning = "Your RetroAchievements connection needs repair. Choose Change connection to enter it again."; }
        JSONObject library = null;
        if (account != null) {
            try { library = new RaSets.Store(new File(app.getNoBackupFilesDir(), "achievements/ra")).read(account); }
            catch (IOException damaged) { warning = "Saved RetroAchievements progress could not be read. Refresh to repair it."; }
        }
        return new Loaded(account, library, warning);
    }

    /** A new connection loads at once (not limited by the previous account's refresh times). */
    public static void resetSchedule(Context context) { prefs(context).edit().clear().apply(); }

    /** Disconnect: the connection, every cached library/history and the pictures. */
    public static void forget(Context context, String account) throws IOException {
        File root = new File(context.getNoBackupFilesDir(), "achievements/ra");
        if (account != null) { new RaSets.Store(root).remove(account); new RaProgress.Store(root).remove(account); }
        new RaBadges(new File(context.getNoBackupFilesDir(), "achievements/ra-badges")).clear();
        prefs(context).edit().clear().apply();
    }
}
