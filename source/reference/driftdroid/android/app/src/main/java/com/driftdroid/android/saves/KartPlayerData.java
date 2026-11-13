package com.driftdroid.android.saves;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.CRC32;

/** Stopped-game player edits. Account IDs, progress, ghosts and console identity are never rewritten.
 * Layout references: rr-pulsar GameSource/MarioKartWii/RKSYS/RKPD.hpp, core/rvl/RFL;
 * RatingSave.cpp (RRRT v1). Every publication uses SaveArchive's existing journal/recovery. */
public final class KartPlayerData {
    public static final String ID = "kart-players";
    private static final int SAVE_BYTES=0x2bc000, SAVE_CRC=0x27ffc, LICENSE_BYTES=0x8cc0;
    private static final int DB_BYTES=779968, DB_CRC=0x1f1de, RECORD_BYTES=74;
    private static final List<String> SAVES=Arrays.asList("mkw","retro","retro-alt");
    private KartPlayerData() {}
    public static final class Driver {
        public final String root, name, saveHash;
        public final int slot;
        private final byte[] id;
        Driver(String root,int slot,byte[] save) throws IOException {
            this.root=root;this.slot=slot;int p=8+slot*LICENSE_BYTES;
            name=name(save,p+0x14);id=Arrays.copyOfRange(save,p+0x28,p+0x30);saveHash=hash(save);
        }
    }
    public static final class Mii {
        public final int slot;
        public final String name;
        private final byte[] id;
        Mii(byte[] db,int slot){this.slot=slot;int p=4+slot*RECORD_BYTES;
            name=name(db,p+2);id=Arrays.copyOfRange(db,p+0x18,p+0x20);}
    }
    private static void require(boolean ok,String message) throws IOException {if(!ok)throw new IOException(message);}
    private static long number(byte[] b,int p,int n){long v=0;for(int i=0;i<n;i++)v=(v<<8)|(b[p+i]&255);return v;}
    private static void put(byte[] b,int p,long value,int n){for(int i=n-1;i>=0;i--){b[p+i]=(byte)value;value>>>=8;}}
    private static String ascii(byte[] b,int p,int n){return new String(b,p,n,StandardCharsets.US_ASCII);}
    private static String name(byte[] b,int p){int n=0;while(n<20 && number(b,p+n,2)!=0)n+=2;return new String(b,p,n,StandardCharsets.UTF_16BE);}
    private static boolean nonzero(byte[] b,int p,int n){for(int i=0;i<n;i++)if(b[p+i]!=0)return true;return false;}
    private static String hash(byte[] b) throws IOException {
        try {return Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-256").digest(b));}
        catch(java.security.NoSuchAlgorithmException e){throw new IOException(e);}
    }
    private static int crc16(byte[] b,int p,int n){int crc=0;for(int i=p;i<p+n;i++){crc^=(b[i]&255)<<8;
        for(int bit=0;bit<8;bit++)crc=((crc&0x8000)!=0?(crc<<1)^0x1021:crc<<1)&65535;}return crc;}
    private static void saveCrc(byte[] b){CRC32 crc=new CRC32();crc.update(b,0,SAVE_CRC);put(b,SAVE_CRC,crc.getValue(),4);}
    public static void validateSave(byte[] b) throws IOException {
        require(b.length==SAVE_BYTES && ascii(b,0,8).equals("RKSD0006"),"Unsupported Mario Kart player save.");
        CRC32 crc=new CRC32();crc.update(b,0,SAVE_CRC);require(crc.getValue()==number(b,SAVE_CRC,4),"Player save checksum failed.");
    }
    public static void validateDatabase(byte[] b) throws IOException {
        require(b.length==DB_BYTES && ascii(b,0,4).equals("RNOD") && crc16(b,0,DB_CRC)==number(b,DB_CRC,2),
            "The Mii database is missing or invalid. Start Mario Kart once, or import your Mii database in Options > Mii Data.");
    }
    private static byte[] read(File f,int max) throws IOException {
        SaveArchive.safe(f);require(f.isFile() && f.length()<=max,"Missing or oversized player file: "+f.getName());
        try(InputStream in=new FileInputStream(f)){ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buf=new byte[32768];int n;
            while((n=in.read(buf))!=-1){require(out.size()+n<=max,"Player file is too large.");out.write(buf,0,n);}return out.toByteArray();}
    }
    private static File file(SaveArchive.Profile p,String key,String name) throws IOException {
        require(p.id.equals(ID) && p.roots.containsKey(key),"Unsupported player profile layout.");
        File f=new File(p.roots.get(key),name);SaveArchive.safe(f);return f;
    }
    private static boolean active(byte[] b,int slot){return ascii(b,8+slot*LICENSE_BYTES,4).equals("RKPD");}
    public static List<Driver> drivers(SaveArchive.Profile p,String game) throws IOException {
        require(game.equals("mkw")||game.equals("retro-rewind"),"Choose a kart game.");
        List<Driver> result=new ArrayList<>();
        for(String root:game.equals("mkw")?Collections.singletonList("mkw"):Arrays.asList("retro","retro-alt")) {
            File f=file(p,root,"rksys.dat");if(!f.exists())continue;byte[] b=read(f,SAVE_BYTES);validateSave(b);
            for(int slot=0;slot<4;slot++)if(active(b,slot))result.add(new Driver(root,slot,b));
        }return result;
    }
    public static List<Mii> miis(SaveArchive.Profile p) throws IOException {
        byte[] db=read(file(p,"miis","RFL_DB.dat"),DB_BYTES);validateDatabase(db);List<Mii> result=new ArrayList<>();
        for(int slot=0;slot<100;slot++)if(nonzero(db,4+slot*RECORD_BYTES,RECORD_BYTES))result.add(new Mii(db,slot));return result;
    }
    public static byte[] encodeName(String name) throws IOException {
        require(name!=null && !name.trim().isEmpty() && name.length()<=10,"Use 1 to 10 characters for the driver name.");
        for(int i=0;i<name.length();i++){
            char c=name.charAt(i);require(!Character.isISOControl(c) && c!=0xfffe && c!=0xffff,"The name contains an unsupported character.");
            if(Character.isHighSurrogate(c)){require(i+1<name.length() && Character.isLowSurrogate(name.charAt(++i)),"Incomplete Unicode character in name.");}
            else require(!Character.isLowSurrogate(c),"Incomplete Unicode character in name.");
        }return name.getBytes(StandardCharsets.UTF_16BE);
    }
    private static void setName(byte[] b,int p,byte[] name){Arrays.fill(b,p,p+20,(byte)0);System.arraycopy(name,0,b,p,name.length);}
    private static boolean idMatches(byte[] b,int p,byte[] id){return Arrays.equals(Arrays.copyOfRange(b,p,p+8),id);}
    private static byte[] checkedDriver(SaveArchive.Profile p,Driver driver) throws IOException {
        require(SAVES.contains(driver.root) && driver.slot>=0 && driver.slot<4,"Unknown driver.");
        byte[] b=read(file(p,driver.root,"rksys.dat"),SAVE_BYTES);validateSave(b);
        require(hash(b).equals(driver.saveHash),"The player save changed. Reopen Player profiles and choose the driver again.");return b;
    }
    /** Rename the linked Mii and every license using it in the two games, in one transaction. */
    public static void rename(SaveArchive.Profile p,Driver driver,String name,File work,File previous) throws IOException {
        byte[] encoded=encodeName(name);checkedDriver(p,driver);
        transact(p,work,previous,stage->{
            checkedDriver(stage,driver);byte[] db=read(file(stage,"miis","RFL_DB.dat"),DB_BYTES);validateDatabase(db);
            int matches=0;for(int slot=0;slot<100;slot++){int at=4+slot*RECORD_BYTES;
                if(nonzero(db,at,RECORD_BYTES) && idMatches(db,at+0x18,driver.id)){setName(db,at+2,encoded);matches++;}}
            require(matches==1,"This license has a missing or ambiguous Mii. Use Choose driver Mii, or import your own Mii in Options > Mii Data first.");
            put(db,DB_CRC,crc16(db,0,DB_CRC),2);write(file(stage,"miis","RFL_DB.dat"),db);
            for(String root:SAVES){File f=file(stage,root,"rksys.dat");if(!f.exists())continue;
                byte[] b=read(f,SAVE_BYTES);validateSave(b);boolean changed=false;
                for(int slot=0;slot<4;slot++){int at=8+slot*LICENSE_BYTES;if(active(b,slot) && idMatches(b,at+0x28,driver.id)){
                    setName(b,at+0x14,encoded);
                    // Update the full cached Mii only if it represents this identity and has a valid checksum.
                    int cached=at+0x5684;
                    if(idMatches(b,cached+0x18,driver.id) && crc16(b,cached,74)==number(b,cached+74,2)){
                        setName(b,cached+2,encoded);put(b,cached+74,crc16(b,cached,74),2);
                    }changed=true;}}
                if(changed){saveCrc(b);write(f,b);}
            }
        });
    }
    /** Repair a missing Mii link without replacing the selected license's online account or progress. */
    public static void chooseMii(SaveArchive.Profile p,Driver driver,Mii mii,File work,File previous) throws IOException {
        checkedDriver(p,driver);
        transact(p,work,previous,stage->{byte[] b=checkedDriver(stage,driver);
            byte[] db=read(file(stage,"miis","RFL_DB.dat"),DB_BYTES);validateDatabase(db);
            require(mii.slot>=0 && mii.slot<100,"Unknown Mii.");int at=4+mii.slot*RECORD_BYTES;
            require(idMatches(db,at+0x18,mii.id) && name(db,at+2).equals(mii.name),"The chosen Mii changed. Choose it again.");
            int license=8+driver.slot*LICENSE_BYTES;
            setName(b,license+0x14,encodeName(mii.name));System.arraycopy(mii.id,0,b,license+0x28,8);
            System.arraycopy(db,at,b,license+0x5684,74);put(b,license+0x5684+74,crc16(b,license+0x5684,74),2);
            saveCrc(b);write(file(stage,driver.root,"rksys.dat"),b);
        });
    }
    private interface Edit {void apply(SaveArchive.Profile profile) throws IOException;}
    private static void transact(SaveArchive.Profile p,File work,File previous,Edit edit) throws IOException {
        require(!SaveArchive.pending(work),"Recover the interrupted save operation first.");
        File stage=new File(work,"dolphin-"+UUID.randomUUID());SaveArchive.mkdir(stage);
        try {File snapshot=new File(stage,"before.zip"),files=new File(stage,"files");SaveArchive.mkdir(files);
            SaveArchive.backup(p,snapshot,true);SaveArchive.Inventory inventory=SaveArchive.verify(p,snapshot,files);
            LinkedHashMap<String,File> roots=new LinkedHashMap<>();for(String key:p.roots.keySet()){
                File root=new File(files,key);roots.put(key,root);if(Boolean.TRUE.equals(inventory.roots.get(key)))SaveArchive.mkdir(root);}
            SaveArchive.Profile staged=new SaveArchive.Profile(ID,roots);edit.apply(staged);
            File merged=new File(stage,"after.zip");SaveArchive.backup(staged,merged,false);
            SaveArchive.restore(p,merged,work,previous);
        }finally{erase(stage);}
    }
    private static Map<Long,Integer> ratingEntries(byte[] b) throws IOException {
        require(b.length==1640 && ascii(b,0,4).equals("RRRT") && number(b,4,2)==1 && number(b,6,2)==100,"Select a supported RRRating.pul (version 1).");
        Map<Long,Integer> entries=new LinkedHashMap<>();
        for(int i=0;i<100;i++){int at=8+16*i;long flags=number(b,at+12,4);require(flags==0||flags==1,"Unsupported rating flags.");
            if(flags==1){long id=number(b,at,4);require(id>0&&id<1000000000L && !entries.containsKey(id),"Invalid or duplicate online profile in ratings.");
                for(int p=at+4;p<=at+8;p+=4){float f=Float.intBitsToFloat((int)number(b,p,4));require(!Float.isNaN(f)&&!Float.isInfinite(f)&&f>=0&&f<=10000,"Invalid rating value.");}
                entries.put(id,at);}}
        return entries;
    }
    public static void importRatings(SaveArchive.Profile p,Driver driver,byte[] source,File work,File previous) throws IOException {
        require(driver.root.equals("retro")||driver.root.equals("retro-alt"),"Choose a Retro Rewind driver for ratings.");
        byte[] incoming=source.clone();Map<Long,Integer> entries=ratingEntries(incoming);
        byte[] save=checkedDriver(p,driver);long id=number(save,8+driver.slot*LICENSE_BYTES+0x5c,4);
        require(id>0&&id<1000000000L && entries.containsKey(id),"This rating file has no matching online account for the chosen driver. Import that account's rksys.dat first.");
        transact(p,work,previous,stage->{checkedDriver(stage,driver);File dest=file(stage,"ratings","RRRating.pul");
            byte[] output;if(dest.exists())output=read(dest,1640);else{output=new byte[1640];System.arraycopy(incoming,0,output,0,8);}
            Map<Long,Integer> existing=ratingEntries(output);Integer target=existing.get(id);
            if(target==null)for(int i=0;i<100;i++)if(number(output,8+16*i+12,4)==0){target=8+16*i;break;}
            require(target!=null,"The local rating file is full.");System.arraycopy(incoming,entries.get(id),output,target,16);write(dest,output);
        });
    }
    private static void write(File f,byte[] b) throws IOException {SaveArchive.safe(f);SaveArchive.mkdir(f.getParentFile());
        try(FileOutputStream out=new FileOutputStream(f)){out.write(b);out.getFD().sync();}}
    private static void erase(File f) throws IOException {SaveArchive.safe(f);if(!f.exists())return;
        if(f.isDirectory()){File[] children=f.listFiles();require(children!=null,"Could not clean player staging.");for(File child:children)erase(child);}Files.delete(f.toPath());}
}
