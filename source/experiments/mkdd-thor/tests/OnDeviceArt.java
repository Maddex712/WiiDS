package dev.thor.mkdd;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;

/** Runs installed APK setup classes in a shell-owned scratch directory only. */
public final class OnDeviceArt {
    private static final String ROOT = "/data/local/tmp/wiids-release-wrap-20261108/";
    private static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] bytes = new byte[1024 * 1024];
            for (int n; (n = input.read(bytes)) >= 0;) digest.update(bytes, 0, n);
        }
        StringBuilder text = new StringBuilder();
        for (byte b : digest.digest()) text.append(String.format("%02x", b & 255));
        return text.toString();
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 3) throw new IOException("usage: source.iso scratch-output true|false");
        File source = new File(args[0]);
        File output = new File(args[1]);
        if (!output.getCanonicalPath().startsWith(ROOT) || output.exists())
            throw new IOException("Requires a new task-owned scratch output");
        byte[] ia8 = new byte[64]; ia8[0] = 3; ia8[3] = 4; ia8[5] = 4; ia8[31] = 32;
        ia8[33] = 83;
        if (MkddArt.btiPixels(ia8)[0] != 0x00535353)
            throw new IOException("Unassociated texture channels lost before material evaluation");
        String before = hash(source);
        long start = System.nanoTime();
        String error = MkddArt.build(source, output, Boolean.parseBoolean(args[2]),
            (done, total, name) -> System.out.println("ART " + done + "/" + total + " " + name));
        if (error != null) throw new IOException(error);
        if (!MkddArt.ready(output)) throw new IOException("Generated art rejected");
        if (!before.equals(hash(source))) throw new IOException("Source changed");
        System.out.println("PASS installed Android art baker; source_sha256=" + before +
            " elapsed_seconds=" + (System.nanoTime() - start) / 1.0e9);
    }
}
