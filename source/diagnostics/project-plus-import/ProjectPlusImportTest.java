package dev.wiids.brawl;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.*;

/** Synthetic FAT/GCT fixtures exercise real production I/O; no game data is included. */
public class ProjectPlusImportTest {
    static int checks=0, symlinkChecks=0;
    static final ProjectPlusImport.Progress QUIET=(phase,bytes)->{};
    static final String JOURNAL=".project-plus-import.properties";
    static final byte[] CARD=card(1), NEW=card(2), CODE=codes(1), NEWCODE=codes(2);
    interface Work { void run() throws Exception; }
    static class Crash extends Error {}
    static void check(boolean value,String message) { checks++;if(!value)throw new AssertionError(message); }
    static void same(byte[] a,Path b)throws Exception { check(Arrays.equals(a,Files.readAllBytes(b)),"changed: "+b); }
    static void rejects(Work work)throws Exception { try {work.run();throw new AssertionError("accepted invalid input");}catch(IOException expected){checks++;} }
    static void put16(byte[] a,int at,int n){a[at]=(byte)n;a[at+1]=(byte)(n>>8);}
    static void put32(byte[] a,int at,long n){put16(a,at,(int)n);put16(a,at+2,(int)(n>>16));}
    public static byte[] card(int marker) {
        byte[] a=new byte[6144*512];put16(a,11,512);a[13]=1;put16(a,14,1);a[16]=2;
        put16(a,17,512);put16(a,19,6144);a[21]=(byte)0xf8;put16(a,22,24);
        a[510]=0x55;a[511]=(byte)0xaa;a[2048]=(byte)marker;return a;
    }
    public static byte[] codes(int marker) {
        return new byte[]{0,(byte)0xd0,(byte)0xc0,(byte)0xde,0,(byte)0xd0,(byte)0xc0,(byte)0xde,
            4,0,0,(byte)marker,0,0,0,0,(byte)0xf0,0,0,0,0,0,0,0};
    }
    static Path root()throws Exception{return Files.createTempDirectory("pplus-").resolve("project-plus");}
    static Path write(Path p,byte[] b)throws Exception {Files.createDirectories(p.getParent());Files.write(p,b);return p;}
    static void pair(Path r,byte[] a,byte[] b)throws Exception{write(r.resolve("sd.raw"),a);write(r.resolve("Codes/RSBE01.gct"),b);}
    static void importPair(Path r)throws Exception{ProjectPlusImport.importFiles(r.toFile(),new ByteArrayInputStream(NEW),new ByteArrayInputStream(NEWCODE),()->false,QUIET);}
    static void crash(Path r,String step)throws Exception {
        try { ProjectPlusImport.importFiles(r.toFile(),new ByteArrayInputStream(NEW),new ByteArrayInputStream(NEWCODE),()->false,QUIET,
            name->{if(name.equals(step))throw new Crash();});throw new AssertionError("no injected crash"); }catch(Crash expected){checks++;}
    }
    static byte[] zip(String...names)throws Exception {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(ZipOutputStream z=new ZipOutputStream(bytes)){for(String name:names){z.putNextEntry(new ZipEntry(name));z.write(name.toLowerCase(Locale.ROOT).endsWith("sd.raw")?NEW:NEWCODE);z.closeEntry();}}
        return bytes.toByteArray();
    }
    static void zipImport(Path r,byte[] b)throws Exception{ProjectPlusImport.importZip(r.toFile(),new ByteArrayInputStream(b),()->false,QUIET);}
    static void noStage(Path r){check(!Files.exists(r.resolveSibling(".project-plus-import-stage")),"stage retained");}
    static void protectedData(Path r)throws Exception{write(r.resolve("NAND/keep"),new byte[]{7});write(r.resolve("Config/keep"),new byte[]{8});write(r.resolveSibling("brawl").resolve("game/keep"),new byte[]{9});}
    static void protectedSame(Path r)throws Exception{same(new byte[]{7},r.resolve("NAND/keep"));same(new byte[]{8},r.resolve("Config/keep"));same(new byte[]{9},r.resolveSibling("brawl").resolve("game/keep"));}
    public static void main(String[] args)throws Exception {
        Path r=root();pair(r,CARD,CODE);protectedData(r);importPair(r);
        same(NEW,r.resolve("sd.raw"));same(NEWCODE,r.resolve("Codes/RSBE01.gct"));protectedSame(r);noStage(r);
        try(var list=Files.list(r.resolve("ImportBackups"))){Path backup=list.findFirst().orElseThrow();same(CARD,backup.resolve("sd.raw"));same(CODE,backup.resolve("RSBE01.gct"));}
        check(ProjectPlusFiles.ready(r.toFile()),"ready after import");
        Path internal=root().resolve("data");importPair(internal);same(NEW,internal.resolve("sd.raw"));
        Path zipped=root();zipImport(zipped,zip("Project+ v3/Dolphin/sd.raw","Project+ v3/Codes/RSBE01.gct","README.txt"));
        same(NEW,zipped.resolve("sd.raw"));same(NEWCODE,zipped.resolve("Codes/RSBE01.gct"));noStage(zipped);
        Path windows=root();zipImport(windows,zip("Pack\\SD.RAW","Pack\\Codes\\rsbe01.GCT"));same(NEW,windows.resolve("sd.raw"));
        for(String step:new String[]{"prepared","backup-card","backup-codes","publish-codes","publish-card","committed","finished"}) {
            for(int present=0;present<4;present++) {
                Path c=root();protectedData(c);if((present&1)!=0)write(c.resolve("sd.raw"),CARD);if((present&2)!=0)write(c.resolve("Codes/RSBE01.gct"),CODE);
                crash(c,step);boolean committed=step.equals("committed")||step.equals("finished");
                if(ProjectPlusImport.pending(c.toFile()))check(!ProjectPlusFiles.ready(c.toFile()),"pending must not be ready");
                ProjectPlusImport.recover(c.toFile());ProjectPlusImport.recover(c.toFile());
                if(committed){same(NEW,c.resolve("sd.raw"));same(NEWCODE,c.resolve("Codes/RSBE01.gct"));}
                else {if((present&1)!=0)same(CARD,c.resolve("sd.raw"));else check(!Files.exists(c.resolve("sd.raw")),"unexpected card");
                    if((present&2)!=0)same(CODE,c.resolve("Codes/RSBE01.gct"));else check(!Files.exists(c.resolve("Codes/RSBE01.gct")),"unexpected codes");}
                protectedSame(c);check(!ProjectPlusImport.pending(c.toFile()),"journal retained");noStage(c);
            }
        }
        // A recovery which was itself interrupted after restoring the card is restartable.
        Path repeat=root();pair(repeat,CARD,CODE);crash(repeat,"publish-card");
        Path back;try(var list=Files.list(repeat.resolve("ImportBackups"))){back=list.findFirst().orElseThrow();}
        Files.delete(repeat.resolve("sd.raw"));Files.move(back.resolve("sd.raw"),repeat.resolve("sd.raw"));
        ProjectPlusImport.recover(repeat.toFile());same(CARD,repeat.resolve("sd.raw"));same(CODE,repeat.resolve("Codes/RSBE01.gct"));
        // Never overwrite files edited after a crash; validate both targets before restoring either.
        Path edited=root();pair(edited,CARD,CODE);crash(edited,"publish-card");write(edited.resolve("Codes/RSBE01.gct"),new byte[]{99});
        rejects(()->ProjectPlusImport.recover(edited.toFile()));same(NEW,edited.resolve("sd.raw"));same(new byte[]{99},edited.resolve("Codes/RSBE01.gct"));check(ProjectPlusImport.pending(edited.toFile()),"unknown edits need review");
        for(String phase:new String[]{"Copying SD image","Copying codes","Reading Project+ ZIP","Extracting SD image","Extracting codes"}) {
            Path c=root();pair(c,CARD,CODE);AtomicBoolean cancel=new AtomicBoolean();
            ProjectPlusImport.Progress progress=(label,bytes)->{if(label.equals(phase))cancel.set(true);};
            rejects(()->{if(phase.startsWith("Copying"))ProjectPlusImport.importFiles(c.toFile(),new ByteArrayInputStream(NEW),new ByteArrayInputStream(NEWCODE),cancel::get,progress);
                else ProjectPlusImport.importZip(c.toFile(),new ByteArrayInputStream(zip("sd.raw","Codes/RSBE01.gct")),cancel::get,progress);});
            same(CARD,c.resolve("sd.raw"));same(CODE,c.resolve("Codes/RSBE01.gct"));noStage(c);
        }
        Path failed=root();pair(failed,CARD,CODE);
        rejects(()->ProjectPlusImport.importFiles(failed.toFile(),new InputStream(){public int read()throws IOException{throw new IOException("provider failed");}},new ByteArrayInputStream(NEWCODE),()->false,QUIET));
        same(CARD,failed.resolve("sd.raw"));same(CODE,failed.resolve("Codes/RSBE01.gct"));noStage(failed);
        for(byte[] bad:new byte[][]{new byte[0],new byte[512],Arrays.copyOf(NEW,512),new byte[2*1024*1024]}) {
            Path c=root();pair(c,CARD,CODE);rejects(()->ProjectPlusImport.importFiles(c.toFile(),new ByteArrayInputStream(bad),new ByteArrayInputStream(NEWCODE),()->false,QUIET));same(CARD,c.resolve("sd.raw"));same(CODE,c.resolve("Codes/RSBE01.gct"));noStage(c);
        }
        for(byte[] bad:new byte[][]{new byte[0],new byte[16],Arrays.copyOf(NEWCODE,23),Arrays.copyOf(NEWCODE,16),new byte[(32<<20)+1]}) {
            Path c=root();pair(c,CARD,CODE);rejects(()->ProjectPlusImport.importFiles(c.toFile(),new ByteArrayInputStream(NEW),new ByteArrayInputStream(bad),()->false,QUIET));same(CARD,c.resolve("sd.raw"));same(CODE,c.resolve("Codes/RSBE01.gct"));noStage(c);
        }
        for(String[] names:new String[][]{{"sd.raw"},{"sd.raw","RSBE01.gct","another/SD.RAW"},{"sd.raw","RSBE01.gct","../keep"},{"/sd.raw","RSBE01.gct"},{"sd.raw","C:\\RSBE01.gct"},{"sd.raw","RSBE01.gct","other/rsbe01.GCT"}}){
            Path c=root();pair(c,CARD,CODE);rejects(()->zipImport(c,zip(names)));same(CARD,c.resolve("sd.raw"));same(CODE,c.resolve("Codes/RSBE01.gct"));noStage(c);
        }
        rejects(()->zipImport(root(),new byte[]{1,2,3}));
        // ZIP payload corruption with unchanged size/central CRC must not pass structural FAT checks.
        ByteArrayOutputStream storedBytes=new ByteArrayOutputStream();
        try(ZipOutputStream z=new ZipOutputStream(storedBytes)){
            for(String name:new String[]{"sd.raw","RSBE01.gct"}){byte[] payload=name.equals("sd.raw")?NEW:NEWCODE;CRC32 crc=new CRC32();crc.update(payload);ZipEntry e=new ZipEntry(name);e.setMethod(ZipEntry.STORED);e.setSize(payload.length);e.setCrc(crc.getValue());z.putNextEntry(e);z.write(payload);z.closeEntry();}
        }
        byte[] damaged=storedBytes.toByteArray();damaged[30+"sd.raw".length()+2048]^=4;
        rejects(()->zipImport(root(),damaged));
        // A killed staging worker has no journal yet; startup still reclaims only its known files.
        Path orphan=root(),orphanWork=orphan.resolveSibling(".project-plus-import-stage");pair(orphan,CARD,CODE);write(orphanWork.resolve("archive.zip"),new byte[]{1});write(orphanWork.resolve("sd.raw"),new byte[]{2});
        ProjectPlusImport.recover(orphan.toFile());noStage(orphan);same(CARD,orphan.resolve("sd.raw"));same(CODE,orphan.resolve("Codes/RSBE01.gct"));
        Path orphanUnknown=root(),orphanUnknownWork=orphanUnknown.resolveSibling(".project-plus-import-stage");write(orphanUnknownWork.resolve("keep"),new byte[]{4});
        rejects(()->ProjectPlusImport.recover(orphanUnknown.toFile()));same(new byte[]{4},orphanUnknownWork.resolve("keep"));
        for(boolean published:new boolean[]{false,true}) {
            Path torn=root();pair(torn,CARD,CODE);
            if(published)crash(torn,"publish-card");else write(torn.resolveSibling(".project-plus-import-stage").resolve("sd.raw"),new byte[]{4});
            write(torn.resolve(JOURNAL+".new"),new byte[]{'i','d','='});
            ProjectPlusImport.recover(torn.toFile());same(CARD,torn.resolve("sd.raw"));same(CODE,torn.resolve("Codes/RSBE01.gct"));noStage(torn);
            check(!Files.exists(torn.resolve(JOURNAL+".new")),"scratch not moved");check(!ProjectPlusImport.pending(torn.toFile()),"torn scratch prevented recovery");
            try(var saved=Files.walk(torn.resolve("ImportBackups"))){Path partial=saved.filter(p->p.getFileName().toString().equals("interrupted-journal.properties")).findFirst().orElseThrow();same(new byte[]{'i','d','='},partial);}
        }
        // MBR partition bounds and FAT geometry, including malicious unsigned values.
        byte[] mbr=new byte[NEW.length+512];System.arraycopy(NEW,0,mbr,512,NEW.length);mbr[450]=6;put32(mbr,454,1);put32(mbr,458,6144);mbr[510]=0x55;mbr[511]=(byte)0xaa;
        Path m=write(root().resolve("sd.raw"),mbr);ProjectPlusImport.validateCard(m);checks++;
        put32(mbr,454,0xffffffffL);Files.write(m,mbr);rejects(()->ProjectPlusImport.validateCard(m));
        for(int offset:new int[]{11,13,14,16,17,19,22}){byte[] bad=NEW.clone();bad[offset]=0;bad[offset+1]=0;Files.write(m,bad);rejects(()->ProjectPlusImport.validateCard(m));}
        for(String field:new String[]{"id=../escape","state=unknown","oldCard=true\noldCard=true","newCard=invalid","oldCodeHash="}) {
            Path c=root();pair(c,CARD,CODE);crash(c,"publish-card");Path journal=c.resolve(JOURNAL);
            String key=field.substring(0,field.indexOf('='));String text=Files.readString(journal).replaceAll("(?m)^"+key+"=.*$",field);Files.writeString(journal,text);
            rejects(()->ProjectPlusImport.recover(c.toFile()));same(NEW,c.resolve("sd.raw"));same(NEWCODE,c.resolve("Codes/RSBE01.gct"));
        }
        Path unknown=root();Path stage=unknown.resolveSibling(".project-plus-import-stage");write(stage.resolve("do-not-remove"),new byte[]{5});
        rejects(()->importPair(unknown));same(new byte[]{5},stage.resolve("do-not-remove"));
        try {
            Path c=root(),outside=Files.createTempDirectory("pplus-outside-");Files.createDirectories(c);write(outside.resolve("keep"),new byte[]{5});
            Files.createSymbolicLink(c.resolve("Codes"),outside);rejects(()->importPair(c));same(new byte[]{5},outside.resolve("keep"));symlinkChecks++;
            Path l=root();Files.createSymbolicLink(l.resolveSibling(".project-plus-setup.lock"),outside.resolve("keep"));rejects(()->importPair(l));same(new byte[]{5},outside.resolve("keep"));symlinkChecks++;
        }catch(FileSystemException|UnsupportedOperationException unavailable){System.out.println("Symlink fixtures unavailable: "+unavailable);}
        // Existing legacy setup still copies only its own card/codes/save and leaves Brawl intact.
        Path legacy=root(),brawl=legacy.resolveSibling("brawl");pair(brawl,CARD,CODE);write(brawl.resolve(ProjectPlusFiles.SAVE).resolve("save.dat"),new byte[]{6});
        ProjectPlusFiles.prepare(brawl.toFile(),legacy.toFile());same(CARD,legacy.resolve("sd.raw"));same(CODE,legacy.resolve("Codes/RSBE01.gct"));same(new byte[]{6},legacy.resolve(ProjectPlusFiles.SAVE).resolve("save.dat"));same(CARD,brawl.resolve("sd.raw"));
        // A pending/malformed new import must not fall back to legacy and overwrite recovery files.
        Path blocked=root(),old=blocked.resolveSibling("brawl");pair(old,CARD,CODE);pair(blocked,CARD,CODE);crash(blocked,"publish-card");Files.writeString(blocked.resolve(JOURNAL),"broken");
        rejects(()->ProjectPlusFiles.prepare(old.toFile(),blocked.toFile()));same(NEW,blocked.resolve("sd.raw"));same(NEWCODE,blocked.resolve("Codes/RSBE01.gct"));
        System.out.println("PASS "+checks+" checks; symlink fixtures="+symlinkChecks);
    }
}
