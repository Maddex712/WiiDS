package com.driftdroid.android.dualscreen;

import android.graphics.Bitmap;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

/** Reads the original disc's menu artwork. The app does not bundle either game's images. */
public final class WiiDsMenuArt {
    private WiiDsMenuArt() {}

    public static Bitmap colosseum(File verifiedIso) {
        // Root opening.bnr in the verified USA GC6E01 revision 0 image.
        try (RandomAccessFile file = new RandomAccessFile(verifiedIso, "r")) {
            file.seek(0x3aae0b6cL);
            if (file.readInt() != 0x424e5231) return null;
            file.skipBytes(28);
            byte[] pixels = new byte[96 * 32 * 2];
            file.readFully(pixels);
            return decode(ByteBuffer.wrap(pixels), 0, 96, 32);
        } catch (IOException | RuntimeException unavailable) {
            return null;
        }
    }

    public static Bitmap cityFolk(File discDirectory) {
        File archive = new File(discDirectory, "files/Banner/banner.arc");
        try (RandomAccessFile file = new RandomAccessFile(archive, "r")) {
            long length = file.length();
            if (length < 32 || length > 128 * 1024) return null;
            byte[] contents = new byte[(int) length];
            file.readFully(contents);
            ByteBuffer u8 = ByteBuffer.wrap(contents);
            if (u8.getInt(0) != 0x55aa382d) return null;
            int root = u8.getInt(4);
            int count = u8.getInt(root + 8);
            if (root < 32 || count < 2 || count > 128 || !contains(length, root, 12 * count)) return null;
            int strings = root + 12 * count;
            byte[] target = "banner.tpl\0".getBytes(StandardCharsets.US_ASCII);
            for (int entry = 1; entry < count; entry++) {
                int node = root + entry * 12;
                int flags = u8.getInt(node);
                if ((flags >>> 24) != 0) continue;
                int name = strings + (flags & 0xffffff);
                if (!contains(length, name, target.length)) continue;
                if (!sameName(contents, name, target)) continue;
                int offset = u8.getInt(node + 4);
                int size = u8.getInt(node + 8);
                if (size < 20 || !contains(length, offset, size)) return null;
                ByteBuffer tpl = ByteBuffer.wrap(contents, offset, size).slice();
                if (tpl.getInt(0) != 0x0020af30 || tpl.getInt(4) != 1) return null;
                int table = tpl.getInt(8);
                if (!contains(size, table, 8)) return null;
                int header = tpl.getInt(table);
                if (!contains(size, header, 12)) return null;
                if (tpl.getShort(header) != 64 || tpl.getShort(header + 2) != 192 || tpl.getInt(header + 4) != 5)
                    return null;
                return decode(tpl, tpl.getInt(header + 8), 192, 64);
            }
        } catch (IOException | RuntimeException unavailable) {
            return null;
        }
        return null;
    }

    private static boolean sameName(byte[] data, int offset, byte[] expected) {
        for (int i = 0; i < expected.length; i++) if (data[offset + i] != expected[i]) return false;
        return true;
    }

    private static boolean contains(long size, int start, int length) {
        return start >= 0 && length >= 0 && (long) start + length <= size;
    }

    private static Bitmap decode(ByteBuffer input, int offset, int width, int height) {
        int count = width * height;
        if (!contains(input.limit(), offset, count * 2)) return null;
        int[] colors = new int[count];
        for (int y = 0; y < height; y += 4) {
            for (int x = 0; x < width; x += 4) {
                for (int pixel = 0; pixel < 16; pixel++) {
                    int sample = input.getShort(offset) & 0xffff;
                    offset += 2;
                    int argb;
                    if ((sample & 0x8000) != 0) {
                        int red = ((sample >> 10) & 31) * 255 / 31;
                        int green = ((sample >> 5) & 31) * 255 / 31;
                        int blue = (sample & 31) * 255 / 31;
                        argb = 0xff000000 | red << 16 | green << 8 | blue;
                    } else {
                        int alpha = ((sample >> 12) & 7) * 255 / 7;
                        argb = alpha << 24 | ((sample >> 8) & 15) * 17 << 16 |
                                ((sample >> 4) & 15) * 17 << 8 | (sample & 15) * 17;
                    }
                    colors[(y + pixel / 4) * width + x + pixel % 4] = argb;
                }
            }
        }
        return Bitmap.createBitmap(colors, width, height, Bitmap.Config.ARGB_8888);
    }
}
