import dev.chasem.smslauncher.ThorStorage;
import java.io.*;
import java.nio.file.*;
public final class ThorDiscValidationTest {
  private static void reject(File f) throws Exception {
    try { ThorStorage.validateDisc(f); throw new AssertionError("Accepted malformed disc"); }
    catch (IOException expected) { }
  }
  public static void main(String[] args) throws Exception {
    File f = Files.createTempFile("sunshine-disc-header-", ".iso").toFile();
    try (RandomAccessFile out = new RandomAccessFile(f,"rw")) {
      byte[] h = new byte[32]; System.arraycopy("GMSE01".getBytes("US-ASCII"),0,h,0,6);
      h[28]=(byte)0xc2; h[29]=0x33; h[30]=(byte)0x9f; h[31]=0x3d;
      out.write(h); reject(f);
      out.setLength(1459978240L); ThorStorage.validateDisc(f);
      out.seek(7); out.write(1); reject(f);
      out.seek(7); out.write(0); out.seek(6); out.write(1); reject(f);
      out.seek(6); out.write(0); out.seek(3); out.write('P'); reject(f);
      out.seek(3); out.write('E'); out.seek(28); out.write(0); reject(f);
    } finally { if (!f.delete()) throw new IOException("Temporary fixture cleanup failed"); }
    System.out.println("PASS actual-source disc gate: clean header, truncated size, revision, disc number, region and magic");
  }
}
