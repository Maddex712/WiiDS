package dev.wiids.brawl;

import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.net.URL;
import java.nio.file.Path;
import javax.net.ssl.HttpsURLConnection;

/** Installation inputs pinned to the Project+ team's published 3.2.1 SD image. */
final class ProjectPlusOfficial {
    static final long CARD_BYTES = 2_097_152_000L;
    static final String CARD_SHA = "c0bb317ac40e50ab5fb30b0af42cfa292226088c7c8c016b330417dcf5c7043a";
    static final String CODE_SHA = "7925f4a0c056f8f71ff9a7cb1ff9cc6b78f5902e8cf571794a2c2d20a99080f1";
    static final String CONFIG_SHA = "6e263dd6254159533f41cc3da4e3c765d720742891a1b9c6e846b7abb853fae0";
    private static final String DOWNLOAD =
            "https://github.com/Project-Plus-Development-Team/PPlusReleases/releases/download/v3.2.1/sd.raw";

    private ProjectPlusOfficial() {}

    static InputStream download() throws IOException {
        URL url = new URL(DOWNLOAD);
        for (int count = 0; count < 5; count++) {
            String host = url.getHost();
            if (!"https".equals(url.getProtocol()) || url.getUserInfo() != null ||
                    (url.getPort() != -1 && url.getPort() != 443) ||
                    !("github.com".equals(host) || "release-assets.githubusercontent.com".equals(host)))
                throw new IOException("Project+ download redirected away from its official host.");
            HttpsURLConnection connection = (HttpsURLConnection) url.openConnection();
            connection.setConnectTimeout(15_000);
            connection.setReadTimeout(15_000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Accept-Encoding", "identity");
            boolean open = false;
            try {
                int status = connection.getResponseCode();
                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    String target = connection.getHeaderField("Location");
                    if (target == null) throw new IOException("The Project+ download redirect was incomplete.");
                    url = new URL(url, target);
                    continue;
                }
                if (status != 200) throw new IOException("Project+ download returned HTTP " + status + ". Try again.");
                long bytes = connection.getContentLengthLong();
                if (bytes != -1 && bytes != CARD_BYTES) throw new IOException("The official Project+ download has an unexpected size.");
                InputStream result = new FilterInputStream(connection.getInputStream()) {
                    @Override public void close() throws IOException {
                        try { super.close(); } finally { connection.disconnect(); }
                    }
                };
                open = true;
                return result;
            } finally {
                if (!open) connection.disconnect();
            }
        }
        throw new IOException("Too many redirects while downloading Project+.");
    }

    // These extents were walked from the published image's FAT32 cluster chains.
    // Only use them after the whole-card size and SHA-256 have been verified.
    static byte[] codes(Path card) throws IOException { return read(card, 1_983_966_208L, 102_120); }
    static byte[] config(Path card) throws IOException { return read(card, 1_975_022_592L, 256); }

    private static byte[] read(Path card, long offset, int size) throws IOException {
        byte[] data = new byte[size];
        try (RandomAccessFile file = new RandomAccessFile(card.toFile(), "r")) {
            file.seek(offset);
            file.readFully(data);
        }
        return data;
    }
}

