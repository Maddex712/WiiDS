package com.driftdroid.android;

/** UI-thread policy for the in-game gear. Times use Android's monotonic uptime. */
public final class SettingsButtonAutoHide {
    public static final long IDLE_MILLIS = 5000;

    private boolean active;
    private boolean started;
    private boolean manuallyHidden;
    private boolean settingsOpen;
    private boolean editing;
    private int pointers;
    private long hideAt;

    public void setActive(boolean value, long now) {
        if (active == value) return;
        active = value;
        pointers = 0;
        if (value && !started) {
            started = true;
            hideAt = now + IDLE_MILLIS;
        }
    }

    public void setManuallyHidden(boolean value, long now) {
        if (manuallyHidden == value) return;
        manuallyHidden = value;
        if (!value) hideAt = now + IDLE_MILLIS;
    }

    public void setSettingsOpen(boolean value, long now) {
        if (settingsOpen == value) return;
        settingsOpen = value;
        if (!value) hideAt = now + IDLE_MILLIS;
    }

    public void setEditing(boolean value, long now) {
        if (editing == value) return;
        editing = value;
        if (!value) hideAt = now + IDLE_MILLIS;
    }

    public void setPointers(int count, long now) {
        pointers = Math.max(0, count);
        reveal(now);
    }

    public void reveal(long now) {
        hideAt = now + IDLE_MILLIS;
    }

    public boolean visible(long now) {
        return active && !manuallyHidden && (heldOpen() || now < hideAt);
    }

    /** Zero means no callback is required until another UI event changes the policy. */
    public long nextUpdateDelay(long now) {
        return active && !manuallyHidden && !heldOpen() ? Math.max(0, hideAt - now) : 0;
    }

    private boolean heldOpen() {
        return settingsOpen || editing || pointers > 0;
    }
}
