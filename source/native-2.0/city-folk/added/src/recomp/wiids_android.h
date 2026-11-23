#pragma once

namespace recomp::wiimote { struct State; }
namespace recomp { struct Ctx; }
namespace wiids {
bool quitRequested();
bool paused();
void applyInput(recomp::wiimote::State& state);
void syncSettings();
// Read-only live state feed for the companion screen (CityFolkState.read; wiids_android.cpp).
// Guest thread: every layout root drawn (cf_hud.cpp), and once per guest frame (cf_game.cpp).
// True: the layout belongs to the guest Map screen and the lower screen owns the map
// (CityFolkGameActivity.setLowerOwnsMap): the caller skips drawing it.
bool noteLayoutDrawn(unsigned rootPane, unsigned callerLr);
void publishState(recomp::Ctx& c);
// Host thread, once per main-loop frame: times for the logcat summary (debug.wiids.cf.perf=1).
void perfFrame(double beginMs, double runMs, double endMs);
// Guest thread: the circle wipe is drawn this frame (its frame byte; 0xFF = fully closed).
void noteWipe(unsigned frameByte);
}
