package com.driftdroid.android.achievements;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;

/** Per-user Web API key encrypted by a non-exportable Android Keystore key. */
final class RaCredentials {
    private static final String ALIAS = "wiids.ra.progress.v1";
    private final AtomicFile file;
    RaCredentials(Context context) { file = new AtomicFile(new File(context.getNoBackupFilesDir(), "ra-credentials")); }
    private javax.crypto.SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null);
        if (!store.containsAlias(ALIAS)) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).setKeySize(256).build());
            generator.generateKey();
        }
        return (javax.crypto.SecretKey)store.getKey(ALIAS, null);
    }
    void save(String account, String apiKey) throws IOException {
        RaProgress.account(account);
        if (apiKey == null || apiKey.trim().isEmpty() || apiKey.length() > 1024) throw new IOException("Enter your RA Web API key.");
        FileOutputStream output = null;
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] encrypted = cipher.doFinal((account + "\n" + apiKey).getBytes(StandardCharsets.UTF_8));
            output = file.startWrite(); output.write(cipher.getIV().length); output.write(cipher.getIV()); output.write(encrypted);
            file.finishWrite(output);
        } catch (Exception unavailable) {
            if (output != null) file.failWrite(output);
            throw new IOException("Could not securely store the RA connection.");
        }
    }
    String[] read() throws IOException {
        if (!file.getBaseFile().exists()) return null;
        try {
            byte[] bytes = file.readFully();
            if (bytes.length < 30 || bytes.length > 4096 || bytes[0] != 12) throw new IOException();
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, Arrays.copyOfRange(bytes, 1, 13)));
            String[] parts = new String(cipher.doFinal(bytes, 13, bytes.length-13), StandardCharsets.UTF_8).split("\n", 2);
            if (parts.length != 2) throw new IOException();
            RaProgress.account(parts[0]); return parts;
        } catch (Exception invalid) { throw new IOException("RA connection needs to be entered again."); }
    }
    void clear() throws IOException {
        file.delete();
        try {
            KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null); store.deleteEntry(ALIAS);
        } catch (Exception error) { throw new IOException("Could not remove the saved RA connection."); }
    }
}
