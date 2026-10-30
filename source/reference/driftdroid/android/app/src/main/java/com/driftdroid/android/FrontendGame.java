package com.driftdroid.android;

/** Stable frontend IDs. Never use carousel indices or a caller-supplied class name. */
public enum FrontendGame {
    MARIO_KART("mkw", "Mario Kart Wii", "original"),
    RETRO_REWIND("retro-rewind", "Retro Rewind", "retro-rewind"),
    NSMB("nsmbw", "New Super Mario Bros. Wii", "nsmbw"),
    MSMBWII("msmbwii", "MSMBWii: Just a Little Adventure", "msmbwii"),
    SUNSHINE("sunshine", "Super Mario Sunshine", "sunshine"),
    ECLIPSE("eclipse", "Super Mario Eclipse", "eclipse"),
    GALAXY("galaxy", "Super Mario Galaxy", "galaxy"),
    KAIZO("kaizo", "Kaizo Mario Galaxy Rebalanced", "kaizo"),
    BRAWL("brawl", "Super Smash Bros. Brawl", "brawl");

    public final String id;
    public final String label;
    public final String artwork;
    FrontendGame(String id, String label, String artwork) {
        this.id = id;
        this.label = label;
        this.artwork = artwork;
    }

    public static FrontendGame fromId(String id) {
        if (id == null) return null;
        for (FrontendGame game : values()) if (game.id.equals(id)) return game;
        return null;
    }
}
