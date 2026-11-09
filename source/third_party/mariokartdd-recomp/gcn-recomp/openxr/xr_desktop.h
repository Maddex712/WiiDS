// PC VR: the OpenXR frontend (xr_frontend.h) on the desktop's own GL context, entered from
// main.cpp with --vr. See xr_desktop.cpp.
#pragma once

struct SDL_Window;
struct PadState;

// What main.cpp already has and the headset path reuses.
struct XrDesktopHost {
    SDL_Window* window;             // its GL context current; kept up as a mirror
    bool (*pump)();                 // the window's events; false once the user quits
    void (*merge_input)(PadState&); // the keyboard and a gamepad, beside the headset's controllers
};

// Brings the headset up, boots the game from `iso` and runs it until the window is closed or
// the runtime ends the session. Returns the process's exit code.
int xr_desktop_main(const XrDesktopHost& host, const char* iso);
