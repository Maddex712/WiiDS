// Tiny NativeActivity launcher.
//
// The game code assumes a 32-bit address space (it stores pointers in u32
// fields and uses Wii memory addresses directly). Reserve an unused B0000000-
// E0000000 window and load libgame.so at C8000000. No preloader is used, and
// an occupied window is always refused without replacing Android mappings.
#include <android/dlext.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "PetariVR", __VA_ARGS__)
#define FATAL(...)                                                                                                                                   \
    do {                                                                                                                                             \
        __android_log_print(ANDROID_LOG_FATAL, "PetariVR", __VA_ARGS__);                                                                             \
        abort();                                                                                                                                     \
    } while (0)

static const uintptr_t kWindowBase = 0xB0000000u;
static const size_t kWindowSize = 0x30000000u;
static const uintptr_t kLibBase = 0xC8000000u;
static const size_t kLibSize = 0x08000000u;

typedef void (*PortAndroidMain)(struct android_app* app, uintptr_t windowBase, size_t windowSize);

void android_main(struct android_app* app) {
    // Serialize play against the package's importer/migration FileChannel lock.
    // Keep the descriptor for the lifetime of the game process.
    char lockPath[1024];
    snprintf(lockPath, sizeof(lockPath), "%s/.galaxy-runtime.lock", app->activity->internalDataPath);
    int lockFd = open(lockPath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    struct flock guard;
    memset(&guard, 0, sizeof(guard));
    guard.l_type = F_WRLCK;
    guard.l_whence = SEEK_SET;
    if (lockFd < 0 || fcntl(lockFd, F_SETLK, &guard) != 0) {
        LOG("Galaxy data is busy; close the importer before playing");
        if (lockFd >= 0) close(lockFd);
        ANativeActivity_finish(app->activity);
        return;
    }
    void* win = mmap((void*)kWindowBase, kWindowSize, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (win != (void*)kWindowBase) {
        if (win != MAP_FAILED) munmap(win, kWindowSize);
        FATAL("Galaxy memory window is occupied; safely refusing to replace Android mappings");
    }
    LOG("complete Galaxy address window safely reserved");

    // libgame.so lives next to us, unless a developer build was pushed to the
    // app's private files directory (must be read-only for dlopen on Android 14).
    char path[512];
    const char* dataDir = app->activity->internalDataPath;
    snprintf(path, sizeof(path), "%s/libgame.so", dataDir ? dataDir : "");
    struct stat st;
    if (!dataDir || stat(path, &st) != 0) {
        Dl_info info;
        if (!dladdr((void*)android_main, &info) || !info.dli_fname) {
            FATAL("dladdr failed");
        }
        size_t used = (size_t)snprintf(path, sizeof(path), "%s", info.dli_fname);
        char* slash = strrchr(path, '/');
        if (!slash) {
            FATAL("bad library path %s", path);
        }
        if (used >= sizeof(path)) {
            // Truncated: appending the name would either overflow the buffer or
            // dlopen a path that is not the real library location.
            FATAL("library path too long for %zu bytes: %s", sizeof(path), path);
        }
        snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "libgame.so");
    } else {
        LOG("using developer libgame.so from %s", path);
    }

    android_dlextinfo ext;
    memset(&ext, 0, sizeof(ext));
    ext.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    ext.reserved_addr = (void*)kLibBase;
    ext.reserved_size = kLibSize;
    void* lib = android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &ext);
    if (!lib) {
        FATAL("android_dlopen_ext(%s) failed: %s", path, dlerror());
    }
    PortAndroidMain entry = (PortAndroidMain)dlsym(lib, "port_android_main");
    if (!entry) {
        FATAL("port_android_main not found: %s", dlerror());
    }
    LOG("libgame.so loaded at %p", (void*)kLibBase);
    entry(app, kWindowBase, kWindowSize);
}
