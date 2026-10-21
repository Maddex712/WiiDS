#ifndef PORT_SAVESTATE_MENU_H
#define PORT_SAVESTATE_MENU_H

// The save state menu the game draws in its pause menu (src/savestate_menu.cpp).
// The pause menu opens it (SAVE_STATE pressed there, patch zzzz-savestate-01),
// gives it the pad while it is open, and draws it over itself (patch
// zzzz-savestate-02).

class TMarioGamePad;
namespace JDrama {
struct TRect;
}

bool SaveStateMenuIsOpen();
void SaveStateMenuOpen();
void SaveStateMenuClose();
// Once a frame while it is open: the pad's presses and the animations.
void SaveStateMenuMove(TMarioGamePad* pad);
// The pause menu's fade, as it draws itself; the menu fades with it.
void SaveStateMenuSetAlpha(u8 alpha);
// Last in the frame (TApplication::gameLoop, after the HUD and the fader), so
// nothing of the paused game draws over it; screen is the 2D area.
void SaveStateMenuDraw(const JDrama::TRect& screen);

#endif
