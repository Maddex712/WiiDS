package dev.thor.galaxy;

import android.content.Context;
import android.content.ContextWrapper;
import java.io.File;

/** A package-internal Galaxy profile; never shares NAND/settings with another game. */
public final class GalaxyStorage extends ContextWrapper {
    private final String namespace;
    public GalaxyStorage(Context base) { this(base, false); }
    public GalaxyStorage(Context base, boolean kaizo) { super(base); namespace = kaizo ? "galaxy-kaizo" : "galaxy"; }
    @Override public File getFilesDir() {
        File dir = new File(super.getFilesDir(), namespace);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("Galaxy storage unavailable");
        return dir;
    }
    @Override public File getExternalFilesDir(String type) {
        File base = super.getExternalFilesDir(null);
        if (base == null) return null;
        File dir = new File(base, type == null ? namespace : namespace + "/" + type);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("Galaxy storage unavailable");
        return dir;
    }
    public static Context scoped(Context context) {
        if (context instanceof GalaxyStorage) return context;
        if (context instanceof android.app.Activity && ((android.app.Activity)context).getBaseContext() instanceof GalaxyStorage)
            return ((android.app.Activity)context).getBaseContext();
        return new GalaxyStorage(context);
    }
    public static boolean ready(Context context) {
        Context profile=scoped(context);
        return !GalaxyMigration.pending(profile.getFilesDir()) && ThorLaunchActivity.ready(profile);
    }
    public static boolean kaizoReady(Context context) {
        Context profile = new GalaxyStorage(context, true);
        if (!ThorLaunchActivity.ready(profile)) return false;
        File external = profile.getExternalFilesDir(null);
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(
                new File(external, "GalaxyData/sys/KaizoTables.txt")))) {
            return KaizoActivities.PATCH_HEADER.equals(in.readLine());
        } catch (Exception missing) { return false; }
    }
}
