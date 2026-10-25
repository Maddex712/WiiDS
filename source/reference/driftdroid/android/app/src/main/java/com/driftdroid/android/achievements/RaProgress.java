package com.driftdroid.android.achievements;

import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.text.*;
import java.util.*;

/** Read-only NSMB history import. No emulator login, evaluation or unlock submission. */
public final class RaProgress {
    public static final int NSMB_SET = 95;
    private static final int LIMIT = 8 * 1024 * 1024;
    private RaProgress() {}
    public static String account(String value) throws IOException {
        if (value == null || !value.equals(value.trim()) || !value.matches("[A-Za-z0-9_\\-]{2,128}"))
            throw new IOException("Enter your RA username or account ULID.");
        return value;
    }
    /** RA badge identities are short numeric names (the API's BadgeName, e.g. "85541"). */
    public static boolean badgeName(String value) {
        return value != null && value.matches("[0-9]{1,12}");
    }
    /** The public badge picture on RA's media host. Never carries account credentials. */
    public static String badgeUrl(String name) throws IOException {
        if (!badgeName(name)) throw new IOException("Invalid RA badge");
        return "https://media.retroachievements.org/Badge/" + name + ".png";
    }
    private static int integer(Object value) throws IOException {
        if (!(value instanceof Integer || value instanceof Long) || ((Number)value).longValue() > Integer.MAX_VALUE ||
            ((Number)value).longValue() < Integer.MIN_VALUE)
            throw new IOException("Invalid RA number");
        return ((Number)value).intValue();
    }
    private static String text(JSONObject entry, String key, int limit) throws JSONException, IOException {
        Object value = entry.get(key);
        if (!(value instanceof String) || ((String)value).length() > limit) throw new IOException("Invalid RA text");
        return (String)value;
    }
    private static String date(JSONObject entry, String key) throws JSONException, IOException {
        Object value = entry.opt(key);
        if (value == null || value == JSONObject.NULL || "".equals(value)) return "";
        if (!(value instanceof String) || !((String)value).matches("[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}"))
            throw new IOException("Invalid RA date");
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);
        format.setTimeZone(TimeZone.getTimeZone("UTC")); format.setLenient(false);
        try { format.parse((String)value); } catch (ParseException invalid) { throw new IOException("Invalid RA date"); }
        return (String)value;
    }
    public static JSONObject normalize(JSONObject payload, String account, long now) throws IOException {
        return normalize(payload, account, now, NSMB_SET);
    }
    /** One RA game's achievements and this account's completions, validated; [game] is the requested RA game ID. */
    public static JSONObject normalize(JSONObject payload, String account, long now, int game) throws IOException {
        account(account);
        try {
            if (game <= 0 || integer(payload.get("ID")) != game) throw new IOException("RA returned a different game.");
            JSONObject source = payload.getJSONObject("Achievements");
            if (source.length() > 5000) throw new IOException("RA set is too large.");
            JSONArray rows = new JSONArray();
            List<String> keys = new ArrayList<>();
            for (Iterator<String> it = source.keys(); it.hasNext();) keys.add(it.next());
            for (String key : keys) {
                if (!key.matches("[1-9][0-9]{0,9}")) throw new IOException("Invalid RA achievement ID.");
                JSONObject entry = source.getJSONObject(key);
                int id = integer(entry.get("ID"));
                if (id <= 0 || !Integer.toString(id).equals(key)) throw new IOException("RA achievement ID mismatch.");
                int points = integer(entry.get("Points"));
                if (points < 0 || points > 1000) throw new IOException("Invalid RA points.");
                String casual = date(entry, "DateEarned"), hardcore = date(entry, "DateEarnedHardcore");
                JSONObject row = new JSONObject().put("id", id).put("title", text(entry, "Title", 512))
                    .put("description", text(entry, "Description", 4096)).put("author", entry.optString("Author", ""))
                    .put("points_on_ra", points).put("casual_date", casual).put("hardcore_date", hardcore)
                    .put("earned_on_ra", !casual.isEmpty() || !hardcore.isEmpty());
                // Optional presentation fields: a missing or unexpected value only costs the picture.
                Object badge = entry.opt("BadgeName");
                if (badge instanceof String && badgeName((String)badge)) row.put("badge", badge);
                Object order = entry.opt("DisplayOrder");
                if ((order instanceof Integer || order instanceof Long) && ((Number)order).longValue() >= 0
                    && ((Number)order).longValue() <= 1_000_000) row.put("order", ((Number)order).intValue());
                rows.put(row);
            }
            return new JSONObject().put("schema", 1).put("game_id", game).put("account", account)
                .put("synced_at", now).put("achievements", rows);
        } catch (JSONException malformed) { throw new IOException("RA returned incomplete progress information."); }
    }
    public static JSONObject fetch(String account, String key) throws IOException { return fetchGame(account, key, NSMB_SET); }

    /** One RA game's achievements with this account's completions (official Web API). */
    public static JSONObject fetchGame(String account, String key, int game) throws IOException {
        account(account);
        if (game <= 0) throw new IOException("Invalid RA game.");
        String body = request(key, "API_GetGameInfoAndUserProgress.php", "&u=" + encode(account) + "&g=" + game);
        try { return normalize(new JSONObject(body), account, System.currentTimeMillis(), game); }
        catch (JSONException | IOException failure) { throw new IOException(FAILED); }
    }

    /** Every RA game with achievements on one system: [{id, title, achievements}]. Cache it; the reply is large. */
    public static JSONArray fetchGameList(String key, int system) throws IOException {
        if (system <= 0 || system > 1000) throw new IOException("Invalid RA system.");
        String body = request(key, "API_GetGameList.php", "&i=" + system + "&f=1");
        try {
            JSONArray source = new JSONArray(body), games = new JSONArray();
            if (source.length() > 50000) throw new IOException();
            for (int i = 0; i < source.length(); i++) {
                JSONObject entry = source.getJSONObject(i);
                int id = integer(entry.get("ID"));
                if (id <= 0) throw new IOException();
                games.put(new JSONObject().put("id", id).put("title", text(entry, "Title", 512))
                    .put("achievements", entry.opt("NumAchievements") instanceof Number ? entry.optInt("NumAchievements") : 0));
            }
            return games;
        } catch (JSONException | IOException failure) { throw new IOException(FAILED); }
    }

    static final String FAILED = "Could not import RA progress. Check your account, Web API key and connection. Previous progress was kept.";
    private static String encode(String value) throws IOException {
        try { return URLEncoder.encode(value, "UTF-8"); } catch (UnsupportedEncodingException impossible) { throw new IOException(FAILED); }
    }

    /** HTTPS GET to the official Web API; no redirects, bounded size and time, the key never in an error. */
    private static String request(String key, String endpoint, String query) throws IOException {
        if (key == null || key.trim().isEmpty() || key.length() > 1024) throw new IOException("Enter your RA Web API key.");
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection)new URL("https://retroachievements.org/API/" + endpoint + "?y=" + encode(key) + query).openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(20000); connection.setReadTimeout(20000);
            connection.setRequestProperty("User-Agent", "WiiDS-ProgressMirror/0.1");
            connection.setRequestProperty("Accept", "application/json");
            if (connection.getResponseCode() != 200) throw new IOException();
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[8192]; int n;
                long deadline = System.nanoTime() + 30_000_000_000L;
                while ((n = input.read(buffer)) != -1) {
                    if (bytes.size() + n > LIMIT || System.nanoTime() > deadline) throw new IOException();
                    bytes.write(buffer, 0, n);
                }
            }
            return new String(bytes.toByteArray(), StandardCharsets.UTF_8);
        } catch (Exception failure) {
            // Exceptions from HTTPS often embed the credential-bearing URL. Never expose the cause.
            throw new IOException(FAILED);
        } finally { if (connection != null) connection.disconnect(); }
    }
    public static final class Store {
        private final File root;
        public Store(File root) { this.root = root; }
        private File target(String account) throws IOException {
            account(account);
            try {
                byte[] hash = MessageDigest.getInstance("SHA-256").digest(account.toLowerCase(Locale.ROOT).getBytes(StandardCharsets.UTF_8));
                StringBuilder name = new StringBuilder();
                for (byte b : hash) name.append(String.format(Locale.ROOT, "%02x", b & 255));
                return new File(root, name + "-95.json");
            } catch (java.security.NoSuchAlgorithmException impossible) { throw new IOException("Storage unavailable"); }
        }
        public synchronized void write(JSONObject snapshot) throws IOException {
            try {
                validate(snapshot, snapshot.getString("account"));
                File target = target(snapshot.getString("account"));
                if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Storage unavailable");
                Path temporary = Files.createTempFile(root.toPath(), ".ra-", ".tmp");
                try {
                    try (FileOutputStream output = new FileOutputStream(temporary.toFile())) {
                        output.write(snapshot.toString().getBytes(StandardCharsets.UTF_8)); output.getFD().sync();
                    }
                    Files.move(temporary, target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
                } finally { Files.deleteIfExists(temporary); }
            } catch (JSONException invalid) { throw new IOException("Invalid RA snapshot"); }
        }
        public synchronized JSONObject read(String account) throws IOException {
            File file = target(account);
            if (!file.exists()) return null;
            if (file.length() > LIMIT) throw new IOException("RA cache is too large");
            try {
                JSONObject snapshot = new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
                validate(snapshot, account);
                return snapshot;
            } catch (JSONException invalid) { throw new IOException("RA cache is damaged"); }
        }
        public synchronized void remove(String account) throws IOException { Files.deleteIfExists(target(account).toPath()); }
        private void validate(JSONObject snapshot, String account) throws IOException, JSONException {
            if (integer(snapshot.get("schema")) != 1 || integer(snapshot.get("game_id")) != NSMB_SET ||
                !snapshot.getString("account").equalsIgnoreCase(account) || snapshot.getLong("synced_at") <= 0)
                throw new IOException("RA cache identity mismatch");
            rows(snapshot.getJSONArray("achievements"));
        }
    }

    /** Validates stored achievement rows (also used by RaLibrary). */
    static void rows(JSONArray rows) throws IOException, JSONException {
        {
            if (rows.length() > 5000) throw new IOException("RA cache is too large");
            Set<Integer> ids = new HashSet<>();
            for (int i=0; i<rows.length(); i++) {
                JSONObject row = rows.getJSONObject(i);
                int id = integer(row.get("id")), points = integer(row.get("points_on_ra"));
                if (id <= 0 || !ids.add(id) || points < 0 || points > 1000) throw new IOException("Invalid RA cache entry");
                text(row, "title", 512); text(row, "description", 4096); text(row, "author", 512);
                String casual = date(row, "casual_date"), hardcore = date(row, "hardcore_date");
                if (!(row.get("earned_on_ra") instanceof Boolean) || row.getBoolean("earned_on_ra") != (!casual.isEmpty() || !hardcore.isEmpty()))
                    throw new IOException("Invalid RA completion");
                // Optional since badge support; caches written before it have neither field.
                if (row.has("badge") && !(row.get("badge") instanceof String && badgeName(row.getString("badge"))))
                    throw new IOException("Invalid RA badge");
                if (row.has("order")) { int order = integer(row.get("order")); if (order < 0 || order > 1_000_000) throw new IOException("Invalid RA order"); }
            }
        }
    }
}
