package com.driftdroid.android;

import android.content.Context;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

/** Publish the current case's pack paths before its private native runtime starts. */
public final class NativeTextureSelection {
    private NativeTextureSelection() {}

    public static void apply(Context context, FrontendGame game) {
        String paths = TexturePacks.activePath(context.getApplicationContext(), game);
        try {
            Os.setenv("WIIDS_NATIVE_TEXTURE_PATHS", paths == null ? "" : paths, true);
        } catch (ErrnoException e) {
            Log.w("WiiDSTextures", "Could not configure texture packs for " + game.id, e);
        }
    }
}
