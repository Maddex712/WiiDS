package com.wither.nsmbw;

/** Read-only lower-display data. Field validity is explicit; unavailable is never a fake zero. */
public final class ThorGameState {
    public static final int SCENE = 1, COURSE = 2, PLAYER_ONE = 4, TIMER = 8, STOP_FLAGS = 16,
            PROGRESS = 32, STAR_COINS = 64, SCORE = 128, SAVED_STAR_COINS = 256;
    public final long sequence;
    public final int fields, scene, mode, world, stage, character, lives, coins, seconds;
    public final boolean gameStopped;
    public final int progress, starCoins, savedStarCoins, score, area, checkpoint, playerXQ4, areaWidthQ4;

    private ThorGameState(int[] data) {
        sequence = Integer.toUnsignedLong(data[1]);
        fields = data[2]; scene = data[3]; mode = data[4]; world = data[5]; stage = data[6];
        character = data[7]; lives = data[8]; coins = data[9]; seconds = data[10]; gameStopped = data[11] != 0;
        progress = data[12]; starCoins = data[13]; savedStarCoins = data[14]; score = data[15];
        area = data[16]; checkpoint = data[17]; playerXQ4 = data[18]; areaWidthQ4 = data[19];
    }
    public boolean has(int field) { return (fields & field) == field; }

    /** Call after SDL loaded the native library. Data is copied under a short native mutex. */
    public static ThorGameState read() {
        final int[] data = GameActivity.nativeReadThorState();
        return data != null && data.length == 20 && data[0] == 2 ? new ThorGameState(data) : null;
    }
}
