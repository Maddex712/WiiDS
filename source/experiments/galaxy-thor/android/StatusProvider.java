package dev.thor.galaxy;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

/** Publishes one readiness bit for the WiiDS library, never files or saves. */
public final class StatusProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri uri, String[] columns, String selection, String[] args, String order) {
        if (!"/ready".equals(uri.getPath())) throw new IllegalArgumentException("Unknown status");
        MatrixCursor cursor = new MatrixCursor(new String[]{"ready"});
        cursor.addRow(new Object[]{getContext() != null && ThorLaunchActivity.ready(getContext()) ? 1 : 0});
        return cursor;
    }
    @Override public String getType(Uri uri) { return "vnd.android.cursor.item/vnd.wiids.galaxy-status"; }
    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String where, String[] args) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String where, String[] args) { throw new UnsupportedOperationException(); }
}
