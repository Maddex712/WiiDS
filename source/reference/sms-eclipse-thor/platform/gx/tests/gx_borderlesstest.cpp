// Windows borderless window regression; no game disc or OpenGL required.
#include "gx_borderless_window.h"
#include <cstdio>

static int failures = 0;
static void expect(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); ++failures; }
}

static bool waitForRectangle(SDL_Window* window, SDL_Rect rectangle) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        SDL_PumpEvents();
        SDL_Rect actual;
        SDL_GetWindowPosition(window, &actual.x, &actual.y);
        SDL_GetWindowSize(window, &actual.w, &actual.h);
        if (actual.x == rectangle.x && actual.y == rectangle.y &&
            actual.w == rectangle.w && actual.h == rectangle.h) return true;
        SDL_Delay(10);
    }
    SDL_Rect actual;
    SDL_GetWindowPosition(window, &actual.x, &actual.y);
    SDL_GetWindowSize(window, &actual.w, &actual.h);
    std::fprintf(stderr, "Window is %dx%d at %d,%d; wanted %dx%d at %d,%d\n",
                 actual.w, actual.h, actual.x, actual.y, rectangle.w, rectangle.h, rectangle.x, rectangle.y);
    return false;
}

int main(int, char**) {
    expect(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video initializes");
    SDL_Window* window = SDL_CreateWindow("Borderless regression", 40, 60, 480, 360, SDL_WINDOW_RESIZABLE);
    expect(window != nullptr, "normal window opens");
    if (!window) return 1;
    // Let the window manager finish mapping and decorating the initial window.
    for (int i = 0; i < 20; ++i) { SDL_PumpEvents(); SDL_Delay(10); }
    SDL_Rect original, bounds;
    SDL_GetWindowPosition(window, &original.x, &original.y);
    SDL_GetWindowSize(window, &original.w, &original.h);
    const int display = SDL_GetWindowDisplayIndex(window);
    expect(SDL_GetDisplayBounds(display, &bounds) == 0, "monitor bounds available");
    SDL_DisplayMode before = {}, after = {};
    expect(SDL_GetCurrentDisplayMode(display, &before) == 0, "initial display mode available");
    gx::BorderlessWindow borderless;
    for (int i = 0; i < 3; ++i) {
        expect(borderless.enter(window), "borderless enters");
        expect(borderless.enter(window), "repeated enter preserves restore rectangle");
        const Uint32 flags = SDL_GetWindowFlags(window);
        expect(!(flags & SDL_WINDOW_FULLSCREEN), "borderless never requests SDL fullscreen");
        expect(flags & SDL_WINDOW_BORDERLESS, "window decorations removed");
        expect(!(flags & SDL_WINDOW_RESIZABLE), "borderless window cannot be resized");
        SDL_Rect covering = bounds;
        covering.h += gx::BorderlessWindow::kExtraRows;
        expect(waitForRectangle(window, covering), "borderless covers the monitor, one row taller");
        expect(SDL_GetCurrentDisplayMode(display, &after) == 0 &&
               before.w == after.w && before.h == after.h && before.refresh_rate == after.refresh_rate &&
               before.format == after.format, "borderless leaves display mode unchanged");
        borderless.leave(window);
        borderless.leave(window);
        expect(waitForRectangle(window, original), "decorated window rectangle restored");
        expect(!(SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS), "window decorations restored");
        expect(SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE, "native resizing restored");
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("Borderless window: %d failures\n", failures);
    return failures ? 1 : 0;
}
