package dev.wiids.brawl;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileChannel;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;

/** Reuses the user's existing Project+ setup without moving or modifying any Brawl file. */
final class ProjectPlusFiles {
    static final String SAVE = "NAND/title/00010000/52534245/data";
    private static final String CARD = "sd.raw";
    private static final String CODE = "Codes/RSBE01.gct";
    private static final String SNAPSHOT = ".initial-save-snapshot";
    private static final LinkOption NO_LINKS = LinkOption.NOFOLLOW_LINKS;

    private ProjectPlusFiles() {}

    static boolean ready(File profile) {
        return Files.isDirectory(profile.toPath(), NO_LINKS)
                && isNonempty(profile, CARD) && isNonempty(profile, CODE);
    }

    static void snapshot(File original, File projectPlus) throws IOException {
        if (ready(original) && !ready(projectPlus)) runSetup(original, projectPlus, false);
    }

    static void prepare(File original, File projectPlus) throws IOException {
        if (!ready(projectPlus)) runSetup(original, projectPlus, true);
    }

    /** The two launch processes share the lock. The card is published last, after a complete copy. */
    private static synchronized void runSetup(File original, File projectPlus, boolean install) throws IOException {
        Path destination = projectPlus.toPath();
        Files.createDirectories(destination.getParent());
        Path lockPath = destination.resolveSibling(".project-plus-setup.lock");
        try (FileChannel channel = FileChannel.open(lockPath, StandardOpenOption.CREATE, StandardOpenOption.WRITE);
             java.nio.channels.FileLock ignored = channel.lock()) {
            if (ready(projectPlus)) return;
            if (!ready(original)) {
                if (install) throw new IOException("Project+ needs your SD card image and Gecko codes.");
                return;
            }

            Path work = destination.resolveSibling(".project-plus-setup");
            deleteTree(work);
            Files.createDirectory(work);
            try {
                if (Files.exists(destination, NO_LINKS) && !Files.isDirectory(destination, NO_LINKS))
                    throw new IOException("The Project+ data folder is unavailable.");
                Files.createDirectories(destination);
                snapshotSave(original.toPath(), destination, work);
                if (install) installMod(original.toPath(), destination, work);
            } finally {
                deleteTree(work);
            }
        }
    }

    private static void snapshotSave(Path original, Path destination, Path work) throws IOException {
        Path marker = destination.resolve(SNAPSHOT);
        if (Files.exists(marker, NO_LINKS)) return;
        Path existing = original.resolve(SAVE);
        Path copy = destination.resolve(SAVE);
        if (Files.isDirectory(existing, NO_LINKS) && !Files.exists(copy, NO_LINKS)) {
            Path stagedSave = work.resolve("save");
            copyTree(existing, stagedSave);
            Files.createDirectories(copy.getParent());
            Files.move(stagedSave, copy);
        }
        Files.createFile(marker);
    }

    private static void installMod(Path original, Path destination, Path work) throws IOException {
        Path card = destination.resolve(CARD);
        Path codes = destination.resolve("Codes");
        if (Files.exists(card, NO_LINKS) && !isNonempty(destination.toFile(), CARD))
            throw new IOException("Replace the invalid project-plus/sd.raw with your Project+ card image.");
        if (Files.exists(codes, NO_LINKS) && !isNonempty(destination.toFile(), CODE))
            throw new IOException("Add your Project+ codes to project-plus/Codes/RSBE01.gct.");
        if (!Files.exists(card, NO_LINKS)) {
            Path source = original.resolve(CARD);
            if (destination.toFile().getUsableSpace() < Files.size(source))
                throw new IOException("Free enough space to copy your Project+ SD card.");
            copyFile(source, work.resolve(CARD));
        }
        if (!Files.exists(codes, NO_LINKS)) {
            copyTree(original.resolve("Codes"), work.resolve("Codes"));
            Files.move(work.resolve("Codes"), codes);
        }
        if (!isNonempty(destination.toFile(), CODE))
            throw new IOException("Project+ needs a valid Codes/RSBE01.gct file.");
        if (Files.exists(work.resolve(CARD), NO_LINKS)) Files.move(work.resolve(CARD), card);
        if (!ready(destination.toFile())) throw new IOException("The Project+ SD card could not be prepared.");
    }

    private static boolean isNonempty(File root, String name) {
        Path file = root.toPath().resolve(name);
        try {
            return Files.isRegularFile(file, NO_LINKS) && file.toRealPath().startsWith(root.toPath().toRealPath())
                    && Files.size(file) > 0;
        } catch (IOException e) {
            return false;
        }
    }

    private static void copyFile(Path source, Path target) throws IOException {
        long size = Files.size(source);
        long modified = Files.getLastModifiedTime(source, NO_LINKS).toMillis();
        long total = 0;
        try (InputStream in = Files.newInputStream(source, NO_LINKS);
             FileOutputStream out = new FileOutputStream(target.toFile())) {
            byte[] buffer = new byte[1024 * 1024];
            for (int n; (n = in.read(buffer)) != -1; ) {
                out.write(buffer, 0, n);
                total += n;
            }
            out.getFD().sync();
        }
        if (total != size || Files.size(source) != size
                || Files.getLastModifiedTime(source, NO_LINKS).toMillis() != modified)
            throw new IOException("A Project+ source file changed while it was being copied.");
    }

    private static void copyTree(Path source, Path target) throws IOException {
        if (!Files.isDirectory(source, NO_LINKS)) throw new IOException("A Project+ source folder is unavailable.");
        Files.walkFileTree(source, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult preVisitDirectory(Path dir, BasicFileAttributes attrs) throws IOException {
                Files.createDirectory(target.resolve(source.relativize(dir)));
                return FileVisitResult.CONTINUE;
            }

            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                if (!attrs.isRegularFile()) throw new IOException("A Project+ source file is not a regular file.");
                copyFile(file, target.resolve(source.relativize(file)));
                return FileVisitResult.CONTINUE;
            }
        });
    }

    private static void deleteTree(Path root) throws IOException {
        if (!Files.exists(root, NO_LINKS)) return;
        Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }

            @Override public FileVisitResult postVisitDirectory(Path dir, IOException error) throws IOException {
                if (error != null) throw error;
                Files.delete(dir);
                return FileVisitResult.CONTINUE;
            }
        });
    }
}
