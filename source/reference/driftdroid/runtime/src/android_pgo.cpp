// Profile recording for the Android profile-guided build (cmake -DMKW_PGO=generate).
//
// An Android app never returns from main, so the compiler runtime's exit-time profile write never
// happens. Write the counters on a timer instead, into the app's own files directory, from where
// a debuggable build's profile can be pulled with `adb shell run-as`. Each write replaces the file
// with the run's cumulative counts, so the last one before the app closes is the whole session.
// Pull it just after a write (the size stops changing), not during one.

#if defined(__ANDROID__) && defined(MKW_PGO_GENERATE)

#include "runtime_config.h"
#include "runtime_log.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <sys/stat.h>

extern "C" void __llvm_profile_set_filename(const char* name);
extern "C" int __llvm_profile_write_file(void);

void StartAndroidPgoRecording() {
    static std::string path;
    if (!path.empty()) {
        return;
    }
    const std::string dir = RuntimeConfigFile::AndroidFilesDir() + "/WiiCompiled/pgo";
    mkdir(dir.c_str(), 0700);
    path = dir + "/game.profraw";
    __llvm_profile_set_filename(path.c_str());
    RT_LOGF(RT_TAG_RUNTIME, "PGO recording build: profile written every 20 s to %s\n", path.c_str());
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            // The runtime appends to an existing file, and llvm-profdata would then add every
            // cumulative snapshot together, counting the start of the session many times over.
            std::remove(path.c_str());
            __llvm_profile_write_file();
        }
    }).detach();
}

#endif
