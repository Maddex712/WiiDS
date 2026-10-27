package com.wither.nsmbw;

/** Actual guest-thread world graph. Links retain the game's sparse point IDs. */
final class ThorWorldMap {
    final int sequence, fields, world, submap;
    final float playerX, playerZ, minX, maxX, minZ, maxZ;
    final Point[] points;
    final Point[] byId = new Point[192];
    static final class Point {
        final int id, kind;
        final String name;
        final float x, z;
        final int[] links = new int[6];
        Point(int[] words, int at) {
            id = words[at]; kind = words[at + 1];
            int n = words[at + 2];
            name = new String(new char[]{(char)((n >>> 24) & 255), (char)((n >>> 16) & 255),
                    (char)((n >>> 8) & 255), (char)(n & 255), (char)(words[at + 3] & 255)}).replace("\0", "");
            x = Float.intBitsToFloat(words[at + 4]); z = Float.intBitsToFloat(words[at + 5]);
            System.arraycopy(words, at + 6, links, 0, 6);
        }
    }
    private ThorWorldMap(int[] words) {
        sequence = words[1]; fields = words[2]; world = words[3]; submap = words[4];
        playerX = Float.intBitsToFloat(words[6]); playerZ = Float.intBitsToFloat(words[7]);
        minX = Float.intBitsToFloat(words[8]); maxX = Float.intBitsToFloat(words[9]);
        minZ = Float.intBitsToFloat(words[10]); maxZ = Float.intBitsToFloat(words[11]);
        points = new Point[words[5]];
        for (int i = 0; i < points.length; i++) {
            Point point = new Point(words, 12 + i * 12);
            if (point.id < 0 || point.id >= 192 || byId[point.id] != null
                    || !Float.isFinite(point.x) || !Float.isFinite(point.z)) throw new IllegalArgumentException("Invalid map point");
            points[i] = point; byId[point.id] = point;
        }
    }
    boolean hasPlayer() { return (fields & 2) != 0; }
    /** Exact selected-save flags; older/unavailable snapshots establish only the current world. */
    int unlockedWorlds() {
        return (fields & 4) != 0 ? (fields >>> 8) & 0x1ff
                : world >= 0 && world < 9 ? 1 << world : 0;
    }
    static ThorWorldMap read() {
        int[] words = GameActivity.nativeReadThorWorldMap();
        if (words == null || words.length < 12 || words[0] != 1 || (words[2] & 1) == 0
                || words[5] < 1 || words[5] > 192 || words.length != 12 + words[5] * 12) return null;
        try {
            ThorWorldMap map = new ThorWorldMap(words);
            if (!Float.isFinite(map.minX) || !Float.isFinite(map.maxX) || !Float.isFinite(map.minZ)
                    || !Float.isFinite(map.maxZ) || map.minX > map.maxX || map.minZ > map.maxZ
                    || (map.hasPlayer() && (!Float.isFinite(map.playerX) || !Float.isFinite(map.playerZ)))) return null;
            return map;
        } catch (IllegalArgumentException invalid) { return null; }
    }
}
