package dev.thor.galaxy;

import java.io.*;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Copy-only legacy profile import. Existing WiiDS Galaxy profiles are never overwritten. */
public final class GalaxyMigration {
    private GalaxyMigration() {}
    private static File journal(File internal) { return new File(internal,".galaxy-migration.properties"); }
    private static void properties(File file,Properties p) throws IOException {
        File tmp=new File(file+".new");
        try(FileOutputStream out=new FileOutputStream(tmp)){p.store(out,"Galaxy migration");out.getFD().sync();}
        Files.move(tmp.toPath(),file.toPath(),java.nio.file.StandardCopyOption.ATOMIC_MOVE,java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }
    private static void remove(File file) throws IOException {
        if(!file.exists())return;
        if(Files.isSymbolicLink(file.toPath()))throw new IOException("Unexpected symbolic link");
        if(file.isDirectory()){File[] list=file.listFiles();if(list==null)throw new IOException("Can't list migration files");for(File p:list)remove(p);}
        if(!file.delete())throw new IOException("Can't remove migration staging");
    }
    public static void recover(File internal,File external) throws IOException {
        File j=journal(internal);if(!j.isFile())return;
        Properties p=new Properties();try(FileInputStream in=new FileInputStream(j)){p.load(in);}
        String token=p.getProperty("token","");if(!token.matches("[0-9a-f-]{36}"))throw new IOException("Invalid migration journal");
        boolean committed=Boolean.parseBoolean(p.getProperty("committed","false"));
        for(File target:new File[]{new File(internal,"nand"),new File(external,"GalaxyData")}){
            File marker=new File(target,".migration-token");
            if(marker.isFile() && new String(Files.readAllBytes(marker.toPath()),"UTF-8").equals(token)){
                if(committed){if(!marker.delete())throw new IOException("Migration cleanup failed");}
                else remove(target);
            }
        }
        remove(new File(internal,".migration-"+token));remove(new File(external,".migration-"+token));
        if(!j.delete())throw new IOException("Can't close migration journal");
    }
    public static boolean pending(File internal){return journal(internal).exists();}

    /** Backup is created by Export-LegacyProfile.py; its SHA-256 inventory is verified before publishing. */
    public static void importBackup(File internal,File external,InputStream source) throws Exception {
        recover(internal,external);
        File nand=new File(internal,"nand"),data=new File(external,"GalaxyData");
        if(nand.exists() || data.exists())throw new IOException("This WiiDS Galaxy profile already has data. Migration never overwrites it. Keep the legacy copy and export your save separately.");
        String token=UUID.randomUUID().toString();
        File si=new File(internal,".migration-"+token),se=new File(external,".migration-"+token);
        Properties expected=new Properties(),actual=new Properties();long total=0;int count=0;
        Properties transaction=new Properties();transaction.setProperty("token",token);properties(journal(internal),transaction);
        try {
            byte[] buffer=new byte[1<<16];
            try(ZipInputStream zip=new ZipInputStream(source)){
                for(ZipEntry e;(e=zip.getNextEntry())!=null;){
                    String name=e.getName();if(e.isDirectory())continue;
                    if(++count>20000)throw new IOException("Too many backup files");
                    if(name.equals("BACKUP-MANIFEST.properties")){
                        ByteArrayOutputStream bytes=new ByteArrayOutputStream();for(int n;(n=zip.read(buffer))>0;){bytes.write(buffer,0,n);if(bytes.size()>4*1024*1024)throw new IOException("Backup manifest too large");}expected.load(new ByteArrayInputStream(bytes.toByteArray()));continue;
                    }
                    if(name.contains("\\") || name.contains(":") || name.startsWith("/") || Arrays.asList(name.split("/")).contains("..") ||
                       !(name.startsWith("internal/nand/") || name.startsWith("external/GalaxyData/")))throw new IOException("Unsafe backup path");
                    if(actual.containsKey(name))throw new IOException("Duplicate backup file");
                    boolean priv=name.startsWith("internal/");File root=priv?si:se;
                    File out=new File(root,name.substring(name.indexOf('/')+1));
                    if(!out.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))throw new IOException("Backup path escape");
                    if(!out.getParentFile().isDirectory()&&!out.getParentFile().mkdirs())throw new IOException("No room for backup staging");
                    MessageDigest sha=MessageDigest.getInstance("SHA-256");
                    try(FileOutputStream file=new FileOutputStream(out)){for(int n;(n=zip.read(buffer))>0;){total+=n;if(total>12L*1024*1024*1024)throw new IOException("Backup exceeds 12 GiB");file.write(buffer,0,n);sha.update(buffer,0,n);}file.getFD().sync();}
                    StringBuilder hex=new StringBuilder();for(byte b:sha.digest())hex.append(String.format(Locale.ROOT,"%02x",b&255));actual.setProperty(name,hex.toString());
                }
            }
            if(expected.isEmpty() || !expected.equals(actual))throw new IOException("Backup SHA-256 inventory mismatch");
            File stagedData=new File(se,"GalaxyData"),stagedNand=new File(si,"nand");
            for(String name:new String[]{"sys/fst.bin","files/ObjectData/Coin.arc","ThorHud/SETUP-MANIFEST.json","ThorHud/planets/HeavensDoorGalaxy.planets"})
                if(!new File(stagedData,name).isFile())throw new IOException("Incomplete legacy backup: "+name);
            if(!stagedNand.isDirectory())throw new IOException("Backup has no NAND/settings profile");
            for(File target:new File[]{stagedNand,stagedData})try(FileOutputStream out=new FileOutputStream(new File(target,".migration-token"))){out.write(token.getBytes("UTF-8"));out.getFD().sync();}
            if(!stagedNand.renameTo(nand))throw new IOException("Can't publish migrated NAND");
            if(!stagedData.renameTo(data))throw new IOException("Can't publish migrated game files");
            transaction.setProperty("committed","true");properties(journal(internal),transaction);
            recover(internal,external);
        } catch(Exception error){try{recover(internal,external);}catch(IOException rollback){error.addSuppressed(rollback);}throw error;}
    }
}
