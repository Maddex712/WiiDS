package dev.thor.galaxy;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import java.io.*;
import java.security.MessageDigest;

/** Same native engine/companion, separate Activity processes and complete profile. */
public final class KaizoActivities {
    static final String PATCH_SHA = "b0b43ee39e3b2c2b7a3dcc4bc33d77718cb97da4b95f490146ec8a80ceddd548";
    static final String PATCH_HEADER = "KMGR21|" + PATCH_SHA;
    public static final class Launch extends ThorLaunchActivity {
        @Override protected boolean kaizoProfile() { return true; }
    }
    public static final class Game extends GalaxyActivity {
        @Override protected boolean kaizoProfile() { return true; }
    }
    public static final class Import extends GalaxyImportActivity {
        private boolean asking, choosing;
        private File patch;
        @Override protected boolean kaizoProfile() { return true; }
        @Override protected String gameTitle() { return "Kaizo Mario Galaxy Rebalanced 2.1"; }
        @Override protected boolean prepareStart() {
            if (patch != null) return true;
            String supplied = getIntent().getStringExtra("patch_path");
            if (supplied != null) {
                try (InputStream in = new FileInputStream(supplied)) { copyPatch(in); }
                catch (Exception failure) { showFailure(failure); return false; }
                return true;
            }
            // Activity.onResume has run; defer only the import until a patch exists.
            if (!asking && !choosing) {
                asking = true;
                new AlertDialog.Builder(this).setTitle("Kaizo Rebalanced 2.1 patch")
                    .setMessage("Choose the creator's original 2.1 ZIP. WiiDS checks its version and combines it with your USA Galaxy disc, keeping separate saves and lower-screen models.")
                    .setPositiveButton("Choose ZIP", (dialog, which) -> {
                        asking = false; choosing = true;
                        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/zip")
                            .addCategory(Intent.CATEGORY_OPENABLE), 2101);
                    })
                    .setNeutralButton("Official download", (dialog, which) -> {
                        asking = false;
                        startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://www.mediafire.com/file/mw07lmjnp2f8f15/KaizoMarioGalaxyRebalanced+V2.1.zip/file")));
                    })
                    .setNegativeButton("Cancel", (dialog, which) -> finish())
                    .setOnCancelListener(dialog -> finish()).show();
            }
            return false;
        }
        @Override protected void onActivityResult(int request, int result, Intent data) {
            super.onActivityResult(request, result, data);
            if (request != 2101) return;
            choosing = false;
            if (result != RESULT_OK || data == null || data.getData() == null) { finish(); return; }
            try (InputStream in = getContentResolver().openInputStream(data.getData())) { copyPatch(in); }
            catch (Exception failure) { showFailure(failure); }
        }
        private void copyPatch(InputStream in) throws Exception {
            if (in == null) throw new IOException("Can't read the patch ZIP");
            File temp = new File(getFilesDir(), "kaizo-patch.new");
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536]; int total = 0;
            try (FileOutputStream out = new FileOutputStream(temp)) {
                for (int n; (n = in.read(buffer)) != -1;) {
                    total += n; if (total > 16 * 1024 * 1024) throw new IOException("This is not the supported patch ZIP");
                    digest.update(buffer, 0, n); out.write(buffer, 0, n);
                }
                out.getFD().sync();
            }
            StringBuilder hash = new StringBuilder(); for (byte b : digest.digest()) hash.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            if (!PATCH_SHA.equals(hash.toString())) { temp.delete(); throw new IOException("Choose the official Rebalanced 2.1 ZIP. Legacy and other versions aren't supported."); }
            File ready = new File(getFilesDir(), "kaizo-patch-2.1.zip");
            java.nio.file.Files.move(temp.toPath(), ready.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            patch = ready;
        }
        private void showFailure(Exception failure) {
            asking = true;
            new AlertDialog.Builder(this).setTitle("Patch couldn't be opened").setMessage(failure.getMessage())
                .setPositiveButton("Close", (dialog, which) -> finish()).setOnCancelListener(dialog -> finish()).show();
        }
        @Override protected void prepareExtraction(File root) throws Exception {
            if (patch == null) throw new IOException("Choose the Kaizo patch first");
            java.nio.file.Files.copy(patch.toPath(), new File(root, ".wiids-kaizo.zip").toPath(),
                java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
    }
}
