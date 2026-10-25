package com.driftdroid.android.achievements;

import org.json.*;
import java.text.*;
import java.util.*;

/**
 * One presentation model for the library preview, the browser and the in-game popup: a WiiDS
 * game's RetroAchievements sets with this account's completions, from the cached RA library.
 * Built on a worker; snapshots are immutable once published. No network or file access here.
 */
public final class AchievementCatalog {
    private AchievementCatalog() {}
    public enum Source { WIIDS, RETROACHIEVEMENTS }

    public static final class Item {
        public final String id, title, description, badge, setTitle;
        public final Source source;
        public final boolean subset;
        /** Earned time in ms (RA's UTC date), 0 while locked. */
        public final long earnedAt;
        public final int raPoints;
        public final String raAuthor, raCasualDate, raHardcoreDate;
        Item(String id, String title, String description, String badge, Source source, String setTitle, boolean subset,
             long earnedAt, int raPoints, String raAuthor, String raCasualDate, String raHardcoreDate) {
            this.id = id; this.title = title; this.description = description; this.badge = badge; this.source = source;
            this.setTitle = setTitle; this.subset = subset; this.earnedAt = earnedAt; this.raPoints = raPoints;
            this.raAuthor = raAuthor; this.raCasualDate = raCasualDate; this.raHardcoreDate = raHardcoreDate;
        }
        public boolean earned() { return earnedAt > 0; }
        public boolean hardcore() { return !raHardcoreDate.isEmpty(); }
    }

    /** One RA set of the game (its base set or a subset/hack set). */
    public static final class Set {
        public final int id; public final String title; public final boolean subset; public final int earned, total, points;
        Set(int id, String title, boolean subset, int earned, int total, int points) {
            this.id = id; this.title = title; this.subset = subset; this.earned = earned; this.total = total; this.points = points;
        }
        /** "Hoverless" for "Super Mario Sunshine [Subset - Hoverless]"; "Base set" for the main set. */
        public String label() {
            int at = title.indexOf("[Subset - ");
            if (at >= 0) return title.substring(at + 10).replace("]", "").trim();
            return subset ? title : "Base set";
        }
    }

    public static final class Snapshot {
        public final String gameId, account;
        public final boolean connected;
        public final long syncedAt;
        public final List<Set> sets;
        public final List<Item> items, recent, upcoming;
        public final int earned;
        Snapshot(String gameId, boolean connected, String account, long syncedAt, List<Set> sets, List<Item> items) {
            this.gameId = gameId; this.connected = connected; this.account = account; this.syncedAt = syncedAt;
            this.sets = Collections.unmodifiableList(new ArrayList<>(sets));
            this.items = Collections.unmodifiableList(new ArrayList<>(items));
            List<Item> got = new ArrayList<>(), next = new ArrayList<>();
            for (Item item : items) (item.earned() ? got : next).add(item);
            // Newest first; equal times keep the sets' own order (stable sort).
            got.sort((a, b) -> Long.compare(b.earnedAt, a.earnedAt));
            this.recent = Collections.unmodifiableList(got);
            this.upcoming = Collections.unmodifiableList(next);
            this.earned = got.size();
        }
        public int total() { return items.size(); }
        /** Imported and RA has at least one set for this game. */
        public boolean hasSets() { return !sets.isEmpty(); }
        public Item find(String id) {
            for (Item item : items) if (item.id.equals(id)) return item;
            return null;
        }
    }

    /**
     * [gameId]'s sets from the cached [library] (null before the first import). Base set first,
     * then subsets; achievements in RA's display order, then by ID.
     */
    public static Snapshot of(JSONObject library, String gameId, boolean connected) {
        List<Set> sets = new ArrayList<>();
        List<Item> items = new ArrayList<>();
        if (library == null) return new Snapshot(gameId, connected, null, 0, sets, items);
        JSONArray source = library.optJSONArray("sets");
        for (int i = 0; source != null && i < source.length(); i++) {
            JSONObject set = source.optJSONObject(i);
            if (set == null || !gameId.equals(set.optString("game"))) continue;
            boolean subset = set.optInt("kind") == 1;
            List<Item> rows = rows(set.optJSONArray("achievements"), set.optString("title"), subset);
            int got = 0, points = 0;
            for (Item item : rows) { if (item.earned()) got++; points += item.raPoints; }
            sets.add(new Set(set.optInt("id"), set.optString("title"), subset, got, rows.size(), points));
            items.addAll(rows);
        }
        return new Snapshot(gameId, connected, library.optString("account", null), library.optLong("synced_at"), sets, items);
    }

    static List<Item> rows(JSONArray source, String setTitle, boolean subset) {
        List<Item> rows = new ArrayList<>();
        if (source == null) return rows;
        List<JSONObject> ordered = new ArrayList<>();
        for (int i = 0; i < source.length(); i++) if (source.optJSONObject(i) != null) ordered.add(source.optJSONObject(i));
        ordered.sort((a, b) -> a.optInt("order", Integer.MAX_VALUE) != b.optInt("order", Integer.MAX_VALUE)
            ? Integer.compare(a.optInt("order", Integer.MAX_VALUE), b.optInt("order", Integer.MAX_VALUE))
            : Integer.compare(a.optInt("id"), b.optInt("id")));
        for (JSONObject row : ordered) {
            String casual = row.optString("casual_date"), hardcore = row.optString("hardcore_date");
            long earned = Math.max(utc(casual), utc(hardcore));
            if (row.optBoolean("earned_on_ra") && earned <= 0) earned = 1;
            String badge = row.optString("badge");
            rows.add(new Item("ra." + row.optInt("id"), row.optString("title"), row.optString("description"),
                badge.isEmpty() ? "" : "ra:" + badge, Source.RETROACHIEVEMENTS, setTitle, subset,
                row.optBoolean("earned_on_ra") ? earned : 0, row.optInt("points_on_ra"), row.optString("author"), casual, hardcore));
        }
        return rows;
    }

    /** The original single-set NSMB history snapshot (schema 1) as rows. */
    public static List<Item> retroAchievements(JSONObject snapshot) {
        return snapshot == null ? new ArrayList<>() : rows(snapshot.optJSONArray("achievements"), "New Super Mario Bros. Wii", false);
    }

    private static long utc(String value) {
        if (value == null || value.isEmpty()) return 0;
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        format.setLenient(false);
        try { return format.parse(value).getTime(); } catch (ParseException invalid) { return 0; }
    }

    /** A WiiDS rule as a just-earned item (the reusable in-game popup). Null for an unknown ID. */
    public static Item unlocked(String id, long earnedAt) {
        for (String[] row : NsmbAchievements.DEFINITIONS)
            if (row[0].equals(id)) return new Item(row[0], row[1], row[2], row[0], Source.WIIDS, "", false, Math.max(1, earnedAt), 0, "", "", "");
        return null;
    }

    /** A locked, picture-less stand-in for designed empty states (drawn as a WiiDS trophy). */
    public static Item placeholder() { return new Item("none", "", "", "none", Source.WIIDS, "", false, 0, 0, "", "", ""); }

    /**
     * Fast case scrolling: only the most recent request for the current key may publish, so a
     * late result for a game scrolled past never replaces the selected game's cards.
     */
    public static final class Latest {
        private long generation;
        private String key;
        public synchronized long begin(String key) { this.key = key; return ++generation; }
        public synchronized boolean accept(long token, String key) {
            return token == generation && Objects.equals(this.key, key);
        }
    }

    /**
     * Popups for newly recorded unlocks, one at a time in their recorded order. An ID is shown
     * at most once per game session, whatever the source of a repeated report.
     */
    public static final class UnlockQueue {
        private final ArrayDeque<String> pending = new ArrayDeque<>();
        private final java.util.Set<String> seen = new HashSet<>();
        public synchronized boolean offer(String id) {
            if (id == null || !seen.add(id)) return false;
            pending.addLast(id);
            return true;
        }
        public synchronized String next() { return pending.pollFirst(); }
        public synchronized boolean isEmpty() { return pending.isEmpty(); }
        public synchronized int size() { return pending.size(); }
        /** Activity destroyed: nothing queued may appear over a later game. */
        public synchronized void clear() { pending.clear(); }
    }
}
