// Internal runtime interfaces (C++).
#pragma once
#include "recomp.h"
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <functional>

// ---- logging ----
enum LogCat { LOG_CPU, LOG_OS, LOG_HW, LOG_DVD, LOG_GX, LOG_VI, LOG_SI, LOG_EXI, LOG_DSP, LOG_AI, LOG_THREAD, LOG_COUNT };
extern bool g_log_enabled[LOG_COUNT];
bool log_enable_list(const char* list);  // "exi,dsp" or "all"
void log_msg(LogCat cat, const char* fmt, ...) GCN_PRINTF_FMT(2, 3);
[[noreturn]] void fatal(const char* fmt, ...) GCN_PRINTF_FMT(1, 2);
#define LOG(cat, ...) do { if (g_log_enabled[cat]) log_msg(cat, __VA_ARGS__); } while (0)

// ---- memory ----
constexpr uint32_t RAM_SIZE = 0x01800000;
constexpr uint32_t ARAM_SIZE = 0x01000000;
constexpr uint32_t LC_BASE = 0x20000000;   // locked cache (guest 0xE0000000)
// The locked cache is 16KB, but what backs the window is wider. Code that keeps a buffer
// there can read its neighbours a row past the end -- on hardware that lands in ordinary
// cache lines, and Dolphin maps 256KB here -- so commit as much, not a 16KB edge to fault on.
constexpr uint32_t LC_SIZE = 0x40000;
void mem_init();
// The lines the VI scans out, from its timing registers (hw/vi.cpp); 0 before the game sets them.
uint32_t vi_display_height();
// Whether a recompiled function starts at `addr`.
bool recomp_is_function(uint32_t addr);
extern uint8_t* g_aram;

// Where the memory card lives. Null means the working directory, which is right for a
// desktop run started from the repository. An app has no useful working directory -- on
// Android it is "/", which is not writable -- so a frontend that knows one sets this
// before the guest boots.
extern const char* g_save_dir;
// Overrides the memory card's file when set (see exi.cpp).
extern const char* g_memcard_path;
inline uint8_t* mem_ptr(uint32_t a) { return HOST_W(a); }
inline uint32_t mem_r32(uint32_t a) { uint32_t v; memcpy(&v, HOST(a), 4); return GCN_BSWAP32(v); }
inline uint16_t mem_r16(uint32_t a) { uint16_t v; memcpy(&v, HOST(a), 2); return GCN_BSWAP16(v); }
inline uint8_t mem_r8(uint32_t a) { return *HOST(a); }
inline void mem_w32(uint32_t a, uint32_t v) { v = GCN_BSWAP32(v); memcpy(HOST_W(a), &v, 4); }
inline void mem_w16(uint32_t a, uint16_t v) { v = GCN_BSWAP16(v); memcpy(HOST_W(a), &v, 2); }
inline void mem_w8(uint32_t a, uint8_t v) { *HOST_W(a) = v; }
// Physical address (as used by DMA engines) -> host pointer into main RAM.
inline uint8_t* phys_ptr(uint32_t pa) { return g_mem + (pa & 0x01FFFFFF); }

// How much of a DMA at physical address `pa` fits inside main memory.
//
// The engines take their address and their length from registers the guest writes, and
// nothing downstream checked either: phys_ptr() masks to 32 MB against 24 MB of RAM, so
// a transfer aimed past the end writes over whatever is there rather than faulting. What
// that looks like is a crash minutes later somewhere unrelated -- OSAllocFromHeap
// walking a free list whose `next` field now holds a word of someone else's data.
//
// Returns how many bytes may be transferred, and says so the first few times that is
// fewer than asked for, naming the engine. Clamping cannot rescue a transfer that was
// already aimed at the wrong place; what it does is keep the mistake inside the
// transfer, and make it visible where it happens instead of where it is noticed.
uint32_t dma_fit(const char* engine, uint32_t pa, uint32_t len);

// ---- timing ----
constexpr uint64_t TB_FREQ = 40500000;  // timebase / decrementer ticks per second
uint64_t now_ticks();                   // guest time, TB units (virtual clock by default)
void timing_init();
// The clock. By default guest time is virtual: it advances with the guest's own execution
// (loop back-edges, TICKS_PER_EDGE each) and the host sleeps to hold it to real time, so a
// replay sees exactly the recording's timeline. GCN_CLOCK=host restores wall-clock time.
bool clock_is_virtual();
void clock_set_scale(double virtual_seconds_per_real_second);  // 0 = unpaced, run flat out
double clock_scale();
// How fast the emulated CPU is, as a multiple of the console's (GCN_CPU_SCALE): on the
// virtual clock a frame's work is charged 1/N of the time. 1 by default; 2 at most.
void clock_set_cpu_scale(double n);
// The step scale (step.cpp): 1 normally; a game running at twice its native frame rate
// sets 0.5 and calls step_frame() once a game frame.
void step_set_scale(double s);
double step_scale();
void step_frame();
// While a finite value is set it replaces the step scale at every site (step.cpp, a body
// the game integrates in several passes a frame); NAN ends the override.
void step_override(double s);
double clock_cpu_scale();
void clock_update_limit();   // recompute g_vlimit from the next event / decrementer deadline
void clock_pace();           // sleep if virtual time is ahead of real time (virtual clock only)
void irq_request();          // make the running guest thread poll at its next back-edge
uint64_t host_ns();          // wall clock since start, for diagnostics
void rtc_set_base(time_t utc);  // what the RTC reads at guest time zero (hw/exi.cpp)

// Guest consistency checks (builds with GCN_GUEST_CHECKS). A game project may install a
// function that inspects the guest's own data structures -- its heap, say -- and returns
// nullptr when they are intact or a description of the first thing wrong. The runtime
// calls it at every interrupt poll, after each of its own writes into guest memory (DVD,
// ARAM and locked-cache DMA) and at every presented frame, and on the first failure
// prints the description with the thread dump and stops, so that a corruption is caught
// within a few hundred microseconds of the write that caused it rather than at the crash
// it leads to. With an exact replay that is enough to find the writer: note the word the
// check names, replay with GCN_WATCH_ADDR set to it. The checks are a function of guest
// state only, so they do not perturb a replay. Without the build option the hook is never
// called.
//
// The same points watch one word without the build option: GCN_GUARD=<hex address>
// reports every change to it with where it was noticed, and after a DMA the transfer
// itself (debug_guard_dma), so a word cleared by the runtime's own write is named at once
// and one cleared by the guest is narrowed to the stretch since the last poll.
// The game resetting the console: OSResetSystem (hle.txt), with the SDK's kind -- 0 a
// restart, which reboots the game from the disc, 1 a hot reset, 2 a shutdown -- and the
// reset code the game passes on to itself. A game returns to its title this way (after a
// death, say), and the runtime emulates no reboot, so it ends the run instead: the input
// log is closed, and the frontend's handler starts the game again however it can (a new
// process is a clean console). Without a handler, or for a shutdown, the process exits.
using ResetHandler = void (*)(uint32_t kind, uint32_t code);
void set_reset_handler(ResetHandler fn);
[[noreturn]] void guest_reset(uint32_t kind, uint32_t code);

using GuestCheck = const char* (*)();
void debug_set_guest_check(GuestCheck fn);
void debug_guest_check(const char* where);
void debug_guard_dma(const char* what, uint32_t addr, uint32_t len);

// GCN_PROFILE=<file>: sample the host's program counters while the game runs, and write the
// histogram out at the end (host_profile.cpp). Linux and Android only.
void host_profile_start();
void host_profile_dump();

// Scheduled hardware events, run on the guest thread holding the baton.
using EventFn = std::function<void()>;
void event_schedule(uint64_t at_ticks, EventFn fn);
void event_schedule_in(uint64_t delta_ticks, EventFn fn);
void events_run_due();

// ---- interrupts (PI) ----
enum : uint32_t {
    INT_PI = 0x1, INT_RSW = 0x2, INT_DI = 0x4, INT_SI = 0x8, INT_EXI = 0x10, INT_AI = 0x20,
    INT_DSP = 0x40, INT_MEM = 0x80, INT_VI = 0x100, INT_PE_TOKEN = 0x200, INT_PE_FINISH = 0x400,
    INT_CP = 0x800, INT_DEBUG = 0x1000, INT_HSP = 0x2000,
};
void pi_set_interrupt(uint32_t cause, bool set);
void pi_update();
uint32_t pi_read32(uint32_t off);
void pi_write32(uint32_t off, uint32_t v);

// ---- threads ----
struct HostThread;
HostThread* host_current();
CPU* cpu_current();
void threads_start_boot(uint32_t entry);
void threads_request_quit();

// ---- HLE ----
void hle_os_report(CPU* c);
std::string guest_format(CPU* c, uint32_t fmt_addr, int first_gpr, int first_fpr);
std::string guest_str(uint32_t addr, size_t max = 4096);

// Appends the SDK heaps' totals, block counts and largest free block, walked out of guest
// memory (runtime/heap_trace.cpp); with GCN_HEAP set, also the live cells by caller.
void heap_report(std::string& out);
void heap_trace_frame(uint32_t frame);  // GCN_HEAP=<frames>: the report every so many frames

// Names a guest address. func_name wants a function's first instruction; func_containing
// takes any address inside one, which is what a return address off the stack is.
const char* func_name(uint32_t addr);
const char* func_containing(uint32_t addr, uint32_t* start);

// ---- hw modules ----
void vi_init(); uint16_t vi_read16(uint32_t off); void vi_write16(uint32_t off, uint16_t v);
void di_init(const char* iso_path); uint32_t di_read32(uint32_t off); void di_write32(uint32_t off, uint32_t v);
void si_init(); uint32_t si_read32(uint32_t off); void si_write32(uint32_t off, uint32_t v);
void exi_init(); uint32_t exi_read32(uint32_t off); void exi_write32(uint32_t off, uint32_t v);
void ai_init(); uint32_t ai_read32(uint32_t off); void ai_write32(uint32_t off, uint32_t v);
void dsp_init(); uint16_t dsp_read16(uint32_t off); void dsp_write16(uint32_t off, uint16_t v);
void cp_init(); uint16_t cp_read16(uint32_t off); void cp_write16(uint32_t off, uint16_t v);
uint16_t pe_read16(uint32_t off); void pe_write16(uint32_t off, uint16_t v);
void gp_write8(uint8_t v); void gp_write16(uint16_t v); void gp_write32(uint32_t v);
uint32_t efb_peek(uint32_t addr); void efb_poke(uint32_t addr, uint32_t v);

// iso reading
bool iso_read(uint64_t offset, void* dst, uint32_t len);
