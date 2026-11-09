package dev.wiids.brawl;

/** Same compiled Brawl runtime, with a separate process and Project+ data directory. */
public final class ProjectPlusGameActivity extends GameActivity {
    @Override protected boolean projectPlusCase() { return true; }
}
