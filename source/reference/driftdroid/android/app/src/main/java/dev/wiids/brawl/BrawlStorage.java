package dev.wiids.brawl;

import android.content.*;
import java.io.*;

/** Private Brawl profile inside the one WiiDS package. */
public final class BrawlStorage extends ContextWrapper {
    static volatile boolean importing;
    public BrawlStorage(Context base) { super(base); }
    @Override public File getFilesDir() {
        File f=new File(super.getFilesDir(),"brawl");if(!f.isDirectory()&&!f.mkdirs())throw new IllegalStateException("Brawl storage unavailable");return f;
    }
    @Override public File getExternalFilesDir(String type) {
        File base=super.getExternalFilesDir(null);if(base==null)return null;
        File f=new File(base,type==null?"brawl":"brawl/"+type);if(!f.isDirectory()&&!f.mkdirs())throw new IllegalStateException("Brawl data storage unavailable");return f;
    }
    @Override public SharedPreferences getSharedPreferences(String name,int mode) { return super.getSharedPreferences("brawl_"+name,mode); }
    public static Context scoped(Context c) {
        if(c instanceof BrawlStorage)return c;
        if(c instanceof android.app.Activity&&((android.app.Activity)c).getBaseContext() instanceof BrawlStorage)return ((android.app.Activity)c).getBaseContext();
        return new BrawlStorage(c);
    }
    static boolean blocked(Context c) { return com.driftdroid.android.GameStorage.INSTANCE.blocker(c)!=null; }
    public static File data(Context c) { return AppFiles.dataDir(scoped(c)); }
    /** Fast readiness for the shelf. The launch worker checks the header and exact executable hash. */
    public static boolean ready(Context c) { return !blocked(c)&&AppFiles.hasGameData(scoped(c)); }
    public static String validate(File root) { return AppFiles.gameProblem(root); }
}
