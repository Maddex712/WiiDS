package dev.wiids.brawl;

/** Keeps Project+ selected across in-game restarts and pending save restores. */
public final class ProjectPlusLauncherActivity extends LauncherActivity {
    @Override protected boolean projectPlusCase() { return true; }
}
