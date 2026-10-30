package com.driftdroid.android.storage;

import java.io.*;
import java.nio.channels.*;
import java.nio.file.*;
import java.util.*;

/** Fixed app-owned content targets. A journal contains target IDs, never executable paths. */
public final class SafeOffload {
    private SafeOffload() {}
    public interface Stopped { void check() throws IOException; }
    public interface Progress { void update(long bytes, int files); }
    public interface Moves { void before(File from, File to) throws IOException; }
    public static final class Target {
        public final String id;
        public final File base, file;
        public Target(String id, File base, String relative) throws IOException {
            if (!id.matches("[a-z0-9][a-z0-9_-]{0,79}") || relative.isEmpty() || relative.startsWith("/") || relative.contains("\\"))
                throw new IOException("Invalid content target.");
            for (String part : relative.split("/", -1))
                if (part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Invalid content path.");
            safe(base);this.id=id; this.base=base.getCanonicalFile();
            this.file=new File(this.base,relative).getAbsoluteFile();
            safe(this.file);
            if (!inside(this.file,this.base) || this.file.equals(this.base)) throw new IOException("Content path escaped its storage.");
        }
    }
    public static final class Plan {
        public final String game;
        public final File work;
        public final LinkedHashMap<String,Target> targets=new LinkedHashMap<>();
        public final List<File> protectedRoots;
        public Plan(String game,File work,List<Target> targets,List<File> protectedRoots) throws IOException {
            if(!game.matches("[a-z0-9-]{1,48}") || targets.size()>1024) throw new IOException("Invalid content plan.");
            safe(work);this.game=game;this.work=work.getCanonicalFile();safe(this.work);
            this.protectedRoots=new ArrayList<>();
            for(File f:protectedRoots) this.protectedRoots.add(f.getCanonicalFile());
            for(Target t:targets) {
                if(this.targets.put(t.id,t)!=null) throw new IOException("Duplicate content target.");
                validate(this,t);
                for(Target other:this.targets.values()) if(other!=t && (inside(t.file,other.file)||inside(other.file,t.file)))
                    throw new IOException("Overlapping content targets.");
            }
        }
    }
    public static final class Result {
        public final long bytes;public final int files;
        Result(long bytes,int files){this.bytes=bytes;this.files=files;}
    }
    private static boolean exists(File f){return Files.exists(f.toPath(),LinkOption.NOFOLLOW_LINKS);}
    private static boolean inside(File f,File root){return f.toPath().normalize().startsWith(root.toPath().normalize());}
    private static void safe(File file) throws IOException {
        for(Path p=file.toPath().toAbsolutePath().normalize();p!=null;p=p.getParent())
            if(Files.isSymbolicLink(p)) throw new IOException("Content storage contains a symbolic link.");
    }
    private static void validate(Plan plan,Target target) throws IOException {
        safe(target.file);
        if(!inside(target.file,target.base)||target.file.equals(target.base)) throw new IOException("Unsafe content target.");
        if(inside(plan.work,target.file)||inside(target.file,plan.work)) throw new IOException("Content overlaps its operation journal.");
        for(File save:plan.protectedRoots)
            if(inside(save,target.file)||inside(target.file,save)) throw new IOException("Content overlaps saves or settings; keeping all files.");
    }
    private static Result scan(File file,int depth) throws IOException {
        safe(file);if(!exists(file))return new Result(0,0);
        if(depth>64)throw new IOException("Content tree is too deep.");
        if(file.isFile())return new Result(file.length(),1);
        if(!file.isDirectory())throw new IOException("Unsupported content file.");
        File[] children=file.listFiles();if(children==null)throw new IOException("Cannot inspect content storage.");
        long bytes=0;int files=0;
        for(File child:children){Result r=scan(child,depth+1);bytes=Math.addExact(bytes,r.bytes);files=Math.addExact(files,r.files);if(files>250000)throw new IOException("Content tree is too large.");}
        return new Result(bytes,files);
    }
    public static Result size(Plan plan) throws IOException {
        long bytes=0;int files=0;
        for(Target t:plan.targets.values()){validate(plan,t);Result r=scan(t.file,0);bytes=Math.addExact(bytes,r.bytes);files+=r.files;}
        return new Result(bytes,files);
    }
    private static File journal(File work){return new File(work,"offload.properties");}
    public static boolean pending(File work){return exists(journal(work));}
    private static Properties read(File work) throws IOException {
        File j=journal(work);safe(j);
        if(!j.isFile()||j.length()>65536)throw new IOException("Invalid offload recovery record.");
        Properties p=new Properties();try(InputStream in=new FileInputStream(j)){p.load(in);}return p;
    }
    public static String pendingGame(File work) throws IOException {
        if(!pending(work))return null;
        String id=read(work).getProperty("game","");
        if(!id.matches("[a-z0-9-]{1,48}"))throw new IOException("Invalid offload game.");return id;
    }
    private static void write(File work,Properties p) throws IOException {
        File target=journal(work),tmp=new File(work,"offload.properties.new");safe(tmp);safe(target);
        try(FileOutputStream out=new FileOutputStream(tmp)){p.store(out,"WiiDS content offload");out.getFD().sync();}
        Files.move(tmp.toPath(),target.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
    }
    private static File aside(Target t,String token){return new File(t.file.getParentFile(),"."+t.file.getName()+".wiids-offload-"+token);}
    private static void remove(File file,long[] count,Progress progress) throws IOException {
        safe(file);if(!exists(file))return;
        if(file.isDirectory()){
            File[] children=file.listFiles();if(children==null)throw new IOException("Cannot read staged content.");
            for(File child:children)remove(child,count,progress);
        }else if(file.isFile()){count[0]+=file.length();count[1]++;}
        else throw new IOException("Unsupported staged content.");
        Files.delete(file.toPath());progress.update(count[0],(int)count[1]);
    }
    public static Result offload(Plan plan,Stopped stopped,Progress progress) throws IOException {
        return offload(plan,stopped,progress,(a,b)->{});
    }
    public static synchronized Result offload(Plan plan,Stopped stopped,Progress progress,Moves moves) throws IOException {
        safe(plan.work);if(!plan.work.isDirectory()&&!plan.work.mkdirs())throw new IOException("Cannot open storage manager.");
        File lock=new File(plan.work,"operation.lock");safe(lock);
        try(RandomAccessFile stream=new RandomAccessFile(lock,"rw");FileChannel channel=stream.getChannel()) {
            FileLock lease;
            try{lease=channel.tryLock();}catch(OverlappingFileLockException busy){throw new IOException("Another storage operation is running.");}
            if(lease==null)throw new IOException("Another storage operation is running.");
            try {
                stopped.check();
                Properties state;
                if(pending(plan.work)) {
                    state=read(plan.work);
                    if(!plan.game.equals(state.getProperty("game")))throw new IOException("Finish the interrupted offload first.");
                }else {
                    size(plan); // Inspect every tree and protection boundary before staging anything.
                    state=new Properties();state.setProperty("game",plan.game);state.setProperty("token",UUID.randomUUID().toString());
                    state.setProperty("targets",String.join(",",plan.targets.keySet()));write(plan.work,state);
                }
                String token=state.getProperty("token","");
                if(!token.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))throw new IOException("Invalid offload recovery token.");
                String raw=state.getProperty("targets","");String[] ids=raw.isEmpty()?new String[0]:raw.split(",",-1);
                if(ids.length>1024||new HashSet<>(Arrays.asList(ids)).size()!=ids.length)throw new IOException("Invalid offload recovery targets.");
                for(String id:ids)if(!plan.targets.containsKey(id))throw new IOException("Unknown offload recovery target; keeping files.");
                long[] count={0,0};
                for(String id:ids) {
                    stopped.check();Target t=plan.targets.get(id);validate(plan,t);File staged=aside(t,token);safe(staged);
                    if(exists(staged)&&exists(t.file))throw new IOException("Content changed during offload; keeping both copies.");
                    if(exists(t.file)) {
                        scan(t.file,0);moves.before(t.file,staged);safe(staged);
                        if(exists(staged))throw new IOException("Offload staging is occupied; keeping both copies.");
                        Files.move(t.file.toPath(),staged.toPath(),StandardCopyOption.ATOMIC_MOVE);
                    }
                    if(exists(staged)){scan(staged,0);remove(staged,count,progress);}
                }
                Files.delete(journal(plan.work).toPath());return new Result(count[0],(int)count[1]);
            }finally{lease.release();}
        }
    }
}
