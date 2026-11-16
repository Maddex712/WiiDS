package dev.wiids.xd;

import java.io.*;
import java.security.MessageDigest;
import java.util.function.BooleanSupplier;

/** Import verifier, called before the common installer's atomic directory swap. */
public final class XdDisc {
    private XdDisc() {}
    public static final long BYTES = 1459978240L;
    public static final String SHA1 = "c1b5218f832403d15aa500ac4d6aacc8865c792d";
    public static final String DOL_SHA256 = "c8659341c252d1b088753d508769344eb6f092b5da5cc7cc0ce1560b32478c15";
    public static String validate(File directory, BooleanSupplier cancelled) {
        return validateImage(new File(directory, "game.iso"), cancelled);
    }
    public static String validateImage(File image, BooleanSupplier cancelled) {
        try (RandomAccessFile in = new RandomAccessFile(image, "r")) {
            if (in.length() != BYTES) return "Pokemon XD needs a complete USA GXXE01 revision 0 disc.";
            byte[] header = new byte[0x440]; in.readFully(header);
            if (header[0]!='G'||header[1]!='X'||header[2]!='X'||header[3]!='E'||header[4]!='0'||header[5]!='1'||
                    header[6]!=0||header[7]!=0||u32(header,0x1c)!=0xc2339f3dL||u32(header,0x200)==0x4e4b4954L)
                return "This is not the supported Pokemon XD USA GXXE01 revision 0 disc.";
            MessageDigest hash = MessageDigest.getInstance("SHA-1"); in.seek(0);
            byte[] block = new byte[1<<20]; long done=0;
            for (int n; (n=in.read(block))!=-1;) {
                if(cancelled.getAsBoolean()) return "Import cancelled.";
                hash.update(block,0,n); done+=n;
            }
            if(cancelled.getAsBoolean()) return "Import cancelled.";
            if(done!=BYTES||in.length()!=BYTES||!SHA1.equals(hex(hash.digest())))
                return "Pokemon XD disc checksum differs from supported USA revision 0. Existing data was kept.";
            return null;
        } catch (Exception e) { return "Pokemon XD disc could not be verified. Existing data was kept."; }
    }
    private static long u32(byte[] b,int o) { return ((long)(b[o]&255)<<24)|((long)(b[o+1]&255)<<16)|((long)(b[o+2]&255)<<8)|(b[o+3]&255); }
    private static String hex(byte[] bytes) { StringBuilder s=new StringBuilder(); for(byte b:bytes)s.append(String.format(java.util.Locale.ROOT,"%02x",b&255)); return s.toString(); }
}
