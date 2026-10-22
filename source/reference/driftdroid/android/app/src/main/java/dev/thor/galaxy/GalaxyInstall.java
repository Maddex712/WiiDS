package dev.thor.galaxy;

import java.io.*;
import java.nio.file.Files;
import java.util.Properties;

/** Recoverable replacement of cooked data/ThorHud. NAND, settings and saves are never moved. */
public final class GalaxyInstall {
    private static final String[] PARTS = {"files", "sys", "ThorHud"};
    interface Moves { void before(File from, File to) throws IOException; }
    private static final Moves NORMAL = (a,b) -> {};
    private GalaxyInstall() {}

    private static File journal(File data) { return new File(data.getParentFile(), ".galaxy-install.properties"); }
    private static void write(File file, byte[] bytes) throws IOException {
        File temp = new File(file.getPath()+".new");
        try (FileOutputStream out = new FileOutputStream(temp)) { out.write(bytes); out.getFD().sync(); }
        Files.move(temp.toPath(), file.toPath(), java.nio.file.StandardCopyOption.ATOMIC_MOVE,
                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }
    private static void save(File file, Properties p) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(); p.store(bytes, "WiiDS Galaxy transaction");
        write(file, bytes.toByteArray());
    }
    private static void move(File from, File to, Moves moves) throws IOException {
        moves.before(from,to);
        if (to.exists() || !from.renameTo(to)) throw new IOException("Can't move " + from.getName());
    }
    private static void remove(File file) throws IOException {
        if (!file.exists()) return;
        if (Files.isSymbolicLink(file.toPath())) throw new IOException("Unexpected link in Galaxy data");
        if (file.isDirectory()) {
            File[] children=file.listFiles(); if(children==null) throw new IOException("Can't list " + file);
            for(File child:children) remove(child);
        }
        if(!file.delete()) throw new IOException("Can't remove " + file.getName());
    }
    private static File aside(File data, String token, String name) { return new File(data,name+".old-"+token); }

    public static synchronized void recover(File data) throws IOException {
        File j=journal(data); if(!j.isFile()) return;
        Properties p=new Properties(); try(FileInputStream in=new FileInputStream(j)){p.load(in);}
        String token=p.getProperty("token", "");
        if(!token.matches("[0-9a-f-]{36}")) throw new IOException("Invalid Galaxy recovery journal");
        boolean committed=Boolean.parseBoolean(p.getProperty("committed", "false"));
        if(!committed) {
            for(String name:PARTS) {
                File old=aside(data,token,name), target=new File(data,name);
                boolean existed=Boolean.parseBoolean(p.getProperty(name));
                if(old.exists()) { remove(target); if(!old.renameTo(target)) throw new IOException("Galaxy rollback failed: "+name); }
                else if(!existed) remove(target);
            }
            File old=aside(data,token,".ready");
            if(old.exists()) { remove(new File(data,".ready")); if(!old.renameTo(new File(data,".ready"))) throw new IOException("Readiness rollback failed"); }
            else if(!Boolean.parseBoolean(p.getProperty("ready"))) remove(new File(data,".ready"));
        } else {
            for(String name:PARTS) remove(aside(data,token,name));
            remove(aside(data,token,".ready"));
        }
        if(!j.delete()) throw new IOException("Can't close Galaxy recovery journal");
    }

    public static synchronized void install(File data, File pending) throws IOException { install(data,pending,NORMAL); }
    static synchronized void install(File data, File pending, Moves moves) throws IOException {
        if(!data.getCanonicalFile().getParentFile().equals(pending.getCanonicalFile().getParentFile()))
            throw new IOException("Galaxy staging must use the same filesystem directory");
        recover(data);
        for(String name:PARTS) if(!new File(pending,name).isDirectory()) throw new IOException("Incomplete Galaxy staging: "+name);
        if(!new File(pending,"ThorHud/SETUP-MANIFEST.json").isFile()) throw new IOException("Lower-screen assets aren't ready");
        if(!data.isDirectory() && !data.mkdirs()) throw new IOException("Galaxy storage unavailable");
        String token=java.util.UUID.randomUUID().toString();
        Properties p=new Properties();p.setProperty("token",token);
        for(String name:PARTS)p.setProperty(name,Boolean.toString(new File(data,name).exists()));
        p.setProperty("ready",Boolean.toString(new File(data,".ready").exists()));
        save(journal(data),p);
        try {
            File ready=new File(data,".ready");
            if(ready.exists())move(ready,aside(data,token,".ready"),moves);
            for(String name:PARTS) {
                File target=new File(data,name);
                if(target.exists())move(target,aside(data,token,name),moves);
                move(new File(pending,name),target,moves);
            }
            write(ready,"WiiDS integrated Galaxy ready\n".getBytes("UTF-8"));
            p.setProperty("committed","true");save(journal(data),p);
        } catch(IOException failure) {
            try{recover(data);}catch(IOException rollback){failure.addSuppressed(rollback);}
            throw failure;
        }
        recover(data);
        // Non-runtime manifest follows the successful transaction; saves stay in data/saves.
        File manifest=new File(pending,"data-manifest.sha256");
        if(manifest.isFile())write(new File(data,manifest.getName()),Files.readAllBytes(manifest.toPath()));
        remove(pending);
    }
}
