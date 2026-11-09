package dev.wiids.brawl;

import android.content.*;
import java.io.*;

/** Private Brawl profile inside the one WiiDS package. */
public final class BrawlStorage extends ContextWrapper {
    static volatile boolean importing;
    private final String profile;
    public BrawlStorage(Context base) { this(base, false); }
    BrawlStorage(Context base, boolean projectPlus) {
        super(base);
        profile = projectPlus ? "project-plus" : "brawl";
    }
    static boolean isProjectPlus(Context c) {
        return ((BrawlStorage) scoped(c)).profile.equals("project-plus");
    }
    static File discData(Context c) { return data(c.getApplicationContext()); }
    private static File profileDir(Context c, String name) {
        Context app = c.getApplicationContext();
        File external = app.getExternalFilesDir(null);
        return external == null ? new File(app.getFilesDir(), name + "/data") : new File(external, name);
    }
    static boolean projectPlusEnabled(Context c) {
        return !com.driftdroid.android.GameStorage.INSTANCE.disabled(c, com.driftdroid.android.FrontendGame.PROJECT_PLUS);
    }
    public static boolean projectPlusInstalled(Context c) {
        return ProjectPlusFiles.ready(profileDir(c, "project-plus"));
    }
    public static boolean projectPlusAvailable(Context c) {
        return projectPlusInstalled(c) || ProjectPlusFiles.ready(profileDir(c, "brawl"));
    }
    static void snapshotProjectPlusSave(Context c) throws IOException {
        ProjectPlusFiles.snapshot(profileDir(c, "brawl"), profileDir(c, "project-plus"));
    }
    public static void prepareProjectPlus(Context c) throws IOException {
        String problem = validate(new File(profileDir(c, "brawl"), "game"));
        if (problem != null) throw new IOException(problem);
        ProjectPlusFiles.prepare(profileDir(c, "brawl"), profileDir(c, "project-plus"));
    }
    @Override public File getFilesDir() {
        File f=new File(super.getFilesDir(),profile);if(!f.isDirectory()&&!f.mkdirs())throw new IllegalStateException("Brawl storage unavailable");return f;
    }
    @Override public File getExternalFilesDir(String type) {
        File base=super.getExternalFilesDir(null);if(base==null)return null;
        File f=new File(base,type==null?profile:profile+"/"+type);if(!f.isDirectory()&&!f.mkdirs())throw new IllegalStateException("Brawl data storage unavailable");return f;
    }
    @Override public SharedPreferences getSharedPreferences(String name,int mode) { return super.getSharedPreferences(profile+"_"+name,mode); }
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
