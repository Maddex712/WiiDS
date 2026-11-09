package com.driftdroid.android;

import java.io.IOException;

/** Decoder for the store's checksum-verified RAR5 and 7z downloads. */
final class CatalogArchive {
    static { System.loadLibrary("wiids_catalog_archive"); }

    private CatalogArchive() {}

    static native long open(String path, boolean rar) throws IOException;
    static native String next(long reader, long[] details) throws IOException;
    static native int read(long reader, byte[] buffer) throws IOException;
    static native void close(long reader);
}
