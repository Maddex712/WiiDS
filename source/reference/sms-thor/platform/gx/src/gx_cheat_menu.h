// In-game cheat menu (F9), drawn with the debug overlay's renderer. It edits the
// port_cheat_* flags (platform/misc/port_cheats.cpp).
#ifndef SMS_GX_CHEAT_MENU_H
#define SMS_GX_CHEAT_MENU_H

union SDL_Event;

namespace gx {

#ifdef SMS_GX_HAVE_SDL2
bool cheatMenuVisible();
void cheatMenuToggle();
// True when the event was the menu's and must not reach the game.
bool cheatMenuHandleEvent(const SDL_Event& ev);
// On the GL thread, between presenting the frame and the swap.
void cheatMenuRender(int winW, int winH);
#else
inline bool cheatMenuVisible() { return false; }
inline void cheatMenuToggle() {}
inline bool cheatMenuHandleEvent(const SDL_Event&) { return false; }
inline void cheatMenuRender(int, int) {}
#endif

}  // namespace gx

#endif
