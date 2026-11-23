package dev.wiids.xd;

import android.content.Context;
import java.io.File;

/** Disc, saves and configuration have separate roots; offload may remove only game/. */
public final class XdStorage {
    private XdStorage() {}
    // The integrated WiiDS package contains the Android game runtime.
    public static final boolean NATIVE_GAME_AVAILABLE = true;
    public static final String XG_VERIFIED_MARKER = ".xg-1.2.1";
    public static File root(Context c) { return root(c, false); }
    public static File root(Context c, boolean xg) {
        Context app=c.getApplicationContext(); File base=app.getExternalFilesDir(null);
        return new File(base!=null?base:new File(app.getFilesDir(),"data"),xg ? "pokemon-xg" : "pokemon-xd");
    }
    public static File game(Context c) { return game(c, false); }
    public static File game(Context c, boolean xg) { return new File(root(c, xg), "game"); }
    public static File iso(Context c) { return iso(c, false); }
    public static File iso(Context c, boolean xg) { return new File(game(c, xg), "game.iso"); }
    public static File saves(Context c) { return saves(c, false); }
    public static File saves(Context c, boolean xg) { return new File(root(c, xg), "saves"); }
    public static boolean imported(Context c) { return iso(c).isFile()&&iso(c).length()==XdDisc.BYTES; }
    public static boolean xgRuntimeAvailable(Context c) {
        return new File(c.getApplicationInfo().nativeLibraryDir, "libxd_xg.so").isFile();
    }
    public static boolean xgReady(Context c) {
        File image = iso(c, true);
        return xgRuntimeAvailable(c) && image.isFile() && image.length() == XdDisc.BYTES
                && new File(game(c, true), XG_VERIFIED_MARKER).isFile();
    }
    public static boolean ready(Context c) { return NATIVE_GAME_AVAILABLE&&imported(c); }
}
