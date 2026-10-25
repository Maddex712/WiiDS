package com.driftdroid.android.achievements;

import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Which RetroAchievements sets belong to each WiiDS game, and the cached library format. Plain Java. */
public final class RaSets {
    private RaSets() {}
    static final int WII = 19, GAMECUBE = 16;
    private static final int LIMIT = 16 * 1024 * 1024;

    /** How a WiiDS game finds its RA sets by RA title (RA names subsets "Base [Subset - Name]" and hacks "~Hack~ ..."). */
    static final class Game {
        final String id, base; final int system; final String[] hackWords;
        Game(String id, int system, String base, String... hackWords) { this.id = id; this.system = system; this.base = base; this.hackWords = hackWords; }
        /** 0 = base set, 1 = subset, -1 = not this game's. */
        int match(String title) {
            if (base != null) {
                if (title.equals(base)) return 0;
                if (title.startsWith(base + " [Subset")) return 1;
                return -1;
            }
            if (!title.startsWith("~Hack~")) return -1;
            String lower = title.toLowerCase(Locale.ROOT);
            for (String word : hackWords) if (lower.contains(word)) return title.contains("[Subset") ? 1 : 0;
            return -1;
        }
    }
    static final Game[] GAMES = {
        new Game("mkw", WII, "Mario Kart Wii"),
        new Game("retro-rewind", WII, null, "retro rewind"),
        new Game("nsmbw", WII, "New Super Mario Bros. Wii"),
        new Game("msmbwii", WII, null, "just a little adventure", "msmbwii"),
        new Game("sunshine", GAMECUBE, "Super Mario Sunshine"),
        new Game("eclipse", GAMECUBE, null, "super mario eclipse"),
        new Game("galaxy", WII, "Super Mario Galaxy"),
        new Game("kaizo", WII, null, "kaizo mario galaxy"),
    };
    public static boolean known(String game) { for (Game g : GAMES) if (g.id.equals(game)) return true; return false; }

    /** The RA sets of every WiiDS game in [lists] (system -> game list), base set first, then subsets by title. */
    static JSONArray plan(Map<Integer, JSONArray> lists) throws JSONException {
        JSONArray sets = new JSONArray();
        for (Game game : GAMES) {
            JSONArray list = lists.get(game.system);
            if (list == null) continue;
            List<JSONObject> found = new ArrayList<>();
            for (int i = 0; i < list.length(); i++) {
                JSONObject entry = list.getJSONObject(i);
                int kind = game.match(entry.getString("title"));
                if (kind >= 0 && entry.optInt("achievements") > 0)
                    found.add(new JSONObject().put("game", game.id).put("id", entry.getInt("id")).put("title", entry.getString("title")).put("kind", kind));
            }
            found.sort((a, b) -> a.optInt("kind") != b.optInt("kind") ? Integer.compare(a.optInt("kind"), b.optInt("kind"))
                : a.optString("title").compareTo(b.optString("title")));
            for (JSONObject set : found) sets.put(set);
        }
        return sets;
    }

    // ------------------------------------------------------------------ storage
    public static final class Store {
        private final File root;
        public Store(File root) { this.root = root; }
        private File target(String account) throws IOException {
            RaProgress.account(account);
            try {
                byte[] hash = MessageDigest.getInstance("SHA-256").digest(account.toLowerCase(Locale.ROOT).getBytes(StandardCharsets.UTF_8));
                StringBuilder name = new StringBuilder();
                for (byte b : hash) name.append(String.format(Locale.ROOT, "%02x", b & 255));
                return new File(root, name + "-library.json");
            } catch (java.security.NoSuchAlgorithmException impossible) { throw new IOException("Storage unavailable"); }
        }
        public synchronized void write(JSONObject library) throws IOException {
            try {
                validate(library, library.getString("account"));
                if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Storage unavailable");
                Path temporary = Files.createTempFile(root.toPath(), ".library-", ".tmp");
                try {
                    try (FileOutputStream output = new FileOutputStream(temporary.toFile())) {
                        output.write(library.toString().getBytes(StandardCharsets.UTF_8)); output.getFD().sync();
                    }
                    Files.move(temporary, target(library.getString("account")).toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
                } finally { Files.deleteIfExists(temporary); }
            } catch (JSONException invalid) { throw new IOException("Invalid RA library"); }
        }
        public synchronized JSONObject read(String account) throws IOException {
            File file = target(account);
            if (!file.exists()) return null;
            if (file.length() > LIMIT) throw new IOException("RA library is too large");
            try {
                JSONObject library = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
                validate(library, account);
                return library;
            } catch (JSONException invalid) { throw new IOException("RA library is damaged"); }
        }
        public synchronized void remove(String account) throws IOException { Files.deleteIfExists(target(account).toPath()); }
        static void validate(JSONObject library, String account) throws IOException, JSONException {
            if (library.getInt("schema") != 2 || !library.getString("account").equalsIgnoreCase(account) ||
                library.getLong("synced_at") <= 0 || library.getLong("catalog_at") <= 0) throw new IOException("RA library identity mismatch");
            JSONArray sets = library.getJSONArray("sets");
            if (sets.length() > 200) throw new IOException("RA library is too large");
            Set<Integer> ids = new HashSet<>();
            for (int i = 0; i < sets.length(); i++) {
                JSONObject set = sets.getJSONObject(i);
                if (!known(set.getString("game")) || set.getInt("id") <= 0 || !ids.add(set.getInt("id")) ||
                    set.getString("title").length() > 512 || (set.getInt("kind") != 0 && set.getInt("kind") != 1))
                    throw new IOException("Invalid RA set");
                RaProgress.rows(set.getJSONArray("achievements"));
            }
        }
    }

}
