#ifdef __ANDROID__
#include "performance_session.h"
#include "performance_cores.h"
#include "port_android_performance.h"
#include <atomic>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <unistd.h>

namespace {
std::string cpuList(const cpu_set_t& mask) {
    std::string ids;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &mask)) {
        if (!ids.empty()) ids += ',';
        ids += std::to_string(cpu);
    }
    return ids.empty() ? "none" : ids;
}
uint32_t cpuWeight(int cpu, const char* node) {
    char path[160];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/%s", cpu, node);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    unsigned long long value = 0;
    const bool valid = fscanf(f, "%llu", &value) == 1 && value > 0 && value <= UINT32_MAX;
    fclose(f);
    return valid ? uint32_t(value) : 0;
}
void configurePerformanceCores() {
    // This is per native thread and independent of hint-session availability.
    // Check only once; the launcher starts a fresh process after settings change.
    static thread_local bool attempted = false;
    if (attempted) return;
    attempted = true;
    const char* enabled = getenv("SMS_ANDROID_PERFORMANCE_CORES");
    if (!enabled || !*enabled || enabled[0] == '0') {
        fprintf(stderr, "[android] performance cores: disabled for TID %d\n", gettid());
        return;
    }
    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) {
        fprintf(stderr, "[android] performance cores: TID %d cannot read allowed CPUs: %s\n", gettid(), strerror(errno));
        return;
    }
    std::vector<port_android::CpuWeight> weights;
    const char* ranking = nullptr;
    // Require complete metadata from one source. Never mix capacity and MHz.
    for (const char* node : {"cpu_capacity", "cpufreq/cpuinfo_max_freq"}) {
        weights.clear();
        bool complete = true;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &allowed)) {
            const uint32_t weight = cpuWeight(cpu, node);
            weights.push_back({cpu, weight});
            if (!weight) complete = false;
        }
        if (complete && !weights.empty()) { ranking = node; break; }
    }
    const auto selected = ranking ? port_android::performanceCores(weights) : std::vector<int>{};
    if (selected.empty()) {
        fprintf(stderr, "[android] performance cores: TID %d unchanged, allowed CPUs %s; "
                        "no clear multi-core performance tier (%s)\n",
                gettid(), cpuList(allowed).c_str(), ranking ? ranking : "metadata unavailable");
        return;
    }
    cpu_set_t requested;
    CPU_ZERO(&requested);
    for (const int cpu : selected) CPU_SET(cpu, &requested);
    if (sched_setaffinity(0, sizeof requested, &requested) != 0) {
        fprintf(stderr, "[android] performance cores: TID %d request CPUs %s rejected: %s\n",
                gettid(), cpuList(requested).c_str(), strerror(errno));
        return;
    }
    cpu_set_t effective;
    const bool verified = sched_getaffinity(0, sizeof effective, &effective) == 0;
    fprintf(stderr, "[android] performance cores: TID %d, ranking %s, allowed CPUs %s, "
                    "requested CPUs %s, effective CPUs %s; clocks remain OS-controlled\n",
            gettid(), ranking, cpuList(allowed).c_str(), cpuList(requested).c_str(),
            verified ? cpuList(effective).c_str() : "unverified");
}
std::atomic<int32_t> renderTid{0};
port_android::HintSession* hints = nullptr;
bool initialized = false, unavailable = false, logTiming = false, headroom = false;
int32_t frameTid = 0, sessionGameTid = 0, sessionRenderTid = 0;
int64_t frameStart = 0, cpuStart = 0, frameTarget = 0, sessionTarget = 0;
unsigned samples = 0;
int64_t sumWork = 0, sumCpu = 0, maxWork = 0, sampleTarget = 0;
int64_t cpuNs() {
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
long cpuKhz(int cpu) {
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    long frequency = -1;
    if (fscanf(f, "%ld", &frequency) != 1) frequency = -1;
    fclose(f);
    return frequency;
}
unsigned reports = 0;
int64_t nowNs() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
void initialize() {
    initialized = true;
    const char* logging = getenv("SMS_TIMING_LOG");
    logTiming = logging && *logging && logging[0] != '0';
    const char* reserve = getenv("SMS_ANDROID_PERFORMANCE_HEADROOM");
    // The v9 build regressed on the tester's device. Keep the
    // original full-frame target unless the player explicitly opts in.
    headroom = reserve && *reserve && reserve[0] != '0';
    const char* enabled = getenv("SMS_ANDROID_PERFORMANCE_HINTS");
    if (enabled && enabled[0] == '0') {
        unavailable = true;
        fprintf(stderr, "[android] performance hints disabled\n");
        return;
    }
    // Resolve native API 33 functions at runtime so older Android still loads.
    // Keep libandroid open for the lifetime of the function pointers.
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    port_android::HintApi api{};
    if (library) {
#define HINT_ENTRY(field, name) api.field = reinterpret_cast<decltype(api.field)>(dlsym(library, name))
        HINT_ENTRY(manager, "APerformanceHint_getManager");
        HINT_ENTRY(create, "APerformanceHint_createSession");
        HINT_ENTRY(target, "APerformanceHint_updateTargetWorkDuration");
        HINT_ENTRY(report, "APerformanceHint_reportActualWorkDuration");
        HINT_ENTRY(close, "APerformanceHint_closeSession");
        HINT_ENTRY(powerEfficiency, "APerformanceHint_setPreferPowerEfficiency");
#undef HINT_ENTRY
    }
    if (!api.manager || !api.create || !api.target || !api.report || !api.close) {
        unavailable = true;
        fprintf(stderr, "[android] performance hint API unavailable\n");
        return;
    }
    hints = new port_android::HintSession(api);
}
}

extern "C" void port_android_performance_render_thread() {
    configurePerformanceCores();
    renderTid.store(gettid(), std::memory_order_release);
}
extern "C" void port_android_performance_frame_end() {
    if (!frameStart || frameTid != gettid()) return;
    const int64_t work = nowNs() - frameStart;
    const int64_t cpu = logTiming ? cpuNs() - cpuStart : 0;
    frameStart = 0;
    if (hints && !unavailable) {
        if (!hints->report(work)) {
            unavailable = true;
            fprintf(stderr, "[android] performance hint reporting stopped\n");
        } else if (++reports == 1) {
            fprintf(stderr, "[android] performance hints: first frame report %.2f ms\n", double(work) / 1000000);
        }
    }
    if (logTiming) {
        if (sampleTarget != frameTarget) {
            samples = 0; sumWork = sumCpu = maxWork = 0; sampleTarget = frameTarget;
        }
        sumWork += work; sumCpu += cpu > 0 ? cpu : 0;
        if (work > maxWork) maxWork = work;
        if (++samples == 120) {
            const int core = sched_getcpu();
            const long khz = cpuKhz(core);
            fprintf(stderr, "[android] workload: %u frames, budget %.2f ms, work avg %.2f/max %.2f ms, "
                            "main-thread CPU %.2f ms/frame, CPU core %d, clock %ld kHz (-1 unavailable), hints %s\n",
                    samples, double(frameTarget) / 1000000, double(sumWork) / samples / 1000000,
                    double(maxWork) / 1000000, double(sumCpu) / samples / 1000000, core, khz,
                    hints && !unavailable ? "active" : "off/unavailable");
            samples = 0; sumWork = sumCpu = maxWork = 0;
        }
    }
}
extern "C" void port_android_performance_frame_begin(int64_t targetNs) {
    if (targetNs <= 0) { frameStart = 0; return; }
    configurePerformanceCores();
    if (!initialized) initialize();
    if ((unavailable || !hints) && !logTiming) return;
    const int32_t game = gettid();
    const int32_t worker = renderTid.load(std::memory_order_acquire);
    // Android normally keeps GL on the game's thread. A separate GL worker
    // can opt in through the existing renderer setting; deduplicate IDs.
    const int32_t render = worker > 0 ? worker : game;
    const int64_t hintTarget = port_android::workTarget(targetNs, headroom);
    if (hints && !unavailable && !hints->configure(game, render, hintTarget)) {
        unavailable = true;
        fprintf(stderr, "[android] performance hint sessions unsupported or rejected\n");
    }
    if (hints && !unavailable && (game != sessionGameTid || render != sessionRenderTid || targetNs != sessionTarget)) {
        fprintf(stderr, "[android] performance hints active: game TID %d, render TID %d, target %.2f ms "
                        "(frame %.2f ms, headroom %s), power-efficiency-off status %d (-1 unavailable)\n",
                game, render, double(hintTarget) / 1000000, double(targetNs) / 1000000,
                headroom ? "20%" : "off", hints->powerEfficiencyStatus());
        sessionGameTid = game; sessionRenderTid = render; sessionTarget = targetNs;
    }
    frameTid = game;
    frameTarget = targetNs;
    frameStart = nowNs();
    cpuStart = logTiming ? cpuNs() : 0;
}
extern "C" void port_android_performance_pause() {
    frameStart = 0;
    if (hints) hints->close();
    sessionGameTid = sessionRenderTid = 0;
    sessionTarget = 0;
    samples = 0; sumWork = sumCpu = maxWork = 0;
}
extern "C" void port_android_performance_shutdown() {
    port_android_performance_pause();
    delete hints; hints = nullptr;
}
#endif
