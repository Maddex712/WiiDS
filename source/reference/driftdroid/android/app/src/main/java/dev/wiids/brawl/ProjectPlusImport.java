package dev.wiids.brawl;

import java.io.*;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.util.*;
import java.util.function.BooleanSupplier;
import java.util.zip.*;

/** Installs the mod card and USA Gecko files without changing Brawl, saves or settings. */
public final class ProjectPlusImport {
    public interface Progress { void update(String phase, long bytes); }
    interface Step { void at(String name) throws IOException; }
    private interface Source { void stage(Path work) throws IOException; }
    private static final long MAX_CARD = 8L << 30, MAX_ZIP = 8L << 30, MAX_CODES = 32L << 20;
    private static final long RESERVE = 64L << 20;
    private static final String JOURNAL = ".project-plus-import.properties";
    private static final LinkOption NO_LINKS = LinkOption.NOFOLLOW_LINKS;
    private static final Set<String> STAGED = new HashSet<>(Arrays.asList("archive.zip", "sd.raw", "RSBE01.gct", "gc.txt"));
    private enum Member {
        CARD("sd.raw", "sd.raw", "Card", "card"),
        CODES("RSBE01.gct", "Codes/RSBE01.gct", "Code", "codes"),
        CONFIG("gc.txt", "Codes/gc.txt", "Config", "config");

        final String name, target, field, step;
        Member(String name, String target, String field, String step) {
            this.name = name; this.target = target; this.field = field; this.step = step;
        }
        String old() { return "old" + field; }
        String oldHash() { return old() + "Hash"; }
        String newHash() { return "new" + field; }
    }

    private static final List<Member> PAIR = Arrays.asList(Member.CARD, Member.CODES);
    private static final List<Member> OFFICIAL = Arrays.asList(Member.CARD, Member.CODES, Member.CONFIG);
    private ProjectPlusImport() {}

    public static long officialDownloadSize() { return ProjectPlusOfficial.CARD_BYTES; }

    public static void installOfficial(File profile, BooleanSupplier cancel, Progress progress) throws IOException {
        installOfficial(profile, ProjectPlusOfficial::download, cancel, progress, name -> {});
    }

    interface OpenCard { InputStream open() throws IOException; }

    static void installOfficial(File profile, OpenCard download, BooleanSupplier cancel,
                                Progress progress, Step step) throws IOException {
        execute(profile, work -> {
            if (work.toFile().getUsableSpace() < ProjectPlusOfficial.CARD_BYTES + RESERVE)
                throw new IOException("Free at least 2.2 GB where WiiDS stores its games, then try again.");
            checkCancel(cancel);
            progress.update("Connecting to the Project+ download", 0);
            MessageDigest digest = sha256();
            Path card = work.resolve("sd.raw");
            try (DigestInputStream input = new DigestInputStream(download.open(), digest)) {
                long received = copy(input, card, ProjectPlusOfficial.CARD_BYTES, cancel, progress, "Downloading Project+ 3.2.1");
                if (received != ProjectPlusOfficial.CARD_BYTES || !hex(digest).equals(ProjectPlusOfficial.CARD_SHA))
                    throw new IOException("The Project+ download did not match the verified 3.2.1 release. Try again.");
            }
            stageOfficialCodeFiles(work, card, cancel, progress);
        }, cancel, progress, step);
    }

    private static void stageOfficialCodeFiles(Path work, Path card, BooleanSupplier cancel,
                                               Progress progress) throws IOException {
        byte[] codes = ProjectPlusOfficial.codes(card);
        Path codeFile = work.resolve(Member.CODES.name);
        progress.update("Preparing Project+ codes", 0);
        copy(new ByteArrayInputStream(codes), codeFile, codes.length, cancel, (phase, bytes) -> {}, "Preparing codes");
        requireHash(codeFile, ProjectPlusOfficial.CODE_SHA);
        stageOfficialConfig(work, card, cancel);
    }

    public static boolean pending(File profile) {
        return Files.exists(profile.toPath().resolve(JOURNAL), NO_LINKS);
    }

    public static void importFiles(File profile, InputStream card, InputStream codes,
                                   BooleanSupplier cancel, Progress progress) throws IOException {
        importFiles(profile, card, codes, cancel, progress, name -> {});
    }

    static void importFiles(File profile, InputStream card, InputStream codes,
                            BooleanSupplier cancel, Progress progress, Step step) throws IOException {
        execute(profile, work -> {
            Path stagedCard = work.resolve("sd.raw"), stagedCodes = work.resolve("RSBE01.gct");
            copy(card, stagedCard, MAX_CARD, cancel, progress, "Copying SD image");
            copy(codes, stagedCodes, MAX_CODES, cancel, progress, "Copying codes");
            stageConfigForOfficialPair(work, stagedCard, stagedCodes, cancel, progress);
        }, cancel, progress, step);
    }

    public static void importZip(File profile, InputStream archive, BooleanSupplier cancel,
                                 Progress progress) throws IOException {
        execute(profile, work -> {
            Path zip = work.resolve("archive.zip");
            copy(archive, zip, MAX_ZIP, cancel, progress, "Reading Project+ ZIP");
            try (ZipFile input = new ZipFile(zip.toFile())) {
                ZipEntry card = null, codes = null;
                int count = 0;
                Enumeration<? extends ZipEntry> entries = input.entries();
                while (entries.hasMoreElements()) {
                    checkCancel(cancel);
                    if (++count > 65536) throw new IOException("The ZIP has too many entries. Extract it and choose the two files instead.");
                    ZipEntry entry = entries.nextElement();
                    String name = zipName(entry.getName());
                    if (entry.isDirectory()) continue;
                    String leaf = name.substring(name.lastIndexOf('/') + 1);
                    if (leaf.equalsIgnoreCase("sd.raw")) {
                        if (card != null) throw new IOException("This ZIP contains more than one sd.raw. Extract it and choose the correct files.");
                        card = entry;
                    } else if (leaf.equalsIgnoreCase("RSBE01.gct")) {
                        if (codes != null) throw new IOException("This ZIP contains more than one USA codes file. Extract it and choose the correct files.");
                        codes = entry;
                    }
                }
                if (card == null) throw new IOException("Choose a Project+ ZIP containing an SD image, or use Install Project+.");
                long codeSize = codes == null ? 0 : codes.getSize();
                if (card.getSize() <= 0 || card.getSize() > MAX_CARD || codeSize < 0 || codeSize > MAX_CODES ||
                        (codes != null && codeSize == 0))
                    throw new IOException("The ZIP's Project+ files have unsupported sizes.");
                if (codes == null && card.getSize() != ProjectPlusOfficial.CARD_BYTES)
                    throw new IOException("This ZIP has no separate USA codes file. Use Install Project+ for the official release.");
                if (work.toFile().getUsableSpace() < card.getSize() + codeSize + RESERVE)
                    throw new IOException("Free more space for the extracted Project+ files, or use Install Project+.");
                CRC32 cardCrc = new CRC32(), codesCrc = new CRC32();
                Path stagedCard = work.resolve("sd.raw");
                try (InputStream in = new CheckedInputStream(input.getInputStream(card), cardCrc)) {
                    long bytes = copy(in, stagedCard, MAX_CARD, cancel, progress, "Extracting SD image");
                    if (bytes != card.getSize() || cardCrc.getValue() != card.getCrc()) throw new IOException("The ZIP's SD image is incomplete or damaged.");
                }
                if (codes == null) {
                    if (!hash(stagedCard).equals(ProjectPlusOfficial.CARD_SHA))
                        throw new IOException("This SD image is from a different Project+ build. Use Install Project+ for version 3.2.1.");
                    stageOfficialCodeFiles(work, stagedCard, cancel, progress);
                    return;
                }
                Path stagedCodes = work.resolve("RSBE01.gct");
                try (InputStream in = new CheckedInputStream(input.getInputStream(codes), codesCrc)) {
                    long bytes = copy(in, stagedCodes, MAX_CODES, cancel, progress, "Extracting codes");
                    if (bytes != codes.getSize() || codesCrc.getValue() != codes.getCrc()) throw new IOException("The ZIP's codes are incomplete or damaged.");
                }
                stageConfigForOfficialPair(work, stagedCard, stagedCodes, cancel, progress);
            } catch (ZipException invalid) {
                throw new IOException("This is not a readable ZIP. Extract .7z or .rar downloads first, then choose sd.raw and RSBE01.gct.", invalid);
            }
        }, cancel, progress, name -> {});
    }

    private static void stageConfigForOfficialPair(Path work, Path card, Path codes, BooleanSupplier cancel,
                                                   Progress progress) throws IOException {
        if (Files.size(card) != ProjectPlusOfficial.CARD_BYTES || !hash(codes).equals(ProjectPlusOfficial.CODE_SHA)) return;
        progress.update("Checking for the official Project+ SD image", 0);
        if (hash(card).equals(ProjectPlusOfficial.CARD_SHA)) stageOfficialConfig(work, card, cancel);
    }

    private static void stageOfficialConfig(Path work, Path card, BooleanSupplier cancel) throws IOException {
        Path configFile = work.resolve(Member.CONFIG.name);
        byte[] config = ProjectPlusOfficial.config(card);
        copy(new ByteArrayInputStream(config), configFile, config.length, cancel, (phase, bytes) -> {}, "Preparing launch configuration");
        requireHash(configFile, ProjectPlusOfficial.CONFIG_SHA);
    }

    private static String zipName(String name) throws IOException {
        if (name.length() > 4096 || name.startsWith("/") || name.startsWith("\\") || name.indexOf(':') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("The ZIP contains an unsafe path.");
        String[] parts = name.replace('\\', '/').split("/", -1);
        if (parts.length > 32) throw new IOException("The ZIP is nested too deeply.");
        for (String part : parts) if (part.equals("..")) throw new IOException("The ZIP contains an unsafe path.");
        return name.replace('\\', '/');
    }

    private static void execute(File file, Source source, BooleanSupplier cancel, Progress progress, Step step) throws IOException {
        Path root = root(file), lock = root.getParent().resolve(".project-plus-setup.lock");
        safe(root);safe(lock);Files.createDirectories(root.getParent());
        try (FileChannel channel = FileChannel.open(lock, StandardOpenOption.CREATE, StandardOpenOption.WRITE, NO_LINKS);
             FileLock ignored = channel.lock()) {
            recoverLocked(root);
            Path work = work(root);
            cleanStage(work);Files.createDirectory(work);
            try {
                source.stage(work);checkCancel(cancel);
                progress.update("Checking SD image and codes", 0);
                validateCard(work.resolve("sd.raw"));validateCodes(work.resolve("RSBE01.gct"));
                checkCancel(cancel);
                progress.update("Finishing setup", -1); // Cancellation ends before publishing starts.
                publish(root, work, step);
            } catch (IOException failure) {
                if (pending(file)) {
                    try { recoverLocked(root); } catch (IOException recovery) { failure.addSuppressed(recovery); }
                }
                throw failure;
            } finally {
                if (!pending(file)) cleanStage(work);
            }
        }
    }

    public static void recover(File file) throws IOException {
        Path root = root(file), lock = root.getParent().resolve(".project-plus-setup.lock");
        if (!pending(file) && !Files.exists(work(root), NO_LINKS) && !Files.exists(root.resolve(JOURNAL + ".new"), NO_LINKS)) return;
        safe(root);safe(lock);Files.createDirectories(root.getParent());
        try (FileChannel channel = FileChannel.open(lock, StandardOpenOption.CREATE, StandardOpenOption.WRITE, NO_LINKS);
             FileLock ignored = channel.lock()) { recoverLocked(root); }
    }

    static void recoverLocked(Path root) throws IOException {
        Path journal = root.resolve(JOURNAL);safe(journal);
        Path next = root.resolve(JOURNAL + ".new");safe(next);
        // A kill during the scratch write may leave any prefix, including an empty file.
        // Preserve it separately; only the atomically published journal controls recovery.
        if (Files.exists(next, NO_LINKS)) {
            if (!Files.isRegularFile(next, NO_LINKS)) throw new IOException("Project+ temporary recovery data needs review.");
            Path retained = root.resolve("ImportBackups").resolve(UUID.randomUUID().toString());
            safe(retained);Files.createDirectories(retained);
            Files.move(next, retained.resolve("interrupted-journal.properties"));
        }
        if (!Files.exists(journal, NO_LINKS)) {
            cleanStage(work(root));
            return;
        }
        Map<String,String> state = readJournal(journal);
        Path backup = root.resolve("ImportBackups").resolve(state.get("id"));safe(backup);
        List<Member> members = state.containsKey(Member.CONFIG.newHash()) ? OFFICIAL : PAIR;
        if (state.get("state").equals("committed")) {
            for (Member member : members) requirePublished(root.resolve(member.target), state.get(member.newHash()));
        } else {
            // Verify all targets before changing any of them.
            for (Member member : members)
                canRollback(root.resolve(member.target), backup.resolve(member.name), state.get(member.old()),
                        state.get(member.oldHash()), state.get(member.newHash()));
            for (Member member : members)
                rollback(root.resolve(member.target), backup.resolve(member.name), state.get(member.old()));
        }
        Files.delete(journal);
        cleanStage(work(root));
    }

    private static void publish(Path root, Path work, Step step) throws IOException {
        safe(root);Files.createDirectories(root);
        boolean installConfig = Files.exists(work.resolve(Member.CONFIG.name), NO_LINKS);
        Path currentConfig = root.resolve(Member.CONFIG.target);
        safe(currentConfig);
        String newCodeHash = hash(work.resolve(Member.CODES.name));
        boolean officialCodes = newCodeHash.equals(ProjectPlusOfficial.CODE_SHA);
        boolean hasConfig = Files.isRegularFile(currentConfig, NO_LINKS);
        if (officialCodes && !installConfig && !hasConfig)
            throw new IOException("These Project+ 3.2.1 codes need a launch configuration. Use Install Project+ or import the original Dolphin ZIP.");
        // A used official card may change during play while its codes still need this config.
        boolean archiveConfig = !installConfig && !officialCodes && hasConfig
                && hash(currentConfig).equals(ProjectPlusOfficial.CONFIG_SHA);
        List<Member> members = installConfig || archiveConfig ? OFFICIAL : PAIR;
        Map<String,String> state = new LinkedHashMap<>();
        state.put("id", UUID.randomUUID().toString());state.put("state", "prepared");
        for (Member member : members) {
            Path target = root.resolve(member.target);
            safe(target);
            Files.createDirectories(target.getParent());
            boolean exists = Files.exists(target, NO_LINKS);
            if (exists && !Files.isRegularFile(target, NO_LINKS))
                throw new IOException("The existing Project+ files need review. They were kept.");
            state.put(member.old(), Boolean.toString(exists));
            state.put(member.oldHash(), exists ? hash(target) : "");
            String replacement = hashForPublish(work, member, newCodeHash, archiveConfig);
            state.put(member.newHash(), replacement);
        }
        Path backup = root.resolve("ImportBackups").resolve(state.get("id"));safe(backup);Files.createDirectories(backup);
        writeJournal(root, state);step.at("prepared");
        for (Member member : members) {
            if (state.get(member.old()).equals("true")) Files.move(root.resolve(member.target), backup.resolve(member.name));
            step.at("backup-" + member.step);
        }
        // Publish the card last so it is never paired with half-installed codes.
        for (int i = members.size() - 1; i >= 0; i--) {
            Member member = members.get(i);
            if (!state.get(member.newHash()).isEmpty()) Files.move(work.resolve(member.name), root.resolve(member.target));
            step.at("publish-" + member.step);
        }
        state.put("state", "committed");writeJournal(root, state);step.at("committed");
        Files.delete(root.resolve(JOURNAL));step.at("finished");
    }

    private static String hashForPublish(Path work, Member member, String newCodeHash, boolean archiveConfig) throws IOException {
        if (member == Member.CODES) return newCodeHash;
        if (member == Member.CONFIG && archiveConfig) return "";
        return hash(work.resolve(member.name));
    }

    private static void canRollback(Path target, Path old, String existed, String oldHash, String newHash) throws IOException {
        safe(target);safe(old);
        if (existed.equals("true")) {
            if (Files.exists(old, NO_LINKS)) {
                requireHash(old, oldHash);
                if (Files.exists(target, NO_LINKS)) requirePublished(target, newHash);
            } else requireHash(target, oldHash);
        } else {
            if (Files.exists(old, NO_LINKS)) throw new IOException("Unexpected Project+ recovery files. Keep them in place.");
            if (Files.exists(target, NO_LINKS)) requirePublished(target, newHash);
        }
    }

    private static void rollback(Path target, Path old, String existed) throws IOException {
        if (Files.exists(old, NO_LINKS)) {
            Files.deleteIfExists(target);Files.createDirectories(target.getParent());Files.move(old, target);
        } else if (existed.equals("false")) Files.deleteIfExists(target);
    }

    private static void writeJournal(Path root, Map<String,String> state) throws IOException {
        StringBuilder text = new StringBuilder();
        for (Map.Entry<String,String> e : state.entrySet()) text.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        Path next = root.resolve(JOURNAL + ".new");safe(next);
        try (FileOutputStream out = new FileOutputStream(next.toFile())) {
            out.write(text.toString().getBytes(StandardCharsets.US_ASCII));out.getFD().sync();
        }
        Files.move(next, root.resolve(JOURNAL), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
    }

    private static Map<String,String> readJournal(Path path) throws IOException {
        safe(path);
        if (!Files.isRegularFile(path, NO_LINKS) || Files.size(path) > 4096) throw new IOException("Project+ recovery data needs review. Keep its files.");
        Map<String,String> result = new LinkedHashMap<>();
        for (String line : new String(Files.readAllBytes(path), StandardCharsets.US_ASCII).split("\n")) {
            int split = line.indexOf('=');
            if (split <= 0 || result.put(line.substring(0,split), line.substring(split+1)) != null)
                throw new IOException("Project+ recovery data is invalid. Keep its files.");
        }
        List<Member> members = result.containsKey(Member.CONFIG.newHash()) ? OFFICIAL : PAIR;
        Set<String> fields = new HashSet<>(Arrays.asList("id", "state"));
        for (Member member : members) fields.addAll(Arrays.asList(member.old(), member.oldHash(), member.newHash()));
        if (!result.keySet().equals(fields) || !result.get("id").matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}") ||
            !Arrays.asList("prepared","committed").contains(result.get("state"))) throw new IOException("Project+ recovery data is invalid. Keep its files.");
        for (Member member : members) {
            String present = result.get(member.old()), hash = result.get(member.oldHash());
            String replacement = result.get(member.newHash());
            boolean archivesOfficialConfig = member == Member.CONFIG && replacement.isEmpty()
                    && hash.equals(ProjectPlusOfficial.CONFIG_SHA);
            if ((!present.equals("true") && !present.equals("false")) || !hash.matches(present.equals("true") ? "[0-9a-f]{64}" : "") ||
                    (!archivesOfficialConfig && !replacement.matches("[0-9a-f]{64}")))
                throw new IOException("Project+ recovery checksum is invalid.");
        }
        return result;
    }

    private static Path root(File file) throws IOException {
        Path root = file.toPath().toAbsolutePath().normalize();
        boolean external = root.getFileName().toString().equals("project-plus");
        boolean internal = root.getFileName().toString().equals("data") && root.getParent() != null
                && root.getParent().getFileName().toString().equals("project-plus");
        if (!external && !internal) throw new IOException("Invalid Project+ storage root.");
        safe(root);return root;
    }
    private static Path work(Path root) { return root.resolveSibling(".project-plus-import-stage"); }
    static void safe(Path path) throws IOException {
        for (Path p = path.toAbsolutePath(); p != null; p = p.getParent())
            if (Files.isSymbolicLink(p)) throw new IOException("Project+ storage contains a symbolic link. Existing files were kept.");
    }
    private static void cleanStage(Path work) throws IOException {
        safe(work);
        if (!Files.exists(work, NO_LINKS)) return;
        if (!Files.isDirectory(work, NO_LINKS)) throw new IOException("Project+ temporary storage needs review.");
        try (DirectoryStream<Path> files = Files.newDirectoryStream(work)) {
            for (Path p : files) if (!STAGED.contains(p.getFileName().toString()) || !Files.isRegularFile(p, NO_LINKS) || Files.isSymbolicLink(p))
                throw new IOException("Unknown files in Project+ temporary storage were kept.");
        }
        for (String name : STAGED) Files.deleteIfExists(work.resolve(name));
        Files.delete(work);
    }
    private static void checkCancel(BooleanSupplier cancel) throws IOException {
        if (cancel.getAsBoolean()) throw new InterruptedIOException("Import cancelled. Your current Project+ files were kept.");
    }
    private static long copy(InputStream in, Path file, long maximum, BooleanSupplier cancel, Progress progress, String label) throws IOException {
        safe(file);long total = 0, spaceAt = 0;
        try (FileOutputStream out = new FileOutputStream(file.toFile())) {
            byte[] buffer = new byte[1024 * 1024];
            while (true) {
                checkCancel(cancel);
                int n = in.read(buffer);if (n < 0) break;if (n == 0) continue;
                if (n > maximum - total) throw new IOException("The selected Project+ file is too large.");
                if (total >= spaceAt) {
                    if (file.toFile().getUsableSpace() < RESERVE + n) throw new IOException("Free more space before importing Project+.");
                    spaceAt = total + (16L << 20);
                }
                out.write(buffer,0,n);total += n;progress.update(label,total);
            }
            out.getFD().sync();
        }
        if (total == 0) throw new IOException("A selected Project+ file is empty.");
        checkCancel(cancel);return total;
    }
    private static String hash(Path file) throws IOException {
        safe(file);if (!Files.isRegularFile(file, NO_LINKS)) throw new IOException("A Project+ recovery file is missing.");
        MessageDigest digest = sha256();
        try (InputStream in = Files.newInputStream(file,NO_LINKS)) {
            byte[] buffer = new byte[1024 * 1024];for (int n = (in.read(buffer)); n != -1; n = in.read(buffer)) digest.update(buffer,0,n);
        }
        return hex(digest);
    }

    private static MessageDigest sha256() throws IOException {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (java.security.NoSuchAlgorithmException impossible) { throw new IOException(impossible); }
    }

    private static String hex(MessageDigest digest) {
        StringBuilder value = new StringBuilder();
        for (byte b : digest.digest()) value.append(String.format(Locale.ROOT,"%02x",b & 255));
        return value.toString();
    }

    private static void requireHash(Path file, String expected) throws IOException {
        if (!hash(file).equals(expected)) throw new IOException("A Project+ recovery file changed. Current files and backups were kept for review.");
    }

    private static void requirePublished(Path file, String expected) throws IOException {
        if (!expected.isEmpty()) {
            requireHash(file, expected);
        } else if (Files.exists(file, NO_LINKS)) {
            throw new IOException("Unexpected Project+ configuration. Current files and backups were kept for review.");
        }
    }

    static void validateCodes(Path file) throws IOException {
        long size = Files.size(file);
        if (size < 16 || size > MAX_CODES || size % 8 != 0) throw new IOException("Choose the binary USA RSBE01.gct codes file from your Project+ download.");
        try (RandomAccessFile in = new RandomAccessFile(file.toFile(),"r")) {
            if (in.readLong() != 0x00d0c0de00d0c0deL) throw new IOException("This is not a binary Gecko codes file (RSBE01.gct).");
            in.seek(size-8);if (in.readLong() != 0xf000000000000000L) throw new IOException("The codes file is incomplete or unsupported.");
        }
    }
    static void validateCard(Path file) throws IOException {
        long size = Files.size(file);
        if (size < 512 || size > MAX_CARD) throw new IOException("Choose the Project+ virtual SD image, sd.raw.");
        try (RandomAccessFile in = new RandomAccessFile(file.toFile(),"r")) {
            byte[] sector = new byte[512];in.readFully(sector);
            if (fat(sector,0,size,size)) return;
            if ((sector[510]&255)==0x55 && (sector[511]&255)==0xaa) {
                for (int i=0;i<4;i++) {
                    int at=446+i*16,type=sector[at+4]&255;
                    if (type!=4&&type!=6&&type!=14&&type!=11&&type!=12) continue;
                    long offset=u32(sector,at+8)*512, length=u32(sector,at+12)*512;
                    if (offset<512||length<512||offset>size-length) continue;
                    byte[] boot=new byte[512];in.seek(offset);in.readFully(boot);
                    if (fat(boot,offset,size,length)) return;
                }
            }
        }
        throw new IOException("This is not a complete FAT16/FAT32 SD image. Choose Project+'s sd.raw, not a Dolphin memory card.");
    }
    private static boolean fat(byte[] b,long offset,long size,long partitionSize) {
        if ((b[510]&255)!=0x55||(b[511]&255)!=0xaa)return false;
        int bytes=u16(b,11),cluster=b[13]&255,reserved=u16(b,14),fats=b[16]&255,entries=u16(b,17);
        if (bytes<512||bytes>4096||(bytes&(bytes-1))!=0||cluster==0||(cluster&(cluster-1))!=0||cluster>128||reserved==0||fats<1||fats>2)return false;
        long sectors=u16(b,19);if(sectors==0)sectors=u32(b,32);
        long fatSize=u16(b,22);boolean is32=fatSize==0;if(is32)fatSize=u32(b,36);
        long rootSectors=((long)entries*32+bytes-1)/bytes;
        long overhead=reserved+fats*fatSize+rootSectors,data=sectors-overhead,clusters=data/cluster,volume=sectors*bytes;
        if(fatSize==0||data<=0||volume>partitionSize||offset>size-volume||clusters<4085)return false;
        if(is32)return entries==0&&clusters>=65525&&u32(b,44)>=2&&u32(b,44)<clusters+2&&(clusters+2)*4<=fatSize*bytes;
        return entries>0&&clusters<65525&&(clusters+2)*2<=fatSize*bytes;
    }
    private static int u16(byte[] b,int o){return (b[o]&255)|((b[o+1]&255)<<8);}
    private static long u32(byte[] b,int o){return (long)u16(b,o)|((long)u16(b,o+2)<<16);}
}
