#pragma once

#include <SDL.h>

namespace gx {
// Keep Windows borderless mode a normal window. Even desktop fullscreen can
// take a driver-specific fullscreen path; this never requests a display mode
// or calls SDL_SetWindowFullscreen. The window is one row taller than the
// monitor (the extra row is off screen): a window exactly the monitor's size
// is still promoted to the driver's and DWM's fullscreen flip, whose switches
// blank the monitor at launch and on every Alt-Tab back in.
struct BorderlessWindow {
    static constexpr int kExtraRows = 1;

    bool active = false;
    SDL_Rect saved = {};
    Uint32 savedFlags = 0;

    bool enter(SDL_Window* window) {
        if (active) return true;
        SDL_Rect bounds;
        const int display = SDL_GetWindowDisplayIndex(window);
        if (display < 0 || SDL_GetDisplayBounds(display, &bounds) != 0) return false;
        SDL_GetWindowPosition(window, &saved.x, &saved.y);
        SDL_GetWindowSize(window, &saved.w, &saved.h);
        savedFlags = SDL_GetWindowFlags(window);
        SDL_SetWindowBordered(window, SDL_FALSE);
        SDL_SetWindowResizable(window, SDL_FALSE);
        SDL_SetWindowPosition(window, bounds.x, bounds.y);
        SDL_SetWindowSize(window, bounds.w, bounds.h + kExtraRows);
        active = true;
        return true;
    }

    void leave(SDL_Window* window) {
        if (!active) return;
        SDL_SetWindowResizable(window, savedFlags & SDL_WINDOW_RESIZABLE ? SDL_TRUE : SDL_FALSE);
        SDL_SetWindowBordered(window, savedFlags & SDL_WINDOW_BORDERLESS ? SDL_FALSE : SDL_TRUE);
        SDL_SetWindowSize(window, saved.w, saved.h);
        SDL_SetWindowPosition(window, saved.x, saved.y);
        active = false;
    }
};
}  // namespace gx
