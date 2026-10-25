package com.driftdroid.android.saves;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.*;
import java.util.*;
import java.util.zip.CRC32;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** File-picker adapters for the eight default WiiDS profiles. No native runtime or root.
 * Wii SD export/GCI layouts: Dolphin HW/WiiSave{,Structs} and HW/GCMemcard/GCMemcard.h
 * (Dolphin Emulator Project, GPL-2.0-or-later). Console certificate authenticity is not asserted.
 * Destinations come only from SaveManagerProfiles; input filenames never become output paths. */
public final class DolphinSaveImport {
    private DolphinSaveImport() {}
    public static final int MAX_INPUT = 8 << 20;
    private static final int HEADER = 0xf0c0;
    private static final String CARD_NAME = "super_mario_sunshine";
    private static final Set<String> GAMES = new HashSet<>(Arrays.asList(
        "mkw","retro-rewind","nsmbw","msmbwii","sunshine","eclipse","galaxy","kaizo"));
    public static final class Plan {
        public final String game, format, description;
        private final byte[] payload, stat;
        private Plan(String game,String format,String description,byte[] payload,byte[] stat) {
            this.game=game;this.format=format;this.description=description;this.payload=payload.clone();
            this.stat=stat==null?null:stat.clone();
        }
    }
    private static void require(boolean ok,String why) throws IOException { if(!ok)throw new IOException(why); }
    private static long n(byte[] b,int p,int w){return GalaxySaveCodec.number(b,p,w,true);}
    private static String ascii(byte[] b,int p,int len){return new String(b,p,len,StandardCharsets.US_ASCII);}
    private static byte[] hex(String s){byte[] b=new byte[s.length()/2];for(int i=0;i<b.length;i++)b[i]=(byte)Integer.parseInt(s.substring(i*2,i*2+2),16);return b;}
    public static byte[] read(InputStream input) throws IOException {
        ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buf=new byte[32768];int count;
        while((count=input.read(buf))!=-1){require(out.size()+count<=MAX_INPUT,"Save file is too large. Select one game's exported save, not a NAND backup or memory card.");out.write(buf,0,count);}
        return out.toByteArray();
    }
    public static String instructions(String game) {
        switch(game){
            case "sunshine": return "In Dolphin's Memory Card Manager, export the USA Sunshine save as a .gci file (GMSE01).";
            case "eclipse": return "In Dolphin's Memory Card Manager, export the Eclipse game save as a .gci file (GMSE04). Select super_mario_sunshine, not the mod's settings file.";
            case "mkw": case "retro-rewind": return "Select rksys.dat from Dolphin's save folder, or a PAL RMCP Wii save exported as data.bin.";
            case "nsmbw": case "msmbwii": return "Select wiimj2d.sav from Dolphin's save folder, or a USA SMNE Wii save exported as data.bin.";
            case "galaxy": case "kaizo": return "Select GameData.bin from Dolphin's save folder, or a Galaxy Wii save exported as data.bin. WiiDS converts the save automatically.";
            default: return "Choose a supported game first.";
        }
    }
    public static Plan inspect(String game,byte[] bytes) throws IOException {
        require(GAMES.contains(game),"Unknown save profile.");
        require(bytes.length>0 && bytes.length<=MAX_INPUT,"Empty or oversized save file.");
        if(game.equals("sunshine") || game.equals("eclipse")) return gci(game,bytes);
        byte[] payload=bytes;String format="Dolphin NAND save file";
        // Raw saves have known, disjoint lengths. Everything else must be a valid Wii SD export.
        int rawSize=game.equals("mkw")||game.equals("retro-rewind")?0x2bc000:
                    game.equals("nsmbw")||game.equals("msmbwii")?0x3fa0:0xbe00;
        if(bytes.length!=rawSize){payload=wiiExport(game,bytes);format="Wii data.bin export";}
        if(game.equals("mkw")||game.equals("retro-rewind")) {
            require(payload.length==0x2bc000 && ascii(payload,0,8).equals("RKSD0006"),"This is not a supported Mario Kart Wii rksys.dat save.");
            CRC32 crc=new CRC32();crc.update(payload,0,0x27ffc);
            require(crc.getValue()==n(payload,0x27ffc,4),"Mario Kart save checksum failed.");
        } else if(game.equals("nsmbw")||game.equals("msmbwii")) {
            require(payload.length==0x3fa0 && ascii(payload,0,4).equals("SMNE"),"Select a USA SMNE wiimj2d.sav save for this game.");
            validateNsmb(payload);
        } else payload=GalaxySaveCodec.toNative(payload);
        String description="Import the main in-game save. Manual save slots and game settings are kept.\n\n"+
            "The save format cannot distinguish an original from its mod. Make sure this save was made for the selected game.";
        return new Plan(game,format,description,payload,null);
    }
    private static byte[] aes(byte[] bytes,int start,int size,byte[] iv) throws IOException {
        try {
            Cipher cipher=Cipher.getInstance("AES/CBC/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE,new SecretKeySpec(hex("ab01b9d8e1622b08afbad84dbfc2a55d"),"AES"),new IvParameterSpec(iv));
            return cipher.doFinal(bytes,start,size);
        }catch(GeneralSecurityException|IllegalArgumentException e){throw new IOException("Could not decode Wii save export.",e);}
    }
    private static byte[] wiiExport(String game,byte[] b) throws IOException {
        require(b.length>=HEADER+0x3c0,"Unsupported save file. "+instructions(game));
        byte[] h=aes(b,0,HEADER,hex("216712e6aa1f689f95c5a22324dc6a98"));
        long title=n(h,0,8);int banner=(int)n(h,8,4);
        require(banner>=0x72a0 && banner<=0xf0a0 && (banner-0x60a0)%0x1200==0 && ascii(h,32,4).equals("WIBN"),"Invalid Wii save header.");
        byte[] md5=Arrays.copyOfRange(h,14,30);System.arraycopy(hex("0e65378199be4517ab06ec22451a5793"),0,h,14,16);
        try{require(MessageDigest.isEqual(md5,MessageDigest.getInstance("MD5").digest(h)),"Wii save header checksum failed.");}
        catch(NoSuchAlgorithmException e){throw new AssertionError(e);}
        boolean kart=game.equals("mkw")||game.equals("retro-rewind"),nsmb=game.equals("nsmbw")||game.equals("msmbwii");
        require(kart?title==0x00010004524d4350L:nsmb?title==0x00010000534d4e45L:
            (title==0x00010000524d4745L||title==0x00010000524d4750L||title==0x00010000524d474aL||title==0x00010000524d474bL),
            "Wii export is for another game or unsupported region. "+instructions(game));
        int k=HEADER;
        long count=n(b,k+12,4),length=n(b,k+16,4);
        require(n(b,k,4)==0x70 && n(b,k+4,4)==0x426b0001L && n(b,k+96,8)==title &&
            count>0 && count<=32 && length<=MAX_INPUT && n(b,k+28,4)==length+0x3c0 && HEADER+length+0x3c0==b.length,"Invalid Wii backup table.");
        String wanted=kart?"rksys.dat":nsmb?"wiimj2d.sav":"GameData.bin";
        int p=k+0x80,end=p+(int)length;byte[] payload=null;Set<String> names=new HashSet<>();
        for(int i=0;i<count;i++) {
            require(p+0x80<=end && n(b,p,4)==0x03adf17e,"Invalid Wii save file table.");
            long size=n(b,p+4,4);int type=b[p+10]&255;
            int len=0;while(len<64 && b[p+11+len]!=0)len++;
            require(len>0 && len<64,"Invalid Wii save filename.");String name=ascii(b,p+11,len);
            // These games need one flat payload. Refuse complete NAND trees and unexpected extras.
            require(type==1 && (name.equals(wanted)||name.equals("banner.bin")) && names.add(name),"Unexpected or duplicate file in Wii save export.");
            require(size>0 && size<=MAX_INPUT,"Invalid Wii save file size.");int rounded=(int)((size+63)&~63L);
            require(rounded<=end-p-0x80,"Truncated Wii save export.");
            byte[] plain=aes(b,p+0x80,rounded,Arrays.copyOfRange(b,p+0x50,p+0x60));
            if(name.equals(wanted))payload=Arrays.copyOf(plain,(int)size);
            p+=0x80+rounded;
        }
        require(p==end && payload!=null,"Wii export is missing its game save.");return payload;
    }
    private static void validateNsmb(byte[] b) throws IOException {
        // Filled in-game records carry the game's CRC; the header is checked separately.
        // Exact ranges follow dSaveMng_c::calcHeaderCRC/calcCRC (SMNE E1).
        require(nsmbCrc(b,4,0x698)==n(b,0x69c,4),"NSMB save header checksum failed.");
        for(int i=0;i<6;i++) {int p=0x6a0+i*0x980;
            require(nsmbCrc(b,p,0x97c)==n(b,p+0x97c,4),"NSMB save slot checksum failed.");}
    }
    private static long nsmbCrc(byte[] b,int p,int length) {
        CRC32 crc=new CRC32();crc.update(b,p,length);return crc.getValue();
    }
    private static Plan gci(String game,byte[] b) throws IOException {
        String id=game.equals("sunshine")?"GMSE01":"GMSE04";
        require(b.length==64+7*8192 && ascii(b,0,6).equals(id),"Select a seven-block "+id+" game save exported as .gci. "+instructions(game));
        require(n(b,56,2)==7 && ascii(b,8,CARD_NAME.length()).equals(CARD_NAME) && b[8+CARD_NAME.length()]==0,"GCI is not the main Sunshine/Eclipse game save.");
        byte[] payload=Arrays.copyOfRange(b,64,b.length);
        boolean[] valid=new boolean[7];
        for(int block=0;block<7;block++) {
            int sum=0,inv=0,p=block*8192;
            for(int j=0;j<8192-4;j+=2){int w=(int)n(payload,p+j,2);sum+=w;inv+=(~w)&65535;}
            valid[block]=(((sum&65535L)<<16)|(inv&65535L))==n(payload,p+8192-4,4);
        }
        require(valid[0] && (valid[1]||valid[2]) && (valid[3]||valid[4]) && (valid[5]||valid[6]),"GCI has no valid copy of one or more save slots.");
        byte[] stat=new byte[108]; // Native CARDStat fields, not the 64-byte GCI directory entry.
        System.arraycopy(b,8,stat,0,32);System.arraycopy(b,0,stat,40,6);stat[46]=b[7];
        GalaxySaveCodec.put(stat,32,payload.length,4,false);
        GalaxySaveCodec.put(stat,36,n(b,40,4),4,false);
        GalaxySaveCodec.put(stat,48,n(b,44,4),4,false);
        GalaxySaveCodec.put(stat,52,n(b,48,2),2,false);
        GalaxySaveCodec.put(stat,54,n(b,50,2),2,false);
        GalaxySaveCodec.put(stat,56,n(b,60,4),4,false);
        long image=n(b,44,4),comment=n(b,60,4);
        require((image==0xffffffffL||image<payload.length) && (comment==0xffffffffL||comment<=payload.length-64),"GCI image/comment offsets are outside the save.");
        return new Plan(game,"GameCube .gci export","Import all three in-game save slots. Existing card entries and mod settings are kept.",payload,stat);
    }
    /** Called only AFTER user confirmation, with the same stopped-game/file lock as Restore.
     * Merge against a fresh verified snapshot, then use the existing journal/rollback transaction.
     * No input-dependent path is ever passed to a filesystem API. */
    public static void restore(SaveArchive.Profile profile,Plan plan,File work,File previous) throws IOException {
        require(profile.id.equals(plan.game),"The selected game changed. Choose the save again.");
        require(!SaveArchive.pending(work),"Recover the interrupted restore first.");
        File stage=new File(work,"dolphin-"+UUID.randomUUID());SaveArchive.mkdir(stage);
        try {
            File snapshot=new File(stage,"current.zip"),files=new File(stage,"files");SaveArchive.mkdir(files);
            SaveArchive.backup(profile,snapshot,true);
            SaveArchive.Inventory before=SaveArchive.verify(profile,snapshot,files);
            LinkedHashMap<String,File> roots=new LinkedHashMap<>();
            for(String key:profile.roots.keySet()){
                File root=new File(files,key);roots.put(key,root);
                if(Boolean.TRUE.equals(before.roots.get(key)))SaveArchive.mkdir(root);
            }
            SaveArchive.Profile merged=new SaveArchive.Profile(profile.id,roots);
            if(plan.stat!=null){
                File main=root(merged,"main");String name=plan.game.equals("eclipse")?"GMSE04_"+CARD_NAME:CARD_NAME;
                File index=new File(main,"index.txt");List<String> lines=new ArrayList<>();
                if(index.exists()) {
                    require(index.length()<=32768,"Card index is too large.");
                    lines.addAll(Arrays.asList(new String(Files.readAllBytes(index.toPath()),StandardCharsets.US_ASCII).split("\n",-1)));
                    if(!lines.isEmpty() && lines.get(lines.size()-1).isEmpty())lines.remove(lines.size()-1);
                }
                require(lines.size()<=127,"Card index has too many entries.");Set<String> seen=new HashSet<>();
                for(String line:lines)require(line.isEmpty()||(line.matches("[A-Za-z0-9_.-]{1,80}") && !line.equals(".") && !line.equals("..") && seen.add(line)),"Invalid existing card index. Restore a WiiDS backup instead.");
                if(!lines.contains(name)) {int empty=lines.indexOf("");if(empty>=0)lines.set(empty,name);else {require(lines.size()<127,"The virtual memory card is full.");lines.add(name);}}
                long blocks=7;
                for(String line:lines)if(!line.isEmpty()&&!line.equals(name)) {
                    File other=new File(main,line+".dat");
                    if(other.isFile())blocks+=(other.length()+8191)/8192;
                }
                require(blocks<=59,"The virtual memory card has insufficient free blocks. Your saves are unchanged.");
                write(new File(main,name+".dat"),plan.payload);write(new File(main,name+".stat"),plan.stat);
                write(index,(String.join("\n",lines)+"\n").getBytes(StandardCharsets.US_ASCII));
            } else if(plan.game.equals("galaxy")||plan.game.equals("kaizo")) {
                for(String title:Arrays.asList("524d4750","524d4745","524d474a","524d474b")) {
                    File data=new File(root(merged,"main"),"00010000/"+title+"/data");
                    write(new File(data,"GameData.bin"),plan.payload);write(new File(data,"save.bin"),plan.payload);
                }
                write(new File(root(merged,"mirror"),"GameData.bin"),plan.payload);
            } else {
                String name=plan.game.equals("mkw")||plan.game.equals("retro-rewind")?"rksys.dat":"wiimj2d.sav";
                write(new File(root(merged,"main"),name),plan.payload);
                if(plan.game.equals("retro-rewind"))write(new File(root(merged,"alternate"),name),plan.payload);
            }
            File archive=new File(stage,"merged.zip");SaveArchive.backup(merged,archive,false);
            SaveArchive.restore(profile,archive,work,previous);
        } finally {erase(stage);}
    }
    private static File root(SaveArchive.Profile p,String key) throws IOException {
        File root=p.roots.get(key);require(root!=null,"Unsupported save profile layout.");SaveArchive.mkdir(root);return root;
    }
    private static void write(File file,byte[] b) throws IOException {
        SaveArchive.safe(file);SaveArchive.mkdir(file.getParentFile());
        try(FileOutputStream out=new FileOutputStream(file)){out.write(b);out.getFD().sync();}
    }
    private static void erase(File file) throws IOException {
        SaveArchive.safe(file);if(!file.exists())return;
        if(file.isDirectory()){File[] entries=file.listFiles();require(entries!=null,"Cannot clean import staging.");for(File child:entries)erase(child);}
        Files.delete(file.toPath());
    }
}
