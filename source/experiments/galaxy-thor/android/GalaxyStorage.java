package dev.thor.galaxy;

import android.content.Context;
import android.content.ContextWrapper;
import java.io.File;

/** A package-internal Galaxy profile; never shares NAND/settings with another game. */
public final class GalaxyStorage extends ContextWrapper {
    public GalaxyStorage(Context base) { super(base); }
    @Override public File getFilesDir() {
        File dir = new File(super.getFilesDir(), "galaxy");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("Galaxy storage unavailable");
        return dir;
    }
    @Override public File getExternalFilesDir(String type) {
        File base = super.getExternalFilesDir(null);
        if (base == null) return null;
        File dir = new File(base, type == null ? "galaxy" : "galaxy/" + type);
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
}
