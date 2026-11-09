package dev.thor.mkdd;

/** Same WiiDS surface and menu, with the exact Plus translation and an independent memory card. */
public final class WiiDsMkddPlusGameActivity extends WiiDsMkddGameActivity {
    @Override protected boolean isPlus() { return true; }
}
