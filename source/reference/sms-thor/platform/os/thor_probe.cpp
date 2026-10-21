// Opt-in, bounded observational diagnostics. Never schedules guest work, sends
// host signals, changes gameplay, or terminates a running session.
#ifdef __ANDROID__
#include "crash.h"
#include "crash_line.h"
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" unsigned int VIGetRetraceCount();
extern "C" unsigned int GXPC_FrameCount();

namespace {
int read_proc(const char* path, char* bytes, size_t capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t count = read(fd, bytes, capacity - 1);
    int error = errno;
    close(fd);
    if (count < 0) return -error;
    while (count > 0 && (bytes[count - 1] == '\n' || bytes[count - 1] == '\r')) --count;
    bytes[count] = 0;
    return static_cast<int>(count);
}
void census() {
    DIR* tasks = opendir("/proc/self/task");
    if (!tasks) return;
    unsigned count = 0;
    for (dirent* item; count < 64 && (item = readdir(tasks));) {
        char* end = nullptr;
        long tid = strtol(item->d_name, &end, 10);
        if (!tid || !end || *end) continue;
        ++count;
        char path[128], name[64] = {}, wait[128] = {}, call[512] = {};
        snprintf(path, sizeof path, "/proc/self/task/%ld/comm", tid);
        read_proc(path, name, sizeof name);
        snprintf(path, sizeof path, "/proc/self/task/%ld/wchan", tid);
        int waitResult = read_proc(path, wait, sizeof wait);
        snprintf(path, sizeof path, "/proc/self/task/%ld/syscall", tid);
        int callResult = read_proc(path, call, sizeof call);
        PortCrashLine line;
        line.text("[thor-probe] task "); line.number(tid, 10);
        line.text(" name="); line.text(name);
        line.text(" wait="); line.text(waitResult < 0 ? "unavailable" : wait);
        line.text(" syscall="); line.text(callResult < 0 ? "unavailable" : call);
        if (callResult < 0) { line.text(" errno="); line.number(-callResult, 10); }
        line.text("\n"); port_crash_emit(line);
        // Linux exposes the blocked thread's SP/PC as the final two syscall
        // fields. If Android denies that proc file, report the denial and stop
        // there. Only native low game stacks are sampled, never ART stacks.
        long syscallNumber;
        unsigned long long args[6], sp, pc;
        if (callResult > 0 && sscanf(call, "%ld %llx %llx %llx %llx %llx %llx %llx %llx",
                &syscallNumber, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &sp, &pc) == 9
                && sp >= 0x10000 && sp < 0x80000000ULL && pc) {
            line.text("[thor-probe] low-stack task "); line.number(tid, 10);
            line.reg("pc", pc); line.reg("sp", sp); line.text("\n"); port_crash_emit(line);
            port_crash_dump_memory(static_cast<uintptr_t>(pc), static_cast<uintptr_t>(sp));
        }
    }
    closedir(tasks);
}
void* probe(void*) {
    pthread_setname_np(pthread_self(), "ThorContext");
    for (unsigned sample = 0; sample < 3; ++sample) {
        sleep(sample == 0 ? 12 : 4);
        PortCrashLine line;
        line.text("\n[thor-probe] observational sample "); line.number(sample + 1, 10);
        line.text(" retraces="); line.number(VIGetRetraceCount(), 10);
        line.text(" frames="); line.number(GXPC_FrameCount(), 10);
        line.text("\n"); port_crash_emit(line);
        port_crash_describe_context();
        census();
        line.text("[thor-probe] sample complete\n"); port_crash_emit(line);
    }
    return nullptr;
}
}
extern "C" void port_thor_probe_init() {
    const char* enabled = getenv("SMS_THOR_CONTEXT_PROBE");
    if (!enabled || strcmp(enabled, "1")) return;
    pthread_t thread;
    int error = pthread_create(&thread, nullptr, probe, nullptr);
    if (!error) pthread_detach(thread);
    PortCrashLine line;
    line.text("[thor-probe] bounded context probe "); line.text(error ? "could not start" : "enabled: 12/16/20 seconds");
    line.text("\n"); port_crash_emit(line);
}
#endif
