package dev.wiids.xd;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.CRC32;

/** Streaming decoder for a same-size UPS disc patch. The caller owns all streams. */
public final class XgUps {
    private static final int BLOCK = 64 * 1024;

    private XgUps() {}

    public interface Progress {
        void update(long patchBytes) throws IOException;
    }

    public static void apply(InputStream source, InputStream patch, OutputStream output,
                             long imageSize, long patchSize, Progress progress) throws IOException {
        if (imageSize < 1 || patchSize < 18) throw invalid();
        Reader reader = new Reader(patch, patchSize - 12, progress);
        if (reader.read() != 'U' || reader.read() != 'P' || reader.read() != 'S' || reader.read() != '1'
                || reader.number() != imageSize || reader.number() != imageSize) throw invalid();
        Copier copier = new Copier(source, output, reader);
        long cursor = 0;
        while (reader.position < reader.limit) {
            long relative = reader.number();
            if (relative >= imageSize - cursor) throw invalid();
            cursor += relative;
            copier.copy(cursor - copier.position, false);
            while (true) {
                int count = reader.xorRun();
                if (count > imageSize - cursor) throw invalid();
                copier.copy(count, true);
                reader.advance(count);
                cursor += count;
                // A run terminator advances the UPS cursor, leaving that source byte unchanged.
                if (reader.consumeTerminator()) {
                    cursor++;
                    break;
                }
            }
        }
        copier.copy(imageSize - copier.position, false);
        if (source.read() != -1) throw invalid();
        long sourceCrc = reader.footerWord(true);
        long outputCrc = reader.footerWord(true);
        long patchCrc = reader.footerWord(false);
        if (sourceCrc != copier.sourceCrc.getValue() || outputCrc != copier.outputCrc.getValue()
                || patchCrc != reader.crc.getValue() || reader.input.read() != -1) throw invalid();
        progress.update(patchSize);
    }

    private static final class Reader {
        final InputStream input;
        final long limit;
        final Progress progress;
        final byte[] buffer = new byte[BLOCK];
        final CRC32 crc = new CRC32();
        long position;
        int offset;
        int end;

        Reader(InputStream input, long limit, Progress progress) {
            this.input = input;
            this.limit = limit;
            this.progress = progress;
        }

        void refill() throws IOException {
            if (position == limit) throw invalid();
            if (offset != end) return;
            progress.update(position);
            end = input.read(buffer, 0, (int)Math.min(BLOCK, limit - position));
            if (end <= 0) throw new EOFException("The UPS patch is incomplete.");
            crc.update(buffer, 0, end);
            offset = 0;
        }

        int read() throws IOException {
            refill();
            int value = buffer[offset] & 255;
            advance(1);
            return value;
        }

        long number() throws IOException {
            long value = 0;
            // Seven groups cover offsets larger than any supported image, with no long overflow.
            for (int shift = 0; shift <= 42; shift += 7) {
                int part = read();
                value += (long)(part & 127) << shift;
                if ((part & 128) != 0) return value;
                value += 1L << (shift + 7);
            }
            throw invalid();
        }

        int xorRun() throws IOException {
            refill();
            int index = offset;
            while (index < end && buffer[index] != 0) index++;
            return index - offset;
        }

        boolean consumeTerminator() {
            if (offset == end || buffer[offset] != 0) return false;
            advance(1);
            return true;
        }

        void advance(int bytes) {
            position += bytes;
            offset += bytes;
        }

        long footerWord(boolean includeInCrc) throws IOException {
            long value = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                int b = input.read();
                if (b == -1) throw invalid();
                if (includeInCrc) crc.update(b);
                value |= (long)b << shift;
            }
            return value;
        }
    }

    private static final class Copier {
        final InputStream source;
        final OutputStream output;
        final Reader patch;
        final byte[] buffer = new byte[BLOCK];
        final CRC32 sourceCrc = new CRC32();
        final CRC32 outputCrc = new CRC32();
        long position;

        Copier(InputStream source, OutputStream output, Reader patch) {
            this.source = source;
            this.output = output;
            this.patch = patch;
        }

        void copy(long size, boolean xor) throws IOException {
            int patchOffset = patch.offset;
            while (size > 0) {
                patch.progress.update(patch.position);
                int requested = (int)Math.min(size, BLOCK);
                int count = source.read(buffer, 0, requested);
                if (count <= 0) throw new EOFException("The original disc is incomplete.");
                sourceCrc.update(buffer, 0, count);
                if (xor) {
                    for (int i = 0; i < count; i++) buffer[i] ^= patch.buffer[patchOffset + i];
                    patchOffset += count;
                }
                outputCrc.update(buffer, 0, count);
                output.write(buffer, 0, count);
                size -= count;
                position += count;
            }
        }
    }

    private static IOException invalid() { return new IOException("The patch or disc failed UPS verification."); }
}
