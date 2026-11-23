package dev.wiids.colosseum;

import com.driftdroid.android.dualscreen.WiiDsMenuView.Page;

/** The WiiDS pages for Colosseum. Values are read from and applied to the running host. */
final class ColosseumSettings {
    private static final int ASPECT = 0, VOLUME = 1, PRESENTATION = 2, SAVED = 3;
    private static final String[] ASPECT_NAMES = {"4:3", "16:9", "Auto"};
    private static final String[] RATE_NAMES = {"Original", "60 Hz", "120 Hz", "Display rate"};
    private static String status;

    private ColosseumSettings() {}

    private static native int[] read();
    private static native boolean apply(int aspect, int volume, int presentation);

    static boolean savePending() {
        int[] values = read();
        return values != null && values[SAVED] == 0;
    }

    static Page root(ColosseumGameActivity game) {
        status = null;
        return new Page("Paused")
                .tile("Resume", "play", game::closeMenu)
                .tile("Display", "display", () -> game.showMenu(display(game)))
                .tile("Audio", "audio", () -> game.showMenu(audio(game)))
                .tile("Controls", "pad", () -> game.showMenu(controls(game)))
                .tile("Saves", "save", () -> game.showMenu(saves(game)))
                .dangerTile("Return to WiiDS", "exit", () -> game.showMenu(exit(game)));
    }

    private static Page display(ColosseumGameActivity game) {
        int[] values = read();
        if (values == null) return starting(game);
        Runnable back = () -> game.showMenu(root(game).focus(1));
        return new Page("Display").back(back).note(() -> status)
                .row("Aspect ratio", () -> ASPECT_NAMES[values[ASPECT]], step -> {
                    values[ASPECT] = Math.floorMod(values[ASPECT] + step, ASPECT_NAMES.length);
                    commit(game, values);
                })
                .row("Presentation rate", () -> RATE_NAMES[values[PRESENTATION]], step -> {
                    values[PRESENTATION] = Math.floorMod(values[PRESENTATION] + step, RATE_NAMES.length);
                    commit(game, values);
                })
                .key("Back", "back", back);
    }

    private static Page audio(ColosseumGameActivity game) {
        int[] values = read();
        if (values == null) return starting(game);
        Runnable back = () -> game.showMenu(root(game).focus(2));
        return new Page("Audio").back(back).note(() -> status)
                .row("Master volume", () -> values[VOLUME] + "%", step -> {
                    int next = Math.max(0, Math.min(100, (values[VOLUME] + 5) / 10 * 10 + step * 10));
                    if (next == values[VOLUME]) return;
                    values[VOLUME] = next;
                    commit(game, values);
                })
                .key("Back", "back", back);
    }

    private static void commit(ColosseumGameActivity game, int[] values) {
        if (!apply(values[ASPECT], values[VOLUME], values[PRESENTATION])) {
            game.showMenu(starting(game));
            return;
        }
        status = null;
        game.checkSettingsSaved(() -> {
            int[] result = read();
            status = result != null && result[SAVED] == 1 ? null : "Couldn’t save settings. Your change applies to this session.";
        });
    }

    private static Page controls(ColosseumGameActivity game) {
        Runnable back = () -> game.showMenu(root(game).focus(3));
        Page page = new Page("Controls").back(back).note(
                "Left stick or D-pad moves. A confirms, B goes back, and Start opens the game menu. "
                        + "Hold R3 to fast-forward. Back or Select opens WiiDS.");
        if (!game.hasCompanion()) page.row("Touch controls", () -> game.hasUpperTouchControls() ? "On" : "Off",
                step -> game.setUpperTouchControls(!game.hasUpperTouchControls()));
        return page.key("Back", "back", back);
    }

    private static Page saves(ColosseumGameActivity game) {
        Runnable back = () -> game.showMenu(root(game).focus(4));
        return new Page("Saves").back(back)
                .note("Save at an in-game PC. Save Manager backs up or restores this memory card.")
                .key("Back", "back", back)
                .primaryKey("Open Save Manager", "save", () -> {
                    Runnable cancel = () -> game.showMenu(saves(game));
                    game.showMenu(new Page("Leave the game?").back(cancel)
                            .note("Save Manager opens after the game closes. Save at a PC first to keep your progress.")
                            .key("Back", "back", cancel)
                            .primaryKey("Open Save Manager", "save", game::leaveForSaveManager));
                });
    }

    private static Page exit(ColosseumGameActivity game) {
        return new Page("Return to WiiDS?").back(() -> game.showMenu(root(game).focus(5)))
                .note("Only progress you’ve saved at a PC will carry over.")
                .key("Keep playing", "play", game::closeMenu)
                .primaryKey("Return to WiiDS", "exit", game::leaveGame);
    }

    static Page closing() { return new Page("Closing").note("Returning to WiiDS…"); }

    private static Page starting(ColosseumGameActivity game) {
        Runnable back = () -> game.showMenu(root(game));
        return new Page("Starting").back(back).note("Colosseum is still preparing the game.").key("Back", "back", back);
    }
}
