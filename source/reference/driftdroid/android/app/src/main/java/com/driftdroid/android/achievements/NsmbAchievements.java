package com.driftdroid.android.achievements;

import java.io.*;
import java.nio.channels.FileLock;
import java.nio.file.*;
import java.util.*;

/** Versioned local NSMB rules and an atomic, process-locked ledger. No game save writes. */
public final class NsmbAchievements {
    private NsmbAchievements() {}
    public static final String[][] DEFINITIONS = {
        {"nsmb.first_exit.v1", "A Fresh Exit", "Record a previously uncleared course exit during solo play."},
        {"nsmb.first_steps.v1", "First Steps", "Record a previously uncleared exit in World 1-1 during solo play."},
        {"nsmb.star_coin.v1", "Star Coin Found", "Pick up a Star Coin during solo play."},
        {"nsmb.three_stars.v1", "Three of a Kind", "Pick up a missing Star Coin so all three are held in the current course."}
    };
    public static List<String> evaluate(int type, int slot, int world, int course, int bits) {
        List<String> ids = new ArrayList<>();
        if (slot < 0 || slot > 2 || world < 0 || world > 8 || course < 0 || course >= 38) return ids;
        if (type == 1 && bits > 0 && bits <= 7) {
            ids.add(DEFINITIONS[2][0]);
            if (bits == 7) ids.add(DEFINITIONS[3][0]);
        } else if (type == 2 && bits > 0 && (bits & ~0x30) == 0) {
            ids.add(DEFINITIONS[0][0]);
            if (world == 0 && course == 0) ids.add(DEFINITIONS[1][0]);
        }
        return ids;
    }
    public static String title(String id) {
        for (String[] row : DEFINITIONS) if (row[0].equals(id)) return row[1];
        return id;
    }
    public static String profile(long nandHash, int slot) {
        if (nandHash == 0 || slot < 0 || slot > 2) throw new IllegalArgumentException("Invalid achievement profile");
        return String.format(Locale.ROOT, "nsmbw.%016x.slot%d", nandHash, slot);
    }
    public static final class Ledger {
        private final File root;
        public Ledger(File root) { this.root = root; }
        private File file(String profile) throws IOException {
            if (!profile.matches("nsmbw\\.[0-9a-f]{16}\\.slot[0-2]")) throw new IOException("Invalid profile");
            return new File(root, profile + ".properties");
        }
        private void prepare() throws IOException {
            if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Achievement storage unavailable");
        }
        private Properties read(File target) throws IOException {
            Properties data = new Properties();
            if (target.isFile()) {
                if (target.length() > 65536) throw new IOException("Achievement ledger is too large");
                try (InputStream stream = new FileInputStream(target)) { data.load(stream); }
                catch (IllegalArgumentException error) { throw new IOException("Achievement ledger is damaged"); }
                if (!"1".equals(data.getProperty("schema"))) throw new IOException("Unsupported achievement ledger");
                for (String key : data.stringPropertyNames()) if (!key.equals("schema")) {
                    boolean known = false;
                    for (String[] row : DEFINITIONS) known |= row[0].equals(key);
                    try { if (!known || Long.parseLong(data.getProperty(key)) <= 0) throw new NumberFormatException(); }
                    catch (NumberFormatException error) { throw new IOException("Achievement ledger is damaged"); }
                }
            }
            data.setProperty("schema", "1");
            return data;
        }
        public synchronized Map<String, Long> progress(String profile) throws IOException {
            synchronized (Ledger.class) {
                prepare();
                try (RandomAccessFile lock = new RandomAccessFile(new File(root, ".ledger.lock"), "rw");
                     FileLock held = lock.getChannel().lock()) {
                    Properties data = read(file(profile));
                    Map<String, Long> result = new LinkedHashMap<>();
                    for (String[] row : DEFINITIONS) if (data.containsKey(row[0]))
                        result.put(row[0], Long.parseLong(data.getProperty(row[0])));
                    return result;
                }
            }
        }
        public List<String> record(String profile, List<String> ids, long time) throws IOException {
            if (time <= 0) throw new IOException("Invalid unlock time");
            synchronized (Ledger.class) {
                prepare();
                try (RandomAccessFile lock = new RandomAccessFile(new File(root, ".ledger.lock"), "rw");
                     FileLock held = lock.getChannel().lock()) {
                    File target = file(profile);
                    Properties data = read(target);
                    List<String> added = new ArrayList<>();
                    for (String id : ids) {
                        boolean known = false;
                        for (String[] row : DEFINITIONS) known |= row[0].equals(id);
                        if (!known) throw new IOException("Unknown achievement rule");
                        if (!data.containsKey(id)) { data.setProperty(id, Long.toString(time)); added.add(id); }
                    }
                    if (!added.isEmpty()) {
                        Path temporary = Files.createTempFile(root.toPath(), ".ledger-", ".tmp");
                        try {
                            try (FileOutputStream output = new FileOutputStream(temporary.toFile())) {
                                data.store(output, "WiiDS NSMB achievements"); output.getFD().sync();
                            }
                            Files.move(temporary, target.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
                        } finally { Files.deleteIfExists(temporary); }
                    }
                    return added;
                }
            }
        }
        public List<String> profiles() throws IOException {
            prepare();
            String[] names = root.list((dir, name) -> name.matches("nsmbw\\.[0-9a-f]{16}\\.slot[0-2]\\.properties"));
            if (names == null) throw new IOException("Achievement storage unavailable");
            List<String> profiles = new ArrayList<>();
            for (String name : names) profiles.add(name.substring(0, name.length() - 11));
            Collections.sort(profiles);
            return profiles;
        }
    }
}
