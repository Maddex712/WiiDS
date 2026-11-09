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
    BRAWL("brawl", "Super Smash Bros. Brawl", "brawl"),
    MKDD("mkdd", "Mario Kart: Double Dash!!", "mkdd"),
    MKDD_PLUS("mkdd-plus", "Mario Kart: Double Dash!! Plus", "mkdd-plus"),
    PROJECT_PLUS("project-plus", "Project+", "project-plus");

    public final String id;
    public final String label;
    public final String artwork;
    FrontendGame(String id, String label, String artwork) {
        this.id = id;
        this.label = label;
        this.artwork = artwork;
    }

    /** Longest stable ID wins: mkdd-plus backups must never be treated as mkdd. */
    public static FrontendGame fromBackupFilename(String name) {
        if (name == null || !name.endsWith(".zip")) return null;
        FrontendGame found = null;
        for (FrontendGame game : values())
            if (name.startsWith(game.id + "-") && (found == null || game.id.length() > found.id.length())) found = game;
        return found;
    }

    public static FrontendGame fromId(String id) {
        if (id == null) return null;
        for (FrontendGame game : values()) if (game.id.equals(id)) return game;
        return null;
    }
}
