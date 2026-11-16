package dev.wiids.xd;

import android.content.Context;
import java.io.File;

/** Disc, saves and configuration have separate roots; offload may remove only game/. */
public final class XdStorage {
    private XdStorage() {}
    // The integrated WiiDS package contains the Android game runtime.
    public static final boolean NATIVE_GAME_AVAILABLE = true;
    public static File root(Context c) {
        Context app=c.getApplicationContext(); File base=app.getExternalFilesDir(null);
        return new File(base!=null?base:new File(app.getFilesDir(),"data"),"pokemon-xd");
    }
    public static File game(Context c) { return new File(root(c),"game"); }
    public static File iso(Context c) { return new File(game(c),"game.iso"); }
    public static File saves(Context c) { return new File(root(c),"saves"); }
    public static boolean imported(Context c) { return iso(c).isFile()&&iso(c).length()==XdDisc.BYTES; }
    public static boolean ready(Context c) { return NATIVE_GAME_AVAILABLE&&imported(c); }
}
