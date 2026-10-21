// SDLActivity loads this small library normally. The Android linker loads the
// game into a reserved address range so the decomp's pointer-in-u32 fields
// work with its existing low stacks and MEM1 allocator on a 64-bit device.
#include <SDL.h>
#include <android/dlext.h>
#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <jni.h>

// The loaded game, for the AYN Thor lower screen's read-only snapshot (android/thor_snapshot.cpp).
static void* g_game;

extern "C" JNIEXPORT jint JNICALL
Java_dev_chasem_smslauncher_ThorSunshineScreen_nativeSnapshot(JNIEnv* env, jclass, jintArray out) {
    using Snapshot = int (*)(int32_t*, int);
    static Snapshot snapshot;
    if (!snapshot && g_game) snapshot = reinterpret_cast<Snapshot>(dlsym(g_game, "port_thor_snapshot"));
    if (!snapshot || !out) return 0;
    int32_t words[128];
    const int n = snapshot(words, 128);
    if (n > 0 && env->GetArrayLength(out) >= n) env->SetIntArrayRegion(out, 0, n, reinterpret_cast<jint*>(words));
    return n;
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_chasem_smslauncher_ThorSunshineScreen_nativeSky(JNIEnv* env, jclass, jobject buffer, jint have) {
    using Sky = uint32_t (*)(uint8_t*, int, uint32_t);
    static Sky sky;
    if (!sky && g_game) sky = reinterpret_cast<Sky>(dlsym(g_game, "port_thor_sky"));
    void* out = buffer ? env->GetDirectBufferAddress(buffer) : nullptr;
    if (!sky || !out) return 0;
    return static_cast<jint>(sky(static_cast<uint8_t*>(out), static_cast<int>(env->GetDirectBufferCapacity(buffer)),
                                 static_cast<uint32_t>(have)));
}



// The address space the library's segments span, from its program headers:
// all the linker needs reserved. 0 if the file cannot be read as ELF64.
static size_t load_span(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    size_t span = 0;
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof eh, 0) == (ssize_t)sizeof eh && !memcmp(eh.e_ident, ELFMAG, SELFMAG)
        && eh.e_ident[EI_CLASS] == ELFCLASS64 && eh.e_phentsize == sizeof(Elf64_Phdr)) {
        for (int i = 0; i < eh.e_phnum; i++) {
            Elf64_Phdr ph;
            if (pread(fd, &ph, sizeof ph, eh.e_phoff + (off_t)i * sizeof ph) != (ssize_t)sizeof ph) { span = 0; break; }
            if (ph.p_type == PT_LOAD && ph.p_vaddr + ph.p_memsz > span) span = ph.p_vaddr + ph.p_memsz;
        }
    }
    close(fd);
    return span;
}

// Reserves `bytes` (a multiple of 1 MiB) where every address fits 32 bits:
// first below the stacks' range (0x40000000), as before, then anywhere else
// in the low 4 GiB but MEM1's preferred 0x80000000. Android's own mappings
// (the Java heap, which a large heap makes larger, starts near 0x12c00000)
// can fill any one stretch.
static void* reserve_low(size_t bytes) {
    static const uint64_t ranges[][2] = {
        {0x10000000u, 0x40000000u}, {0x40000000u, 0x80000000u}, {0x84000000u, 0xf0000000u}};
    for (const auto& r : ranges)
        for (uint64_t at = r[0]; at + bytes <= r[1]; at += 1u << 20) {
            void* want = reinterpret_cast<void*>(static_cast<uintptr_t>(at));
#ifdef MAP_FIXED_NOREPLACE
            void* p = mmap(want, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
#else
            void* p = mmap(want, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
            if (p == want) return p;
            if (p != MAP_FAILED) munmap(p, bytes);
        }
    return MAP_FAILED;
}

// What occupies the low 4 GiB, for the log when nothing fits.
static void log_low_mappings() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return;
    char line[512];
    while (fgets(line, sizeof line, maps)) {
        unsigned long long from = 0, to = 0;
        if (sscanf(line, "%llx-%llx", &from, &to) == 2 && from < 0x100000000ull && to - from >= (1ull << 20))
            fprintf(stderr, "[android]   %s", line);
    }
    fclose(maps);
}

// The current area's photo (port_thor_photo), in the sky's format.
extern "C" JNIEXPORT jint JNICALL
Java_dev_chasem_smslauncher_ThorSunshineScreen_nativePhoto(JNIEnv* env, jclass, jobject buffer, jint have) {
    using Photo = uint32_t (*)(uint8_t*, int, uint32_t);
    static Photo photo;
    if (!photo && g_game) photo = reinterpret_cast<Photo>(dlsym(g_game, "port_thor_photo"));
    void* out = buffer ? env->GetDirectBufferAddress(buffer) : nullptr;
    if (!photo || !out) return 0;
    return static_cast<jint>(photo(static_cast<uint8_t*>(out), static_cast<int>(env->GetDirectBufferCapacity(buffer)),
                                   static_cast<uint32_t>(have)));
}

// The current area's floor triangles (port_thor_floor) into a direct float buffer; returns the
// triangle count and puts the export's sequence in seq[0]; 0 when nothing is newer than `have`.
extern "C" JNIEXPORT jint JNICALL
Java_dev_chasem_smslauncher_ThorSunshineScreen_nativeFloor(JNIEnv* env, jclass, jobject buffer, jint have, jintArray seq) {
    using Floor = int (*)(float*, int, uint32_t, uint32_t*);
    static Floor floor;
    if (!floor && g_game) floor = reinterpret_cast<Floor>(dlsym(g_game, "port_thor_floor"));
    void* out = buffer ? env->GetDirectBufferAddress(buffer) : nullptr;
    if (!floor || !out || !seq || env->GetArrayLength(seq) < 1) return 0;
    uint32_t got = 0;
    const int n = floor(static_cast<float*>(out), static_cast<int>(env->GetDirectBufferCapacity(buffer) / 4),
                        static_cast<uint32_t>(have), &got);
    if (n > 0) { jint v = static_cast<jint>(got); env->SetIntArrayRegion(seq, 0, 1, &v); }
    return n;
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    std::string data, libs, disc;
    bool selftest = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--data")) data = argv[i + 1];
        else if (!strcmp(argv[i], "--libraries")) libs = argv[i + 1];
        else if (!strcmp(argv[i], "--disc")) disc = argv[i + 1];
        else if (!strcmp(argv[i], "--gx-selftest")) selftest = !strcmp(argv[i + 1], "1");
        else if (!strcmp(argv[i], "--env")) {
            std::string e = argv[i + 1];
            auto at = e.find('=');
            if (at != std::string::npos) setenv(e.substr(0, at).c_str(), e.substr(at + 1).c_str(), 1);
        }
    }
    if (data.empty() || libs.empty() || disc.empty() || chdir(data.c_str())) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", "The game files could not be opened.", nullptr);
        return 1;
    }
    int log = open((data + "/game.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log >= 0) { dup2(log, STDOUT_FILENO); dup2(log, STDERR_FILENO); close(log); }
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (const char* policy = getenv("SMS_ANDROID_STARTUP_POLICY"))
        fprintf(stderr, "[android] startup policy: %s\n", policy);
    setenv("HOME", data.c_str(), 1);
    setenv("XDG_CACHE_HOME", (data + "/cache").c_str(), 1);
    setenv("SMS_GX_GL_THREAD", "0", 0); // Android SDL owns the surface's thread.
    setenv("SMS_MSAA", "0", 0);
    const std::string library = libs + (selftest ? "/libgx_selftest.so" : "/libsms.so");
    // The library's own span, rounded up to whole MiB; 128 MiB if unreadable.
    size_t bytes = load_span(library);
    bytes = bytes ? (bytes + (1u << 20) - 1) & ~size_t((1u << 20) - 1) : size_t(128) << 20;
    void* region = reserve_low(bytes);
    if (region == MAP_FAILED) {
        fprintf(stderr, "[android] could not reserve the game's compatibility address range (%zu MiB); mappings below 4 GiB:\n",
                bytes >> 20);
        log_low_mappings();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", "The game runtime could not reserve its address range.", nullptr);
        return 1;
    }
    android_dlextinfo info{};
    info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    info.reserved_addr = region;
    info.reserved_size = bytes;
    void* game = android_dlopen_ext(library.c_str(), RTLD_NOW | RTLD_LOCAL, &info);
    if (!game) {
        const char* error = dlerror();
        fprintf(stderr, "[android] loading libsms.so: %s\n", error ? error : "unknown error");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SMS Launcher", error ? error : "The game runtime could not be loaded.", nullptr);
        munmap(region, bytes);
        return 1;
    }
    g_game = game;
    auto entry = reinterpret_cast<int (*)(int, char**)>(dlsym(game, "SDL_main"));
    if (!entry) { fprintf(stderr, "[android] SDL_main missing: %s\n", dlerror()); return 1; }
    fprintf(stderr, "[android] 64-bit game loaded at %p (%zu MiB reserved), page size %ld\n", region, bytes >> 20,
            sysconf(_SC_PAGESIZE));
    if (selftest) {
        std::string output = data + "/gx-selftest.ppm";
        char* testArgs[] = {const_cast<char*>("gx_selftest"), const_cast<char*>("--window"), const_cast<char*>(output.c_str()), nullptr};
        int result = entry(3, testArgs);
        fprintf(stderr, "[android] GX self-test returned %d\n", result);
        return result;
    }
    char* args[] = {const_cast<char*>("sms"), const_cast<char*>(disc.c_str()), nullptr};
    return entry(2, args);
}
