// In-game warp menu (F8, or a controller's Back/Select button, with the debug
// overlay open), drawn with the debug overlay's renderer: every stage on the
// disc, then its episodes, and a warp to the one picked
// (src/port_include/port_warp.h).
#ifndef SMS_GX_WARP_MENU_H
#define SMS_GX_WARP_MENU_H

union SDL_Event;

namespace gx {

#ifdef SMS_GX_HAVE_SDL2
bool warpMenuVisible();
void warpMenuToggle();
// True when the event was the menu's and must not reach the game.
bool warpMenuHandleEvent(const SDL_Event& ev);
// Once per event pump: held-direction repeats and the list once the game has it.
void warpMenuTick();
// On the GL thread, between presenting the frame and the swap.
void warpMenuRender(int winW, int winH);
#else
inline bool warpMenuVisible() { return false; }
inline void warpMenuToggle() {}
inline bool warpMenuHandleEvent(const SDL_Event&) { return false; }
inline void warpMenuTick() {}
inline void warpMenuRender(int, int) {}
#endif

}  // namespace gx

#endif
