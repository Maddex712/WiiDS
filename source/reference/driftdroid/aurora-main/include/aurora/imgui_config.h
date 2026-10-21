#pragma once

#define IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS 1

#if defined(__SWITCH__)
// devkitA64/libnx homebrew has no fork()/execvp() (no process model to spawn xdg-open into) and
// no shell to open a file browser in anyway - a real "undefined reference to waitpid/execvp"
// link error otherwise, since imgui's default Platform_OpenInShellFn always takes the
// fork+exec("xdg-open", ...) path outside of _WIN32/__APPLE__.
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS 1
#endif

typedef struct SDL_IOStream SDL_IOStream;
typedef SDL_IOStream* ImFileHandle;
typedef unsigned long long ImU64;

ImFileHandle ImFileOpen(const char* filename, const char* mode);
bool ImFileClose(ImFileHandle file);
ImU64 ImFileGetSize(ImFileHandle file);
ImU64 ImFileRead(void* data, ImU64 size, ImU64 count, ImFileHandle file);
ImU64 ImFileWrite(const void* data, ImU64 size, ImU64 count, ImFileHandle file);
