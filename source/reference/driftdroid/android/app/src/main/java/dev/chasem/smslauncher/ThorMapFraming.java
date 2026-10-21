package dev.chasem.smslauncher;

import java.util.Arrays;

/** Static overview framing, using the floor export's world coordinates unchanged.
 * This only selects presentation geometry; it never changes collision or player positions.
 */
final class ThorMapFraming {
    private static final int STRIDE = 11;
    private ThorMapFraming() {}

    static final class Fit {
        final boolean[] draw, frame;
        final float minX, maxX, minZ, maxZ, lowY, highY, k, ox, oz;
        Fit(boolean[] draw, boolean[] frame, float minX, float maxX,
                float minZ, float maxZ, float lowY, float highY, int width, int height) {
            this.draw = draw; this.frame = frame;
            this.minX = minX; this.maxX = maxX; this.minZ = minZ; this.maxZ = maxZ;
            this.lowY = lowY; this.highY = highY;
            // At least 1000 world units prevents tiny isolated meshes from becoming enormous.
            k = Math.min(width * .90f / Math.max(1000f, maxX - minX),
                    height * .88f / Math.max(1000f, maxZ - minZ));
            ox = width / 2f - (minX + maxX) * .5f * k;
            oz = height / 2f - (minZ + maxZ) * .5f * k;
        }
    }

    static Fit fit(float[] f, int n, int width, int height) {
        if (f == null || n <= 0 || n > f.length / STRIDE || width <= 0 || height <= 0) return null;
        boolean[] draw = new boolean[n], frame = new boolean[n];
        int[] water = new int[n]; int waters = 0, count = 0;
        float[] cx = new float[n], cz = new float[n], ys = new float[n];
        for (int i = 0; i < n; i++) {
            int o = i * STRIDE;
            if (!finite(f, o) || f[o + 8] < .35f || area(f, o) < .01f) continue;
            draw[i] = true;
            if (f[o + 10] != 0) water[waters++] = o;
        }
        for (int i = 0; i < n; i++) {
            int o = i * STRIDE;
            if (!draw[i] || f[o + 10] != 0) continue;
            float x = (f[o] + f[o + 2] + f[o + 4]) / 3f;
            float z = (f[o + 1] + f[o + 3] + f[o + 5]) / 3f;
            boolean submerged = false;
            for (int j = 0; j < waters; j++) {
                int q = water[j];
                // Test local water coverage: a high swimming pool must not erase the island.
                if (f[o + 6] < f[q + 6] - 1f && contains(f, q, x, z)) {
                    submerged = true; break;
                }
            }
            if (submerged) { draw[i] = false; continue; }
            frame[i] = true; cx[count] = x; cz[count] = z; ys[count++] = f[o + 6];
        }
        if (count < 3) return null;
        Arrays.sort(cx, 0, count); Arrays.sort(cz, 0, count);
        // A small distant scenery mesh must not frame miles of empty sea. Use a robust core
        // only to identify remote outliers, then fit ALL vertices of the retained geometry.
        // The 25% expansion retains peripheral ramps and nearby disconnected buildings.
        int trim = count >= 100 ? Math.max(1, count / 100) : 0;
        float left = cx[trim], right = cx[count - 1 - trim];
        float top = cz[trim], bottom = cz[count - 1 - trim];
        float padX = Math.max(500f, (right - left) * .25f);
        float padZ = Math.max(500f, (bottom - top) * .25f);
        float minX = Float.POSITIVE_INFINITY, minZ = minX;
        float maxX = Float.NEGATIVE_INFINITY, maxZ = maxX; int kept = 0;
        for (int i = 0; i < n; i++) {
            if (!frame[i]) continue;
            int o = i * STRIDE;
            float x = (f[o] + f[o + 2] + f[o + 4]) / 3f;
            float z = (f[o + 1] + f[o + 3] + f[o + 5]) / 3f;
            if (x < left - padX || x > right + padX || z < top - padZ || z > bottom + padZ) {
                frame[i] = false; draw[i] = false; continue;
            }
            ys[kept++] = f[o + 6];
            for (int v = 0; v < 3; v++) {
                minX = Math.min(minX, f[o + v * 2]); maxX = Math.max(maxX, f[o + v * 2]);
                minZ = Math.min(minZ, f[o + v * 2 + 1]); maxZ = Math.max(maxZ, f[o + v * 2 + 1]);
            }
        }
        if (kept < 3) return null;
        Arrays.sort(ys, 0, kept);
        return new Fit(draw, frame, minX, maxX, minZ, maxZ, ys[(int)(kept * .03f)],
                ys[Math.min(kept - 1, (int)(kept * .97f))], width, height);
    }

    private static boolean finite(float[] f, int o) {
        for (int k = 0; k < STRIDE; k++) if (!Float.isFinite(f[o + k])) return false;
        return true;
    }
    private static float area(float[] f, int o) {
        return Math.abs((f[o + 2] - f[o]) * (f[o + 5] - f[o + 1])
                - (f[o + 4] - f[o]) * (f[o + 3] - f[o + 1])) * .5f;
    }
    private static boolean contains(float[] f, int o, float x, float z) {
        double a = side(f[o], f[o + 1], f[o + 2], f[o + 3], x, z);
        double b = side(f[o + 2], f[o + 3], f[o + 4], f[o + 5], x, z);
        double c = side(f[o + 4], f[o + 5], f[o], f[o + 1], x, z);
        return (a >= -.01 && b >= -.01 && c >= -.01) || (a <= .01 && b <= .01 && c <= .01);
    }
    private static double side(float ax, float az, float bx, float bz, float x, float z) {
        return ((double)bx - ax) * (z - az) - ((double)bz - az) * (x - ax);
    }
}
