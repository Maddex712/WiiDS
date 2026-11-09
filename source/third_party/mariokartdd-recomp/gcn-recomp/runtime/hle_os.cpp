// HLE replacements for a few SDK OS functions (debug output, and the reset).
#include "runtime.h"
#include "input_log.h"
#include "platform.h"

std::string guest_str(uint32_t addr, size_t max) {
    std::string s;
    if (!addr) return "(null)";
    for (size_t i = 0; i < max; i++) {
        char ch = (char)mem_r8(addr + (uint32_t)i);
        if (!ch) break;
        s.push_back(ch);
    }
    return s;
}

// printf-style formatting with PowerPC EABI varargs: integer args in r[first_gpr..10],
// floating args in f[first_fpr..8], overflow on the stack.
std::string guest_format(CPU* c, uint32_t fmt_addr, int first_gpr, int first_fpr) {
    std::string fmt = guest_str(fmt_addr), out;
    int gi = first_gpr, fi = first_fpr;
    uint32_t stack = c->r[1] + 8;
    auto next_int = [&]() -> uint32_t {
        if (gi <= 10) return c->r[gi++];
        uint32_t v = mem_r32(stack); stack += 4; return v;
    };
    auto next_i64 = [&]() -> uint64_t {
        if (!(gi & 1)) gi++;  // 64-bit args occupy an aligned pair starting at an odd register (r3,r5,r7,r9)
        if (gi <= 9) { uint64_t v = ((uint64_t)c->r[gi] << 32) | c->r[gi + 1]; gi += 2; return v; }
        stack = (stack + 7) & ~7u;
        uint64_t v = ((uint64_t)mem_r32(stack) << 32) | mem_r32(stack + 4); stack += 8; return v;
    };
    auto next_dbl = [&]() -> double {
        if (fi <= 8) return c->f[fi++].d;
        stack = (stack + 7) & ~7u;
        uint64_t u = ((uint64_t)mem_r32(stack) << 32) | mem_r32(stack + 4); stack += 8;
        double d; memcpy(&d, &u, 8); return d;
    };
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%') { out.push_back(fmt[i]); continue; }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && strchr("-+ #0123456789.*", fmt[j])) {
            if (fmt[j] == '*') spec += std::to_string((int32_t)next_int());
            else spec.push_back(fmt[j]);
            j++;
        }
        int lng = 0;
        while (j < fmt.size() && strchr("hlLqjzt", fmt[j])) { if (fmt[j] == 'l' || fmt[j] == 'L' || fmt[j] == 'q') lng++; j++; }
        if (j >= fmt.size()) break;
        char conv = fmt[j];
        char buf[512];
        switch (conv) {
        case 'd': case 'i':
            if (lng >= 2) snprintf(buf, sizeof(buf), (spec + "lld").c_str(), (long long)next_i64());
            else snprintf(buf, sizeof(buf), (spec + "d").c_str(), (int32_t)next_int());
            out += buf; break;
        case 'u': case 'x': case 'X': case 'o':
            if (lng >= 2) snprintf(buf, sizeof(buf), (spec + "ll" + conv).c_str(), (unsigned long long)next_i64());
            else snprintf(buf, sizeof(buf), (spec + conv).c_str(), next_int());
            out += buf; break;
        case 'c': snprintf(buf, sizeof(buf), (spec + "c").c_str(), (int)next_int()); out += buf; break;
        case 'p': snprintf(buf, sizeof(buf), "0x%08x", next_int()); out += buf; break;
        case 's': snprintf(buf, sizeof(buf), (spec + "s").c_str(), guest_str(next_int()).c_str()); out += buf; break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            snprintf(buf, sizeof(buf), (spec + conv).c_str(), next_dbl()); out += buf; break;
        case '%': out.push_back('%'); break;
        default: out += spec; out.push_back(conv); break;
        }
        i = j;
    }
    return out;
}

extern "C" void hle_OSReport(CPU* c) {
    std::string s = guest_format(c, c->r[3], 4, 1);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    LOG(LOG_OS, "%s", s.c_str());
}

// Walks the guest's stack, innermost first, into `out`. PowerPC EABI: a frame's first word
// is the back chain to its caller's frame, and a function's return address is saved at
// offset 4 of the frame below it -- which is why each step needs both. The innermost return
// address is still in lr, since the callee's prologue has not run.
static void guest_backtrace(CPU* c, std::string& out, int max_frames) {
    char line[256];
    auto frame = [&](uint32_t pc) {
        uint32_t start = 0;
        const char* name = func_containing(pc, &start);
        snprintf(line, sizeof(line), "    %08X  %s+0x%X\n", pc, name, pc - start);
        out += line;
    };
    frame(c->lr);
    uint32_t sp = c->r[1];
    for (int i = 0; i < max_frames; i++) {
        // Stack grows down, so the back chain must climb; anything else is a wild pointer
        // and following it would walk off into guest RAM printing noise.
        if (sp < 0x80000000u || sp >= 0x81800000u) break;
        uint32_t next = mem_r32(sp);
        if (next <= sp || next >= 0x81800000u) break;
        uint32_t lr = mem_r32(next + 4);
        if (lr < 0x80003000u || lr >= 0x81800000u) break;
        frame(lr);
        sp = next;
    }
}

// The game's own OSPanic: (file, line, format, ...). Its real body prints through the SDK's
// vprintf -- not the OSReport path above, the only one the runtime logs -- and then falls
// into PPCHalt, an infinite loop with interrupts disabled. In a recomp that is faithful and
// useless: the process sits there spinning on the interrupt poll, producing no frames and
// saying nothing, and looks exactly like the port having deadlocked. One such panic cost a
// long session of profiling to identify as the game stopping on purpose.
//
// So the body is replaced by this: say what the game was going to say, with the guest's own
// backtrace, and stop. Stopping rather than spinning is the point -- the game is over either
// way, and a process that dies explaining itself beats one that freezes. fatal() puts the
// first line somewhere the app's own logging cannot lap it (see plat_record_fatal).
extern "C" void hle_OSPanic(CPU* c) {
    // Both of these are arbitrary guest text -- the message usually ends in a newline, and
    // neither pointer is trustworthy on the path that gets here. Flattened, so that the one
    // line fatal() records stays one line and one log entry.
    auto flatten = [](std::string s) {
        for (char& ch : s)
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    const std::string file = flatten(guest_str(c->r[3], 256));
    const uint32_t line = c->r[4];
    const std::string msg = flatten(guest_format(c, c->r[5], 6, 1));
    std::string report = "guest panic: " + file + ":" + std::to_string(line) + ": " + msg +
                         "\nguest backtrace (innermost first):\n";
    guest_backtrace(c, report, 16);
    // The heaps too: the panic this was written for is an allocation failing, and the
    // question that follows is whether the heap was exhausted or merely fragmented.
    heap_report(report);
    fprintf(stderr, "%s", report.c_str());
    // And once more where a log ring cannot lap it. On a headset the only reader is adb
    // after the fact, by which time a session's worth of logging has usually pushed the
    // backtrace out; fatal() preserves its one line in the crash record but has nowhere to
    // put sixteen more. Best effort -- a panic that cannot write a file still has to report.
    const std::string path = (g_save_dir ? std::string(g_save_dir) : std::string(".")) + "/panic.txt";
    if (FILE* f = fopen(path.c_str(), "wb")) {
        fwrite(report.data(), 1, report.size(), f);
        fclose(f);
        fprintf(stderr, "panic report written to %s\n", path.c_str());
    }
    fatal("guest panic: %s:%u: %s", file.c_str(), line, msg.c_str());
}

// ---------------------------------------------------------------------------
// OSResetSystem. On the console this runs the SDK's reset callbacks and then either
// reboots the game from the disc (a restart: __OSReboot clears the exception and interrupt
// tables and loads the apploader) or resets the hardware. Neither is emulated, and a game
// left to carry on after its own reboot has begun dies on the next interrupt, through the
// table it has just cleared. So the call ends the run here, and the frontend starts the
// game again (guest_reset). The memory card needs nothing: it is written out at the end
// of each card command.
static ResetHandler g_reset_handler;
void set_reset_handler(ResetHandler fn) { g_reset_handler = fn; }

void guest_reset(uint32_t kind, uint32_t code) {
    static const char* const kinds[] = {"restart", "hot reset", "shutdown"};
    fprintf(stderr, "[reset] the game reset the console (%s, code %u)\n", kind < 3 ? kinds[kind] : "unknown", code);
    input_log_end();
    fflush(nullptr);
    if (g_reset_handler && kind != 2) g_reset_handler(kind, code);
    plat_exit_now(0);
}

extern "C" void hle_OSResetSystem(CPU* c) { guest_reset(c->r[3], c->r[4]); }
