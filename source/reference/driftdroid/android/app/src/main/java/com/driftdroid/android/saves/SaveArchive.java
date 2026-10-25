package com.driftdroid.android.saves;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

/** WiiDS save-only archives. No Android, ROM, settings, or account dependency. */
public final class SaveArchive {
    private SaveArchive() {}
    public static final long MAX_BYTES = 128L << 20;
    private static final int MAX_FILES = 2048;
    private static final int MAX_MANIFEST = 1 << 20;
    private static final String MANIFEST = "wiids-save.tsv";

    public static final class Profile {
        public final String id;
        public final LinkedHashMap<String, File> roots;
        public Profile(String id, Map<String, File> roots) throws IOException {
            if (!id.matches("[a-z0-9-]{1,48}") || roots.isEmpty() || roots.size() > 32)
                throw new IOException("Invalid save profile.");
            this.id = id;
            this.roots = new LinkedHashMap<>(roots);
            Set<String> paths = new HashSet<>();
            for (Map.Entry<String, File> root : this.roots.entrySet()) {
                if (!root.getKey().matches("[a-z0-9-]{1,48}")) throw new IOException("Invalid save root.");
                safe(root.getValue());
                String path = root.getValue().getCanonicalPath();
                for (String other : paths)
                    if (path.equals(other) || path.startsWith(other + File.separator) || other.startsWith(path + File.separator))
                        throw new IOException("Overlapping save roots.");
                paths.add(path);
            }
        }
    }

    public static final class Inventory {
        public final String profile;
        public final long created;
        public final Map<String, Boolean> roots = new LinkedHashMap<>();
        public final Map<String, Entry> files = new TreeMap<>();
        Inventory(String profile, long created) { this.profile = profile; this.created = created; }
        public long bytes() { long n = 0; for (Entry e : files.values()) n += e.size; return n; }
    }
    public static final class Entry {
        public final long size;
        public final String hash;
        Entry(long size, String hash) { this.size = size; this.hash = hash; }
    }

    /** Refuse links in any component, including a missing destination's existing ancestors. */
    public static void safe(File file) throws IOException {
        Path path = file.toPath().toAbsolutePath().normalize();
        for (Path p = path; p != null; p = p.getParent())
            if (Files.isSymbolicLink(p)) throw new IOException("A save path contains a symbolic link.");
    }
    private static boolean exists(File f) { return Files.exists(f.toPath(), LinkOption.NOFOLLOW_LINKS); }
    private static File child(File base, String path) throws IOException {
        validPath(path);
        File result = new File(base, path);
        safe(result);
        if (!result.getCanonicalPath().startsWith(base.getCanonicalPath() + File.separator))
            throw new IOException("Save path escapes its folder.");
        return result;
    }
    private static void validPath(String name) throws IOException {
        if (name.length() > 1024 || name.startsWith("/") || name.contains("\\") || name.contains(":") || name.split("/", -1).length > 24)
            throw new IOException("Unsafe path in save backup.");
        for (String part : name.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Unsafe path in save backup.");
            for (char c : part.toCharArray()) if (c < 32 || c == 127) throw new IOException("Unsafe save filename.");
        }
    }
    private static MessageDigest digest() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (Exception e) { throw new AssertionError(e); }
    }
    private static String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder();
        for (byte b : bytes) s.append(String.format(Locale.ROOT, "%02x", b & 255));
        return s.toString();
    }
    private static Entry transfer(InputStream in, OutputStream out, long limit) throws IOException {
        MessageDigest hash = digest(); long size = 0; byte[] buffer = new byte[65536]; int n;
        while ((n = in.read(buffer)) != -1) {
            size += n; if (size > limit) throw new IOException("Save backup exceeds its size limit.");
            hash.update(buffer, 0, n); if (out != null) out.write(buffer, 0, n);
        }
        return new Entry(size, hex(hash.digest()));
    }
    private static void match(Entry expected, Entry actual) throws IOException {
        if (expected.size != actual.size || !expected.hash.equals(actual.hash))
            throw new IOException("A save file failed its size or SHA-256 check.");
    }
    public static Inventory inventory(Profile profile) throws IOException {
        Inventory result = new Inventory(profile.id, System.currentTimeMillis());
        for (Map.Entry<String, File> root : profile.roots.entrySet()) {
            safe(root.getValue());
            result.roots.put(root.getKey(), exists(root.getValue()));
            if (exists(root.getValue())) scan(root.getValue(), root.getKey(), result);
        }
        return result;
    }
    private static void scan(File folder, String prefix, Inventory result) throws IOException {
        safe(folder);
        if (!folder.isDirectory()) throw new IOException("A save folder is not a directory.");
        File[] files = folder.listFiles(); if (files == null) throw new IOException("Cannot read the save folder.");
        Arrays.sort(files, Comparator.comparing(File::getName));
        for (File f : files) {
            safe(f); String name = prefix + "/" + f.getName(); validPath(name);
            if (f.isDirectory()) scan(f, name, result);
            else if (f.isFile()) {
                if (result.files.size() >= MAX_FILES) throw new IOException("Too many save files.");
                try (InputStream in = new FileInputStream(f)) { result.files.put(name, transfer(in, null, MAX_BYTES - result.bytes())); }
            } else throw new IOException("Unsupported object in save folder.");
        }
    }
    private static byte[] manifest(Inventory i) {
        StringBuilder text = new StringBuilder("WIIDS-SAVE\t1\nprofile\t" + i.profile + "\ncreated\t" + i.created + "\n");
        for (Map.Entry<String, Boolean> r : i.roots.entrySet()) text.append("root\t").append(r.getKey()).append('\t').append(r.getValue() ? 1 : 0).append('\n');
        for (Map.Entry<String, Entry> f : i.files.entrySet()) text.append("file\t").append(f.getKey()).append('\t').append(f.getValue().size).append('\t').append(f.getValue().hash).append('\n');
        return text.toString().getBytes(StandardCharsets.UTF_8);
    }

    /** Publish only a read-back-verified archive, never replacing an earlier backup. */
    public static Inventory backup(Profile profile, File destination, boolean allowEmpty) throws IOException {
        safe(destination);
        if (exists(destination)) throw new IOException("Backup already exists.");
        Inventory i = inventory(profile);
        if (!allowEmpty && i.files.isEmpty()) throw new IOException("No save yet. Save in the game first.");
        mkdir(destination.getParentFile());
        File tmp = new File(destination.getParentFile(), ".backup-" + UUID.randomUUID() + ".tmp");
        try {
            try (FileOutputStream file = new FileOutputStream(tmp); ZipOutputStream zip = new ZipOutputStream(file)) {
                zip.putNextEntry(new ZipEntry(MANIFEST)); zip.write(manifest(i)); zip.closeEntry();
                for (Map.Entry<String, Entry> e : i.files.entrySet()) {
                    String name = e.getKey(); int slash = name.indexOf('/');
                    File source = child(profile.roots.get(name.substring(0, slash)), name.substring(slash + 1));
                    zip.putNextEntry(new ZipEntry("data/" + name));
                    try (InputStream in = new FileInputStream(source)) { match(e.getValue(), transfer(in, zip, e.getValue().size)); }
                    zip.closeEntry();
                }
                zip.finish(); file.getFD().sync();
            }
            verify(profile, tmp, null);
            // Detect a writer that replaced or added files during the snapshot.
            Inventory after = inventory(profile);
            if (!Arrays.equals(manifestWithoutTime(i), manifestWithoutTime(after))) throw new IOException("Saves changed during backup. Close the game and try again.");
            move(tmp, destination);
            return i;
        } finally { Files.deleteIfExists(tmp.toPath()); }
    }
    private static byte[] manifestWithoutTime(Inventory i) {
        return new String(manifest(i), StandardCharsets.UTF_8).replace("created\t" + i.created, "created\t0").getBytes(StandardCharsets.UTF_8);
    }

    /** Read only our own bounded, profile-bound archive; optionally extract into a private stage. */
    public static Inventory verify(Profile profile, File archive, File extract) throws IOException {
        safe(archive);
        if (!archive.isFile() || archive.length() > MAX_BYTES + (8L << 20)) throw new IOException("Invalid save backup size.");
        try (ZipFile zip = new ZipFile(archive)) {
            ZipEntry m = zip.getEntry(MANIFEST); if (m == null || m.isDirectory()) throw new IOException("Not a WiiDS save backup.");
            ByteArrayOutputStream text = new ByteArrayOutputStream();
            try (InputStream in = zip.getInputStream(m)) { transfer(in, text, MAX_MANIFEST); }
            Inventory i = parse(profile, text.toString("UTF-8"));
            Set<String> seen = new HashSet<>(); int count = 0;
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry e = entries.nextElement(); String name = e.getName();
                if (++count > MAX_FILES + 1 || !seen.add(name) || e.isDirectory()) throw new IOException("Duplicate or invalid backup entry.");
                if (name.equals(MANIFEST)) continue;
                if (!name.startsWith("data/")) throw new IOException("Unexpected backup content.");
                String relative = name.substring(5); Entry expected = i.files.get(relative);
                if (expected == null) throw new IOException("Unlisted save file.");
                File target = extract == null ? null : child(extract, relative);
                if (target != null) mkdir(target.getParentFile());
                try (InputStream in = zip.getInputStream(e)) {
                    if (target == null) match(expected, transfer(in, null, expected.size));
                    else try (FileOutputStream out = new FileOutputStream(target)) {
                        match(expected, transfer(in, out, expected.size)); out.getFD().sync();
                    }
                }
            }
            if (count != i.files.size() + 1) throw new IOException("Backup is missing save files.");
            return i;
        } catch (IllegalArgumentException e) { throw new IOException("Invalid save backup.", e); }
    }
    private static Inventory parse(Profile profile, String text) throws IOException {
        try {
            String[] lines = text.split("\n");
            if (lines.length < 4 || !lines[0].equals("WIIDS-SAVE\t1") || !lines[1].equals("profile\t" + profile.id)
                    || !lines[2].startsWith("created\t")) throw new IOException("This backup belongs to a different game or format.");
            Inventory i = new Inventory(profile.id, Long.parseLong(lines[2].substring(8)));
            for (int n = 3; n < lines.length; n++) {
                String[] p = lines[n].split("\t", -1);
                if (p.length == 3 && p[0].equals("root") && profile.roots.containsKey(p[1]) && (p[2].equals("0") || p[2].equals("1"))) {
                    if (i.roots.put(p[1], p[2].equals("1")) != null) throw new IOException("Duplicate save root.");
                } else if (p.length == 4 && p[0].equals("file")) {
                    validPath(p[1]); int slash = p[1].indexOf('/');
                    if (slash < 1 || !Boolean.TRUE.equals(i.roots.get(p[1].substring(0, slash)))) throw new IOException("Invalid save root.");
                    long size = Long.parseLong(p[2]);
                    if (size < 0 || size > MAX_BYTES || !p[3].matches("[0-9a-f]{64}") || i.files.size() >= MAX_FILES ||
                            i.files.put(p[1], new Entry(size, p[3])) != null || i.bytes() > MAX_BYTES) throw new IOException("Invalid save inventory.");
                } else throw new IOException("Invalid save manifest.");
            }
            if (!i.roots.keySet().equals(profile.roots.keySet())) throw new IOException("Backup profile layout differs.");
            return i;
        } catch (NumberFormatException e) { throw new IOException("Invalid save manifest number.", e); }
    }
    public static void mkdir(File folder) throws IOException {
        safe(folder); if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("Cannot create save backup folder.");
    }
    private static void move(File from, File to) throws IOException {
        safe(from); safe(to);
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE);
    }
    public static void copy(InputStream in, OutputStream out) throws IOException { transfer(in, out, MAX_BYTES + (8L << 20)); }
    private static void erase(File file) throws IOException {
        safe(file); if (!exists(file)) return;
        if (file.isDirectory()) {
            File[] files = file.listFiles(); if (files == null) throw new IOException("Cannot read restore staging folder.");
            for (File f : files) erase(f);
        }
        Files.delete(file.toPath());
    }
    private static File adjacent(File target, String token, String suffix) { return new File(target.getParentFile(), ".wiids-" + token + "-" + target.getName() + suffix); }
    public static boolean pending(File work) { return new File(work, "restore.properties").exists(); }

    /** Journal is durable before the first rename. Recovery always rolls an interrupted restore back. */
    public static void recover(Profile profile, File work) throws IOException {
        File journal = new File(work, "restore.properties"); safe(journal); if (!journal.exists()) return;
        Properties p = new Properties(); try (InputStream in = new FileInputStream(journal)) { p.load(in); }
        String token = p.getProperty("token", "");
        if (!profile.id.equals(p.getProperty("profile")) || !token.matches("[0-9a-f-]{36}")) throw new IOException("Restore recovery belongs to another profile.");
        if (!String.join(",", profile.roots.keySet()).equals(p.getProperty("roots"))) throw new IOException("Restore layout changed; keep the recovery files.");
        boolean committed = "1".equals(p.getProperty("committed"));
        for (Map.Entry<String, File> r : profile.roots.entrySet()) {
            File target = r.getValue(), old = adjacent(target, token, ".old"), stage = adjacent(target, token, ".new");
            safe(target); safe(old); safe(stage);
            if (!committed) {
                if (exists(old)) { erase(target); move(old, target); }
                else if ("0".equals(p.getProperty("had." + r.getKey()))) erase(target);
                else if (!exists(target)) throw new IOException("The original save cannot be found for recovery.");
            } else erase(old);
            erase(stage);
        }
        Files.delete(journal.toPath());
    }
    private static void journal(File work, Properties p) throws IOException {
        File tmp = new File(work, "restore.tmp"), destination = new File(work, "restore.properties");
        safe(tmp); safe(destination);
        try (FileOutputStream out = new FileOutputStream(tmp)) { p.store(out, "WiiDS restore recovery"); out.getFD().sync(); }
        Files.move(tmp.toPath(), destination.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    }
    /** Caller owns a process/file lock and has checked that no native game is running. */
    public static void restore(Profile profile, File archive, File work, File previous) throws IOException {
        mkdir(work); recover(profile, work);
        File extracted = new File(work, "extract-" + UUID.randomUUID()); mkdir(extracted);
        Properties p = new Properties(); String token = UUID.randomUUID().toString();
        p.setProperty("profile", profile.id); p.setProperty("token", token);
        p.setProperty("roots", String.join(",", profile.roots.keySet()));
        boolean started = false;
        try {
            Inventory wanted = verify(profile, archive, extracted);
            // The previous save remains a dated, verified archive even after a successful restore.
            backup(profile, previous, true);
            for (Map.Entry<String, File> r : profile.roots.entrySet()) {
                File target = r.getValue(), stage = adjacent(target, token, ".new");
                safe(target); p.setProperty("had." + r.getKey(), exists(target) ? "1" : "0");
                mkdir(target.getParentFile());
                if (Boolean.TRUE.equals(wanted.roots.get(r.getKey()))) {
                    mkdir(stage); File source = new File(extracted, r.getKey());
                    if (source.isDirectory()) copyTree(source, stage);
                }
            }
            journal(work, p); started = true;
            for (File target : profile.roots.values()) {
                File old = adjacent(target, token, ".old"), stage = adjacent(target, token, ".new");
                if (exists(target)) move(target, old);
                if (exists(stage)) move(stage, target);
            }
            // Verify destination bytes before recording commit.
            Inventory installed = inventory(profile);
            if (!Arrays.equals(manifestWithoutTime(wanted), manifestWithoutTime(installed))) throw new IOException("Restored save did not match the backup.");
            p.setProperty("committed", "1"); journal(work, p);
            recover(profile, work);
        } catch (IOException | RuntimeException failure) {
            if (started) try { recover(profile, work); } catch (IOException recovery) { failure.addSuppressed(recovery); }
            throw failure;
        } finally {
            erase(extracted);
            if (!pending(work)) for (File target : profile.roots.values()) erase(adjacent(target, token, ".new"));
        }
    }
    private static void copyTree(File source, File destination) throws IOException {
        safe(source); File[] files = source.listFiles(); if (files == null) throw new IOException("Cannot read staged save.");
        for (File f : files) {
            File target = child(destination, f.getName());
            if (f.isDirectory()) { mkdir(target); copyTree(f, target); }
            else try (InputStream in = new FileInputStream(f); FileOutputStream out = new FileOutputStream(target)) { transfer(in, out, MAX_BYTES); out.getFD().sync(); }
        }
    }
}
