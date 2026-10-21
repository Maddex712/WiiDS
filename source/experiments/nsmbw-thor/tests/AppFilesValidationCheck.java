package com.wither.nsmbw;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

/** Host check of real import guards. Does not load SDL, native code or an Android Activity. */
public final class AppFilesValidationCheck {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    private static byte[] header(String id, int revision) {
        byte[] data = new byte[8];
        System.arraycopy(id.getBytes(StandardCharsets.US_ASCII), 0, data, 0, 6);
        data[7] = (byte) revision;
        return data;
    }
    public static void main(String[] args) throws Exception {
        require(BuildConfig.THOR_BUILD && BuildConfig.DISC_REVISION == 2, "Run with Thor BuildConfig");
        require(AppFiles.headerProblem(header("SMNE01", 2)) == null, "owner revision header rejected");
        require(AppFiles.headerProblem(header("SMNE01", 1)) != null, "wrong revision accepted");
        require(AppFiles.headerProblem(header("SMNP01", 1)) != null, "wrong region accepted");
        require(AppFiles.headerProblem(new byte[4]) != null, "short header accepted");
        require(AppFiles.headerProblem(null) != null, "null header accepted");
        Path testRoot = Files.createTempDirectory("thor-nsmb-guard-");
        try {
            require(AppFiles.gameProblem(testRoot.toFile()) != null, "missing tree accepted");
            Files.createDirectories(testRoot.resolve("sys"));
            Files.createDirectories(testRoot.resolve("files"));
            Files.write(testRoot.resolve("sys/boot.bin"), header("SMNE01", 2));
            Files.write(testRoot.resolve("sys/main.dol"), new byte[256]);
            require(AppFiles.gameProblem(testRoot.toFile()) != null, "wrong executable accepted");
        } finally {
            Files.deleteIfExists(testRoot.resolve("sys/main.dol"));
            Files.deleteIfExists(testRoot.resolve("sys/boot.bin"));
            Files.deleteIfExists(testRoot.resolve("sys"));
            Files.deleteIfExists(testRoot.resolve("files"));
            Files.deleteIfExists(testRoot);
        }
        require(args.length == 1, "Pass owner's extracted DATA folder");
        require(AppFiles.gameProblem(Path.of(args[0]).toFile()) == null, "owner executable rejected");
        System.out.println("PASS: 8 negative/positive disc validation checks");
    }
}
