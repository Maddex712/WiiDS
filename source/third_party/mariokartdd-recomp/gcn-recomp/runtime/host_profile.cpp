// A sampling profiler of the host code, for devices where perf will not run: a Quest's
// SELinux policy refuses perf_event_open to the shell, so simpleperf cannot record there.
//
// GCN_PROFILE=<file> arms a profiling timer (ITIMER_PROF, 1 kHz of CPU time across the
// process). Its signal lands on whichever thread was running, and the handler records
// that thread's program counter. host_profile_dump() writes one line per distinct
// location, most frequent first:
//
//   <count> <thread id> <module>+0x<offset>
//
// The offsets are into the module as loaded, which is what llvm-addr2line takes:
//   llvm-addr2line -f -C -e <the unstripped binary> 0x<offset> ...
// tools/profile_report.py does that and sums by function.
//
// Linux and Android only; elsewhere both calls do nothing.
#include "runtime.h"
#include <cstdio>
#include <cstdlib>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <map>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
constexpr size_t kMaxSamples = 1u << 22;  // over an hour at 1 kHz
struct Sample { uintptr_t pc; uint32_t tid; };
Sample* g_samples;
std::atomic<size_t> g_count{0};

void on_prof(int, siginfo_t*, void* ctx) {
    const size_t i = g_count.fetch_add(1, std::memory_order_relaxed);
    if (i >= kMaxSamples) return;
    const ucontext_t* uc = (const ucontext_t*)ctx;
#if defined(__aarch64__)
    g_samples[i].pc = (uintptr_t)uc->uc_mcontext.pc;
#else
    g_samples[i].pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#endif
    g_samples[i].tid = (uint32_t)syscall(SYS_gettid);
}
}  // namespace

void host_profile_start() {
    if (!getenv("GCN_PROFILE") || g_samples) return;
    g_samples = (Sample*)calloc(kMaxSamples, sizeof(Sample));
    struct sigaction sa{};
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, nullptr);
    // GCN_PROFILE_DELAY=<seconds> starts sampling that long after boot, so a profile can
    // be of one stretch of a replayed route rather than everything before it too.
    // ITIMER_PROF counts CPU time, so the delay is taken as wall time by a thread instead.
    const int delay = getenv("GCN_PROFILE_DELAY") ? atoi(getenv("GCN_PROFILE_DELAY")) : 0;
    auto arm = [] {
        itimerval tv{};
        tv.it_interval.tv_usec = 1000;
        tv.it_value.tv_usec = 1000;
        setitimer(ITIMER_PROF, &tv, nullptr);
    };
    if (delay > 0) {
        std::thread([delay, arm] {
            std::this_thread::sleep_for(std::chrono::seconds(delay));
            arm();
        }).detach();
    } else {
        arm();
    }
    fprintf(stderr, "profiling to %s%s\n", getenv("GCN_PROFILE"), delay > 0 ? " after a delay" : "");
}

void host_profile_dump() {
    const char* path = getenv("GCN_PROFILE");
    if (!path || !g_samples) return;
    itimerval off{};
    setitimer(ITIMER_PROF, &off, nullptr);
    const size_t n = std::min(g_count.load(), kMaxSamples);
    std::map<std::pair<uintptr_t, uint32_t>, uint32_t> hist;
    for (size_t i = 0; i < n; i++) hist[{g_samples[i].pc, g_samples[i].tid}]++;
    std::vector<std::pair<uint32_t, std::pair<uintptr_t, uint32_t>>> rows;
    for (const auto& [k, v] : hist) rows.push_back({v, k});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# %zu samples\n", n);
    for (const auto& [count, k] : rows) {
        Dl_info info{};
        if (dladdr((void*)k.first, &info) && info.dli_fname) {
            const char* name = info.dli_fname;
            for (const char* s = name; *s; s++) if (*s == '/') name = s + 1;
            fprintf(f, "%u %u %s+0x%lx\n", count, k.second, name,
                    (unsigned long)(k.first - (uintptr_t)info.dli_fbase));
        } else {
            fprintf(f, "%u %u ?+0x%lx\n", count, k.second, (unsigned long)k.first);
        }
    }
    fclose(f);
    fprintf(stderr, "profile: %zu samples written to %s\n", n, path);
}

#else
void host_profile_start() {}
void host_profile_dump() {}
#endif
