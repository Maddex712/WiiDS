import dev.chasem.smslauncher.ThorStorage;
import java.io.*;
import java.nio.file.*;

/** Runs the actual Eclipse host's lightweight entry gate, without starting Android. */
public final class ThorDiscValidationTest {
    private static int checks;
    private static void reject(File file) throws Exception {
        try { ThorStorage.validateDisc(file); throw new AssertionError("Accepted a wrong disc"); }
        catch (IOException expected) { checks++; }
    }
    public static void main(String[] args) throws Exception {
        File file = Files.createTempFile("eclipse-entry-", ".fixture").toFile();
        try (RandomAccessFile out = new RandomAccessFile(file, "rw")) {
            byte[] header = new byte[32];
            System.arraycopy("GMSE04".getBytes("US-ASCII"), 0, header, 0, 6);
            header[28] = (byte)0xc2; header[29] = 0x33; header[30] = (byte)0x9f; header[31] = 0x3d;
            out.write(header); reject(file);
            out.setLength(1459978240L); ThorStorage.validateDisc(file); checks++;
            out.seek(5); out.write('1'); reject(file); // Original Sunshine cannot enter Eclipse.
            out.seek(5); out.write('4'); out.seek(7); out.write(1); reject(file);
            out.seek(7); out.write(0); out.seek(6); out.write(1); reject(file);
            out.seek(6); out.write(0); out.seek(3); out.write('P'); reject(file);
            out.seek(3); out.write('E'); out.seek(28); out.write(0); reject(file);
        } finally { if (!file.delete()) throw new IOException("Fixture cleanup failed"); }
        System.out.println("PASS " + checks + " actual-source Eclipse entry checks (header gate, not disc authentication)");
    }
}
