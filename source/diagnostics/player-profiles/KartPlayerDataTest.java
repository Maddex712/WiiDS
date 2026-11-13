import com.driftdroid.android.saves.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.CRC32;

public class KartPlayerDataTest {
    static int checks;
    interface Attempt {void run() throws Exception;}
    static void yes(boolean ok,String label){checks++;if(!ok)throw new AssertionError(label);}
    static void reject(Attempt a,String label)throws Exception {checks++;try{a.run();}catch(IOException expected){return;}throw new AssertionError("Must reject: "+label);}
    static void put(byte[] b,int p,long v,int n){for(int i=n-1;i>=0;i--){b[p+i]=(byte)v;v>>>=8;}}
    static int crc(byte[] b,int p,int n){int c=0;for(int i=p;i<p+n;i++){c^=(b[i]&255)<<8;for(int j=0;j<8;j++)c=((c&0x8000)!=0?(c<<1)^0x1021:c<<1)&65535;}return c;}
    static void name(byte[] b,int p,String s){Arrays.fill(b,p,p+20,(byte)0);byte[] n=s.getBytes(StandardCharsets.UTF_16BE);System.arraycopy(n,0,b,p,n.length);}
    static String name(byte[] b,int p){int n=0;while(n<20&&(b[p+n]!=0||b[p+n+1]!=0))n+=2;return new String(b,p,n,StandardCharsets.UTF_16BE);}
    static void saveCrc(byte[] b){CRC32 c=new CRC32();c.update(b,0,0x27ffc);put(b,0x27ffc,c.getValue(),4);}
    static byte[] record(int id,String n){byte[] b=new byte[74];b[0]=4;b[22]=63;b[23]=63;put(b,24,0x80000000L+id,4);put(b,28,0x44726f69,4);name(b,2,n);return b;}
    static byte[] save(int id,String n,int pid){byte[] b=new byte[0x2bc000];new Random(42).nextBytes(b);Arrays.fill(b,0,0x23308,(byte)0);
        System.arraycopy("RKSD0006".getBytes(StandardCharsets.US_ASCII),0,b,0,8);
        for(int slot=0;slot<2;slot++){int p=8+slot*0x8cc0;System.arraycopy("RKPD".getBytes(StandardCharsets.US_ASCII),0,b,p,4);
            byte[] r=record(slot==0?id:2,slot==0?n:"Other");name(b,p+0x14,slot==0?n:"Other");System.arraycopy(r,24,b,p+0x28,8);
            put(b,p+0x5c,slot==0?pid:345,4);System.arraycopy(r,0,b,p+0x5684,74);put(b,p+0x5684+74,crc(b,p+0x5684,74),2);
            b[p+0x30]=55;b[p+0xb0]=23;
        }saveCrc(b);return b;}
    static byte[] db(){byte[] b=new byte[779968];System.arraycopy("RNOD".getBytes(StandardCharsets.US_ASCII),0,b,0,4);
        for(int i=0;i<2;i++)System.arraycopy(record(i+1,i==0?"DriftDroid":"Other"),0,b,4+i*74,74);
        b[0x1cec]=(byte)0xc0;System.arraycopy("RNHD".getBytes(StandardCharsets.US_ASCII),0,b,0x1d00,4);put(b,0x1d04,0xffffffffL,4);
        put(b,0x1f1de,crc(b,0,0x1f1de),2);return b;}
    static void write(File f,byte[] b)throws Exception {f.getParentFile().mkdirs();Files.write(f.toPath(),b);}
    static byte[] read(File f)throws Exception{return Files.readAllBytes(f.toPath());}
    static File f(SaveArchive.Profile p,String root,String name){return new File(p.roots.get(root),name);}
    static byte[] rating(int pid,float vr){byte[] b=new byte[1640];System.arraycopy("RRRT".getBytes(StandardCharsets.US_ASCII),0,b,0,4);put(b,4,1,2);put(b,6,100,2);
        put(b,8,pid,4);put(b,12,Float.floatToIntBits(vr),4);put(b,16,Float.floatToIntBits(5000f),4);put(b,20,1,4);return b;}
    static Map<String,String> hashes(SaveArchive.Profile p)throws Exception {Map<String,String> m=new TreeMap<>();for(Map.Entry<String,SaveArchive.Entry> e:SaveArchive.inventory(p).files.entrySet())m.put(e.getKey(),e.getValue().hash);return m;}
    static File backup(File base,String s){return new File(base,"backups/"+s+".zip");}
    public static void main(String[] args)throws Exception {
        File base=Files.createTempDirectory(Paths.get(args[0]),"kart-players-").toFile(),work=new File(base,"work");
        LinkedHashMap<String,File> roots=new LinkedHashMap<>();for(String r:new String[]{"mkw","retro","retro-alt","miis","ratings"})roots.put(r,new File(base,r));
        SaveArchive.Profile p=new SaveArchive.Profile(KartPlayerData.ID,roots);
        for(String r:new String[]{"mkw","retro","retro-alt"})write(f(p,r,"rksys.dat"),save(1,"DriftDroid",123));
        write(f(p,"miis","RFL_DB.dat"),db());write(f(p,"miis","RFL_Res.dat"),new byte[]{4,3,2});
        write(f(p,"ratings","RRRating.pul"),rating(345,8000));write(f(p,"ratings","settings.pul"),new byte[]{1,2,3});
        yes(KartPlayerData.drivers(p,"retro-rewind").size()==4,"separate Retro save licenses visible");
        yes(KartPlayerData.drivers(p,"mkw").size()==2,"original licenses visible");yes(KartPlayerData.miis(p).size()==2,"existing Miis visible");
        reject(()->KartPlayerData.encodeName(""),"empty");reject(()->KartPlayerData.encodeName("  "),"blank");
        reject(()->KartPlayerData.encodeName("12345678901"),"long");reject(()->KartPlayerData.encodeName("a\nb"),"control");
        reject(()->KartPlayerData.encodeName("\ud800"),"unpaired high");reject(()->KartPlayerData.encodeName("\udc00"),"unpaired low");
        yes(KartPlayerData.encodeName("1234567890").length==20,"exact ten");yes(KartPlayerData.encodeName("Luïgi日本").length==14,"Unicode");
        yes(KartPlayerData.encodeName("A\ud83d\ude00").length==6,"surrogate pair");
        Map<String,String> original=hashes(p);byte[] before=read(f(p,"retro","rksys.dat"));
        final KartPlayerData.Driver driver=KartPlayerData.drivers(p,"retro-rewind").get(0);
        KartPlayerData.rename(p,driver,"Somber",work,backup(base,"before-name"));
        yes(!SaveArchive.pending(work),"journal completed");
        for(String r:new String[]{"mkw","retro","retro-alt"}){byte[] b=read(f(p,r,"rksys.dat"));KartPlayerData.validateSave(b);
            yes(name(b,8+0x14).equals("Somber"),r+" shared short name");yes(name(b,8+0x5684+2).equals("Somber"),r+" cached Mii name");
            yes(crc(b,8+0x5684,74)==((b[8+0x5684+74]&255)*256+(b[8+0x5684+75]&255)),r+" cached checksum");
            for(int i=0;i<b.length;i++)if(!(i>=8+0x14&&i<8+0x28)&&!(i>=8+0x5684+2&&i<8+0x5684+22)&&!(i>=8+0x5684+74&&i<8+0x5684+76)&&!(i>=0x27ffc&&i<0x28000))
                if(b[i]!=before[i])throw new AssertionError("unrelated save byte changed "+r+" "+i);
            yes(true,r+" all unrelated bytes (account, progress, other licenses, ghosts) exact");}
        byte[] renamedDb=read(f(p,"miis","RFL_DB.dat"));KartPlayerData.validateDatabase(renamedDb);yes(name(renamedDb,6).equals("Somber"),"linked database Mii renamed");
        byte[] oldDb=db();for(int i=0;i<oldDb.length;i++)if(!(i>=6&&i<26)&&!(i>=0x1f1de&&i<0x1f1e0))if(oldDb[i]!=renamedDb[i])throw new AssertionError("unrelated DB byte "+i);
        yes(true,"unrelated Mii bytes and faces exact");yes(Arrays.equals(read(f(p,"ratings","RRRating.pul")),rating(345,8000)),"ratings unchanged by rename");
        SaveArchive.Inventory inv=SaveArchive.verify(p,backup(base,"before-name"),null);yes(inv.files.get("retro/rksys.dat").hash.equals(original.get("retro/rksys.dat")),"before backup exact");
        Map<String,String> renamed=hashes(p);reject(()->KartPlayerData.rename(p,driver,"Stale",work,backup(base,"stale")),"stale save");yes(renamed.equals(hashes(p)),"stale refusal changes nothing");
        final KartPlayerData.Driver current=KartPlayerData.drivers(p,"retro-rewind").get(0);
        byte[] broken=read(f(p,"retro-alt","rksys.dat"));broken[0]^=1;write(f(p,"retro-alt","rksys.dat"),broken);Map<String,String> corrupt=hashes(p);
        reject(()->KartPlayerData.rename(p,current,"NoChange",work,backup(base,"corrupt")),"corrupt linked profile");yes(corrupt.equals(hashes(p)),"failure before publication is atomic");
        write(f(p,"retro-alt","rksys.dat"),read(f(p,"retro","rksys.dat")));
        byte[] orphan=read(f(p,"retro","rksys.dat"));put(orphan,8+0x28,0x80000009L,4);saveCrc(orphan);write(f(p,"retro","rksys.dat"),orphan);
        final KartPlayerData.Driver missing=KartPlayerData.drivers(p,"retro-rewind").get(0);Map<String,String> orphanHashes=hashes(p);
        reject(()->KartPlayerData.rename(p,missing,"Name",work,backup(base,"missing")),"missing Mii is not a fake rename");yes(orphanHashes.equals(hashes(p)),"missing link preserves all files");
        KartPlayerData.chooseMii(p,missing,KartPlayerData.miis(p).get(1),work,backup(base,"before-mii"));
        byte[] chosen=read(f(p,"retro","rksys.dat"));yes(name(chosen,8+0x14).equals("Other"),"selected local Mii name");yes(name(chosen,8+0x5684+2).equals("Other"),"cached full Mii updated");
        yes(Arrays.equals(Arrays.copyOfRange(chosen,8+0x40,8+0x80),Arrays.copyOfRange(orphan,8+0x40,8+0x80)),"online identity exact after Mii repair");
        yes(Arrays.equals(Arrays.copyOfRange(chosen,8+0x30,8+0x40),Arrays.copyOfRange(orphan,8+0x30,8+0x40)),"unlock flags exact after Mii repair");
        KartPlayerData.Driver ratingDriver=KartPlayerData.drivers(p,"retro-rewind").get(0);byte[] source=rating(123,1234.5f),sourceCopy=source.clone();
        KartPlayerData.importRatings(p,ratingDriver,source,work,backup(base,"before-ratings"));byte[] output=read(f(p,"ratings","RRRating.pul"));
        yes(Arrays.equals(Arrays.copyOfRange(output,8,24),Arrays.copyOfRange(rating(345,8000),8,24)),"other online account ratings exact");
        yes(Arrays.equals(Arrays.copyOfRange(output,24,40),Arrays.copyOfRange(source,8,24)),"matching PID rating merged");yes(Arrays.equals(source,sourceCopy),"source file bytes untouched");
        reject(()->KartPlayerData.importRatings(p,ratingDriver,rating(999,3),work,backup(base,"wrong-account")),"unrelated rating PID");
        byte[] nan=rating(123,Float.NaN);reject(()->KartPlayerData.importRatings(p,ratingDriver,nan,work,backup(base,"nan")),"nonfinite");
        reject(()->KartPlayerData.importRatings(p,ratingDriver,new byte[1641],work,backup(base,"oversized")),"oversized");
        byte[] duplicate=rating(123,3);System.arraycopy(duplicate,8,duplicate,24,16);reject(()->KartPlayerData.importRatings(p,ratingDriver,duplicate,work,backup(base,"dupe")),"duplicate account");
        KartPlayerData.Driver originalDriver=KartPlayerData.drivers(p,"mkw").get(0);reject(()->KartPlayerData.importRatings(p,originalDriver,source,work,backup(base,"original-rating")),"original cannot import Retro ratings");
        SaveArchive.restore(p,backup(base,"before-name"),work,backup(base,"before-undo"));yes(hashes(p).equals(original),"complete shared undo restores original bytes");
        System.out.println("PASS "+checks+" player checks; exact name-only changes, Mii repair, PID-matched rating merge, stale/corrupt refusal and journal-backed undo");
    }
}
