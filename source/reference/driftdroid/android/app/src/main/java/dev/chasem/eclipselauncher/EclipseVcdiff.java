// SPDX-License-Identifier: MIT
// Adapted from sms-pc-port/tools/mods/vcdiff.py, Copyright (c) 2026 SoftwareAura LLC.
// The full MIT notice is distributed in assets/licenses/LICENSE-MIT-sms-pc-port.txt.
package dev.chasem.eclipselauncher;

import java.io.*;
import java.util.Arrays;
import java.util.zip.Adler32;

/** RFC 3284 default-table VCDIFF decoder for the pinned Eclipse patch.
 * Input and output are streamed per window; the original disc is always read-only.
 * Secondary compression and custom code tables are deliberately unsupported.
 */
public final class EclipseVcdiff {
    private static final int MAX_WINDOW = 16 * 1024 * 1024, MAX_SECTION = 32 * 1024 * 1024;
    private static final long DISC_SIZE = 1459978240L;
    private static final int NOOP = 0, ADD = 1, RUN = 2, COPY = 3;
    private static final int[][] TABLE = table();
    public interface Progress { void update(long consumed, long written) throws IOException; }
    private EclipseVcdiff() {}
    private static IOException bad(String reason) { return new IOException("Unsupported or damaged Eclipse patch: " + reason); }

    private static int[][] table() {
        int[][] t = new int[256][]; int n = 0;
        t[n++] = new int[]{RUN, 0, 0, NOOP, 0, 0};
        for (int s = 0; s <= 17; s++) t[n++] = new int[]{ADD, s, 0, NOOP, 0, 0};
        for (int m = 0; m < 9; m++) {
            t[n++] = new int[]{COPY, 0, m, NOOP, 0, 0};
            for (int s = 4; s <= 18; s++) t[n++] = new int[]{COPY, s, m, NOOP, 0, 0};
        }
        for (int m = 0; m < 6; m++) for (int a = 1; a <= 4; a++) for (int c = 4; c <= 6; c++)
            t[n++] = new int[]{ADD, a, 0, COPY, c, m};
        for (int m = 6; m < 9; m++) for (int a = 1; a <= 4; a++)
            t[n++] = new int[]{ADD, a, 0, COPY, 4, m};
        for (int m = 0; m < 9; m++) t[n++] = new int[]{COPY, 4, m, ADD, 1, 0};
        if (n != 256) throw new AssertionError("VCDIFF default table");
        return t;
    }

    private static final class Input extends FilterInputStream {
        long count;
        Input(InputStream input) { super(input); }
        int octet() throws IOException { int b = read(); if (b < 0) throw bad("truncated data"); return b; }
        @Override public int read() throws IOException { int b = in.read(); if (b >= 0) count++; return b; }
        @Override public int read(byte[] b, int off, int len) throws IOException {
            int n = in.read(b, off, len); if (n > 0) count += n; return n;
        }
        long varint() throws IOException {
            long v = 0;
            for (int i = 0; i < 9; i++) {
                int b = octet();
                if (v > (Long.MAX_VALUE >>> 7)) throw bad("integer overflow");
                v = (v << 7) | (b & 127);
                if ((b & 128) == 0) return v;
            }
            throw bad("integer too long");
        }
        byte[] section(long length, int limit) throws IOException {
            if (length < 0 || length > limit) throw bad("window exceeds supported bounds");
            byte[] data = new byte[(int) length]; int at = 0;
            while (at < data.length) { int n = read(data, at, data.length - at); if (n <= 0) throw bad("truncated section"); at += n; }
            return data;
        }
    }

    /** Writes a new, empty output file. Caller verifies the final disc hash before installing it. */
    public static void apply(File source, InputStream patch, File output, Progress progress) throws IOException {
        if (source.getCanonicalFile().equals(output.getCanonicalFile())) throw bad("source and output paths coincide");
        if (output.exists()) throw bad("output already exists");
        try (RandomAccessFile src = new RandomAccessFile(source, "r");
             RandomAccessFile out = new RandomAccessFile(output, "rw");
             Input delta = new Input(new BufferedInputStream(patch, 256 * 1024))) {
            if (delta.octet() != 0xd6 || delta.octet() != 0xc3 || delta.octet() != 0xc4 || delta.octet() != 0)
                throw bad("not VCDIFF");
            int header = delta.octet();
            if ((header & ~4) != 0) throw bad("secondary compression or custom code table");
            if ((header & 4) != 0) delta.section(delta.varint(), 1024 * 1024);
            long written = 0;
            for (int window; (window = delta.read()) != -1;) {
                if ((window & ~7) != 0 || (window & 3) == 3) throw bad("window flags");
                long slen = 0, spos = 0;
                RandomAccessFile segment = src;
                if ((window & 3) != 0) {
                    slen = delta.varint(); spos = delta.varint();
                    segment = (window & 1) != 0 ? src : out;
                    long available = (window & 1) != 0 ? src.length() : written;
                    if (spos > available || slen > available - spos) throw bad("source segment outside existing data");
                }
                long encodedLength = delta.varint(), start = delta.count;
                long targetLength = delta.varint();
                if (targetLength > MAX_WINDOW || targetLength > DISC_SIZE - written) throw bad("output window too large");
                if (delta.octet() != 0) throw bad("compressed window sections");
                long dataLength = delta.varint(), instLength = delta.varint(), addrLength = delta.varint();
                long checksum = -1;
                if ((window & 4) != 0) {
                    checksum = 0;
                    for (int i = 0; i < 4; i++) checksum = (checksum << 8) | delta.octet();
                }
                byte[] data = delta.section(dataLength, MAX_SECTION), inst = delta.section(instLength, MAX_SECTION), addr = delta.section(addrLength, MAX_SECTION);
                if (delta.count - start != encodedLength) throw bad("encoded window length mismatch");
                byte[] target = decode((int) targetLength, segment, spos, slen, data, inst, addr);
                if (checksum >= 0) {
                    Adler32 adler = new Adler32(); adler.update(target);
                    if (adler.getValue() != checksum) throw bad("window checksum mismatch");
                }
                out.seek(written); out.write(target); written += target.length;
                if (progress != null) progress.update(delta.count, written);
            }
            if (written != DISC_SIZE) throw bad("unexpected output size");
            out.getFD().sync();
        }
    }

    private static byte[] decode(int length, RandomAccessFile segment, long spos, long slen,
                                 byte[] data, byte[] instructions, byte[] addresses) throws IOException {
        byte[] target = new byte[length];
        Input inst = new Input(new ByteArrayInputStream(instructions));
        Input addr = new Input(new ByteArrayInputStream(addresses));
        long[] near = new long[4], same = new long[768]; int slot = 0, at = 0, dpos = 0;
        for (int code; (code = inst.read()) >= 0;) {
            int[] pair = TABLE[code];
            for (int k = 0; k < 6; k += 3) {
                int type = pair[k], mode = pair[k + 2];
                if (type == NOOP) continue;
                long count = pair[k + 1] == 0 ? inst.varint() : pair[k + 1];
                if (count <= 0 || count > target.length - at) throw bad("instruction exceeds output window");
                int size = (int) count;
                if (type == ADD) {
                    if (size > data.length - dpos) throw bad("ADD exceeds data section");
                    System.arraycopy(data, dpos, target, at, size); dpos += size; at += size;
                } else if (type == RUN) {
                    if (dpos >= data.length) throw bad("RUN exceeds data section");
                    Arrays.fill(target, at, at + size, data[dpos++]); at += size;
                } else {
                    long here = slen + at, address;
                    if (mode == 0) address = addr.varint();
                    else if (mode == 1) address = here - addr.varint();
                    else if (mode < 6) address = near[mode - 2] + addr.varint();
                    else address = same[(mode - 6) * 256 + addr.octet()];
                    if (address < 0 || address >= here) throw bad("COPY address outside decoded data");
                    near[slot] = address; slot = (slot + 1) % 4; same[(int) (address % 768)] = address;
                    if (address < slen) {
                        int part = (int) Math.min(size, slen - address);
                        segment.seek(spos + address); segment.readFully(target, at, part);
                        at += part; size -= part; address = slen;
                    }
                    if (size != 0) {
                        int from = (int) (address - slen);
                        if (from < 0 || from >= at) throw bad("COPY target address outside decoded data");
                        while (size > 0) {
                            int part = Math.min(size, at - from);
                            System.arraycopy(target, from, target, at, part); at += part; size -= part;
                        }
                    }
                }
            }
        }
        if (at != length || dpos != data.length || addr.count != addresses.length) throw bad("window sections do not add up");
        return target;
    }
}
