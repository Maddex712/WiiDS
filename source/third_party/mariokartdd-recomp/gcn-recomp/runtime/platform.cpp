// Host OS services: POSIX and Windows implementations of platform.h.
#include "platform.h"
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// ===========================================================================
#ifdef _WIN32
// ===========================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <io.h>
#include <process.h>

void* plat_reserve(size_t size, void*) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool plat_commit(void* base, size_t size) {
    return VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

// ---- diagnostics ----
static bool g_sym_ready;

void plat_backtrace_print() {
    HANDLE proc = GetCurrentProcess();
    if (!g_sym_ready) {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
        SymInitialize(proc, nullptr, TRUE);
        g_sym_ready = true;
    }
    void* frames[64];
    USHORT n = CaptureStackBackTrace(1, 64, frames, nullptr);
    // SymFromAddr wants a SYMBOL_INFO with room for the name after it.
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    for (USHORT i = 0; i < n; i++) {
        DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
        memset(sym, 0, sizeof(SYMBOL_INFO));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;
        DWORD64 disp = 0;
        const char* name = SymFromAddr(proc, addr, &disp, sym) ? sym->Name : "?";

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line))
            fprintf(stderr, "  %2u %p %s (%s:%lu)\n", i, frames[i], name, line.FileName, line.LineNumber);
        else
            fprintf(stderr, "  %2u %p %s\n", i, frames[i], name);
    }
    fflush(stderr);
}

void plat_watchdog(int seconds, int exit_code) {
    std::thread([seconds, exit_code] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        _exit(exit_code);
    }).detach();
}

void plat_exit_now(int code) { _exit(code); }
void plat_relaunch(char** argv) { fflush(nullptr); _execv(argv[0], argv); }

static void (*g_on_interrupt)();
static void (*g_on_fault)(const void*, int);

static BOOL WINAPI console_ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        if (g_on_interrupt) g_on_interrupt();
        return TRUE;
    }
    return FALSE;
}

static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = nullptr;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
        ep->ExceptionRecord->NumberParameters >= 2)
        addr = (const void*)ep->ExceptionRecord->ExceptionInformation[1];
    if (g_on_fault) g_on_fault(addr, (int)code);
    return EXCEPTION_EXECUTE_HANDLER;
}

void plat_install_crash_handlers(void (*on_interrupt)(), void (*on_fault)(const void*, int)) {
    g_on_interrupt = on_interrupt;
    g_on_fault = on_fault;
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    SetUnhandledExceptionFilter(unhandled_filter);
    // Don't pop the "program stopped working" dialog in a non-interactive run.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

// ---- threads ----
struct ThreadStart { void* (*fn)(void*); void* arg; };

static DWORD WINAPI thread_trampoline(void* p) {
    ThreadStart ts = *(ThreadStart*)p;
    delete (ThreadStart*)p;
    ts.fn(ts.arg);
    return 0;
}

bool plat_thread_start(void* (*fn)(void*), void* arg, size_t stack_size) {
    auto* ts = new ThreadStart{fn, arg};
    // STACK_SIZE_PARAM_IS_A_RESERVATION keeps the large stack reserved rather than
    // committed, so each guest thread costs address space and not RAM.
    HANDLE h = CreateThread(nullptr, stack_size, thread_trampoline, ts,
                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!h) { delete ts; return false; }
    CloseHandle(h);
    return true;
}

void plat_thread_exit() {
    ExitThread(0);
    for (;;) {}  // not reached; keeps [[noreturn]] honest under MSVC
}

// ---- time ----
long plat_utc_offset_seconds() {
    time_t t = time(nullptr);
    struct tm lt;
    if (localtime_s(&lt, &t) != 0) return 0;
    // Re-read the local wall clock as if it were UTC: the difference is the offset,
    // with whatever DST was in effect already folded in by localtime_s.
    time_t as_utc = _mkgmtime(&lt);
    if (as_utc == (time_t)-1) return 0;
    return (long)(as_utc - t);
}

// ---- files ----
bool plat_readable(const char* path) { return path && *path && _access(path, 4) == 0; }

bool plat_replace_file(const char* from, const char* to) {
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
}

// Positional reads on a handle opened for overlapped I/O: each read carries its own
// offset, so concurrent DVD reads from guest threads and the audio callback are safe.
struct PlatFile { HANDLE h; };

PlatFile* plat_open_read(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    return new PlatFile{h};
}

bool plat_read_at(PlatFile* f, uint64_t offset, void* dst, uint32_t len) {
    if (!f) return false;
    // One event per thread rather than per read: the game streams assets off the disc
    // continuously, so this is a hot path. Each thread waits only on its own I/O.
    static thread_local HANDLE t_event = nullptr;
    if (!t_event) {
        t_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!t_event) return false;
    }
    uint8_t* out = (uint8_t*)dst;
    uint32_t done = 0;
    while (done < len) {
        OVERLAPPED ov{};
        ov.hEvent = t_event;
        uint64_t off = offset + done;
        ov.Offset = (DWORD)(off & 0xFFFFFFFFu);
        ov.OffsetHigh = (DWORD)(off >> 32);
        DWORD got = 0;
        BOOL ok = ReadFile(f->h, out + done, len - done, &got, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
            ok = GetOverlappedResult(f->h, &ov, &got, TRUE);
        if (!ok || got == 0) return false;
        done += got;
    }
    return true;
}

// ===========================================================================
#else  // POSIX
// ===========================================================================
#include <csignal>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#ifndef __APPLE__
#include <sys/syscall.h>
#endif
#ifdef __ANDROID__
// bionic has no execinfo.h; unwind by hand and symbolize with dladdr.
#include <android/set_abort_message.h>
#include <dlfcn.h>
#include <unwind.h>
#else
#include <execinfo.h>
#endif

void* plat_reserve(size_t size, void* at) {
    int flags = MAP_PRIVATE | MAP_ANON | MAP_NORESERVE;
#ifdef MAP_FIXED_NOREPLACE
    if (at) flags |= MAP_FIXED_NOREPLACE;
#endif
    void* p = mmap(at, size, PROT_NONE, flags, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (at && p != at) {   // a kernel without MAP_FIXED_NOREPLACE takes the address as a hint
        munmap(p, size);
        return nullptr;
    }
    return p;
}

bool plat_commit(void* base, size_t size) {
    return mprotect(base, size, PROT_READ | PROT_WRITE) == 0;
}

#ifdef __ANDROID__
namespace {
struct BacktraceState { void** frames; int count; int max; };

_Unwind_Reason_Code bt_frame(struct _Unwind_Context* ctx, void* arg) {
    auto* st = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc) {
        if (st->count >= st->max) return _URC_END_OF_STACK;
        st->frames[st->count++] = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}
}  // namespace

void plat_backtrace_print() {
    void* frames[64];
    BacktraceState st{frames, 0, 64};
    _Unwind_Backtrace(bt_frame, &st);
    for (int i = 0; i < st.count; i++) {
        Dl_info info{};
        if (dladdr(frames[i], &info) && info.dli_sname)
            fprintf(stderr, "  %2d %p %s\n", i, frames[i], info.dli_sname);
        else
            fprintf(stderr, "  %2d %p\n", i, frames[i]);
    }
    fflush(stderr);
}
#else
void plat_backtrace_print() {
    void* bt[64];
    int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 2);
}
#endif

void plat_watchdog(int seconds, int exit_code) {
    static int s_code;
    s_code = exit_code;
    signal(SIGALRM, [](int) { _exit(s_code); });
    alarm((unsigned)seconds);
}

void plat_exit_now(int code) { _exit(code); }
void plat_relaunch(char** argv) { fflush(nullptr); execv("/proc/self/exe", argv); execvp(argv[0], argv); }

static void (*g_on_interrupt)();
static void (*g_on_fault)(const void*, int);

static void sig_interrupt(int) { if (g_on_interrupt) g_on_interrupt(); }
static void sig_fault(int sig, siginfo_t* si, void*) {
    if (g_on_fault) g_on_fault(si ? si->si_addr : nullptr, sig);
}

void plat_install_crash_handlers(void (*on_interrupt)(), void (*on_fault)(const void*, int)) {
    g_on_interrupt = on_interrupt;
    g_on_fault = on_fault;
    signal(SIGINT, sig_interrupt);
    signal(SIGTERM, sig_interrupt);
    struct sigaction sa = {};
    sa.sa_sigaction = sig_fault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
}

bool plat_thread_start(void* (*fn)(void*), void* arg, size_t stack_size) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);
    pthread_t th;
    int rc = pthread_create(&th, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    if (rc != 0) fprintf(stderr, "pthread_create: %s (a %zu MB stack)\n", strerror(rc), stack_size >> 20);
    return rc == 0;
}

void plat_thread_exit() {
    pthread_exit(nullptr);
    for (;;) {}  // not reached
}

long plat_utc_offset_seconds() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return (long)lt.tm_gmtoff;
}

bool plat_readable(const char* path) { return path && *path && access(path, R_OK) == 0; }

bool plat_replace_file(const char* from, const char* to) { return rename(from, to) == 0; }

struct PlatFile { int fd; };

PlatFile* plat_open_read(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return nullptr;
    return new PlatFile{fd};
}

bool plat_read_at(PlatFile* f, uint64_t offset, void* dst, uint32_t len) {
    if (!f) return false;
    uint8_t* out = (uint8_t*)dst;
    uint32_t done = 0;
    while (done < len) {
        ssize_t r = pread(f->fd, out + done, len - done, (off_t)(offset + done));
        if (r <= 0) {
            if (r < 0) fprintf(stderr, "read of %u bytes at %llu failed: %s\n", len, (unsigned long long)offset, strerror(errno));
            return false;
        }
        done += (uint32_t)r;
    }
    return true;
}

#endif

// ===========================================================================
// Shared
// ===========================================================================
static long current_tid() {
#if defined(_WIN32)
    return (long)GetCurrentThreadId();
#elif defined(__APPLE__)
    uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return (long)id;
#else
    return (long)syscall(SYS_gettid);
#endif
}

static void set_thread_name(const char* name) {
#if defined(_WIN32)
    wchar_t w[64];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 64)) SetThreadDescription(GetCurrentThread(), w);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    char n[16];  // Linux keeps 15 characters
    snprintf(n, sizeof(n), "%s", name);
    pthread_setname_np(pthread_self(), n);
#endif
}

static std::mutex g_roles_mutex;
static std::vector<std::pair<ThreadRole, long>> g_roles;
static std::vector<PlatThread> g_named;
static void (*g_role_listener)(ThreadRole, long);

void plat_thread_role(ThreadRole role, const char* name) {
    set_thread_name(name);
    const long tid = current_tid();
#if defined(__linux__)
    // The render workers (the vertex transform) yield to the rest when cores are short. A
    // headset gives an app three cores, and in its heaviest scenes the five busy threads
    // wanted more than that: the guest, which sets the pace, spent 90-190 ms of each
    // second runnable and waiting while the transform held its core. The transform has
    // the slack -- a frame's is done while the guest is already on the next -- so it takes
    // the lower priority. GCN_WORKER_NICE sets how much lower (5, a third of the share
    // when contended; 0 leaves it).
    if (role == ThreadRole::RenderWorker) {
        const char* e = getenv("GCN_WORKER_NICE");
        const int nice = e ? atoi(e) : 5;
        if (nice) setpriority(PRIO_PROCESS, (id_t)tid, nice);
    }
#endif
    void (*fn)(ThreadRole, long);
    {
        std::lock_guard<std::mutex> lk(g_roles_mutex);
        g_roles.emplace_back(role, tid);
        g_named.push_back({name, tid});
        fn = g_role_listener;
    }
    if (fn) fn(role, tid);
}

std::vector<PlatThread> plat_threads() {
    std::lock_guard<std::mutex> lk(g_roles_mutex);
    return g_named;
}

bool plat_thread_sched(long tid, uint64_t* run_ns, uint64_t* wait_ns, int* cpu) {
#if defined(__linux__)
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%ld/schedstat", tid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    unsigned long long run = 0, wait = 0;
    const bool ok = fscanf(f, "%llu %llu", &run, &wait) == 2;
    fclose(f);
    if (!ok) return false;
    *run_ns = run;
    *wait_ns = wait;
    // The core is the 39th field of stat, counted after the parenthesised name, which
    // may itself hold spaces.
    *cpu = -1;
    snprintf(path, sizeof(path), "/proc/self/task/%ld/stat", tid);
    if ((f = fopen(path, "r"))) {
        char buf[1024];
        const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = 0;
        if (const char* q = strrchr(buf, ')')) {
            int field = 2;
            for (q++; *q && field < 39; q++)
                if (*q == ' ') field++;
            if (field == 39) *cpu = atoi(q);
        }
    }
    return true;
#else
    (void)tid; (void)run_ns; (void)wait_ns; (void)cpu;
    return false;
#endif
}

void plat_set_thread_role_listener(void (*fn)(ThreadRole, long)) {
    std::vector<std::pair<ThreadRole, long>> known;
    {
        std::lock_guard<std::mutex> lk(g_roles_mutex);
        g_role_listener = fn;
        known = g_roles;
    }
    if (fn)
        for (const auto& r : known) fn(r.first, r.second);
}

void plat_make_dirs(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

std::string plat_find_file(const char* dir, const char* suffix) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    if (ec) return {};
    const size_t slen = strlen(suffix);
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string name = it->path().filename().string();
        if (name.size() <= slen) continue;
        std::string tail = name.substr(name.size() - slen);
        bool match = true;
        for (size_t i = 0; i < slen; i++)
            match &= (char)tolower((unsigned char)tail[i]) == (char)tolower((unsigned char)suffix[i]);
        if (match) return it->path().string();
    }
    return {};
}

// Hand a fatal message to the platform's own crash record.
//
// stderr is not enough on Android. The app's stdout and stderr are piped into logcat,
// and logcat is a 256 KiB ring: with `log_frames 1` in vr.txt the theater path alone
// writes a line per display frame and laps the buffer in well under a minute, so by the
// time a crash is noticed the message explaining it has been overwritten. That is not
// hypothetical -- it is how the crash of 2026-10-06 lost its diagnostic entirely.
//
// The abort message goes somewhere else: the tombstone, and the `crash` log buffer,
// neither of which the app's own logging can flush. It survives.
void plat_record_fatal(const char* msg) {
#ifdef __ANDROID__
    android_set_abort_message(msg);
#else
    (void)msg;  // desktop stderr is a terminal or a file, not a ring that laps
#endif
}
