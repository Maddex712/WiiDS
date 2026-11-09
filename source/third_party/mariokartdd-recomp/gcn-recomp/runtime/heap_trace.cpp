// Guest heap diagnostics: what the SDK heaps hold, and who holds it.
//
// The game manages its memory with the SDK's OSAlloc: HeapArray[] of {size, free, allocated},
// each a doubly linked list of cells {prev, next, size} with a 32-byte header, the payload
// following it. OSAllocFromHeap walks the free list for the first cell that fits and returns
// NULL when none does -- which is what the course menu turns into a panic
// ("cannot allocate memory for the frame buffer copy"). A panic says the heap was full; it
// does not say with what, and the SDK keeps no record of who allocated a cell.
//
// Two things here fill that in:
//
//   heap_report()  walks the heaps out of guest memory -- totals, block counts, the largest
//                  free block -- and is appended to every panic report, so a headset that
//                  died can still say whether the heap was exhausted or fragmented.
//
//   GCN_HEAP=1      keeps a table of live cells keyed by the return address of whoever called
//                  OSAllocFromHeap, via three hooks that the game's patches.txt splices into
//                  the allocator's entry and both of its returns, and one into
//                  OSFreeToHeap's entry (see docs/diagnostics.md for the four lines). A failed allocation (and a panic) then prints the live cells
//                  grouped by caller, which is what a leak looks like: one caller, many
//                  cells, the count growing with each visit. GCN_HEAP=<frames> also prints
//                  that table every so many presented frames, to watch the trend without
//                  waiting for the crash.
//
// The hooks run on the guest thread, which is serialised, but an interrupt in the
// allocator's search loop can switch guest threads between entry and return, so the pending
// request is keyed by the CPU making it.
#include "runtime.h"
#include <algorithm>
#include <tuple>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

// The SDK's HeapArray variable: a pointer to the descriptors OSInitAlloc carved out of the
// arena. Its address is the game's business -- OSAllocFromHeap loads it from a small-data
// offset off r13 -- so the game's patches.txt passes it to the entry hook, and until that
// hook has run the report has no heaps to walk.
uint32_t g_heap_array_var;
constexpr int MAX_HEAPS = 8;

bool in_ram(uint32_t a) { return a >= 0x80000000u && a < 0x80000000u + RAM_SIZE; }

struct ListStats { uint32_t blocks = 0, bytes = 0, largest = 0; bool broken = false; };

ListStats walk(uint32_t cell) {
    ListStats s;
    for (int guard = 0; cell; guard++) {
        if (!in_ram(cell) || guard > 200000) { s.broken = true; break; }
        uint32_t size = mem_r32(cell + 8);
        s.blocks++;
        s.bytes += size;
        s.largest = std::max(s.largest, size);
        cell = mem_r32(cell + 4);
    }
    return s;
}

// Two return addresses per cell, because the game reaches OSAllocFromHeap through thin
// wrappers (fn_80036AA0 is one) and the wrapper's own caller is the one worth naming.
// The second comes off the guest stack: the wrapper's frame holds the back chain, and its
// return address sits at +4 of the frame that chain points to.
struct Live { uint32_t size, caller, caller2, heap; };
struct Pending { uint32_t size, caller, caller2, heap; };

uint32_t caller_above(CPU* c) {
    uint32_t sp = c->r[1];
    if (!in_ram(sp)) return 0;
    uint32_t chain = mem_r32(sp);
    if (!in_ram(chain) || chain <= sp) return 0;
    return mem_r32(chain + 4);
}

bool g_trace = false;
uint32_t g_every = 0;              // presented frames between timed reports, 0 for none
std::unordered_map<uint32_t, Live> g_live;
std::unordered_map<CPU*, Pending> g_pending;
int g_failures = 0;

void init() {
    static bool done = false;
    if (done) return;
    done = true;
    if (const char* e = getenv("GCN_HEAP")) {
        g_trace = true;
        g_every = (uint32_t)strtoul(e, nullptr, 10);
        if (g_every == 1) g_every = 0;  // GCN_HEAP=1 is just "on"
    }
}

void live_by_caller(std::string& out) {
    struct Key { uint32_t heap, caller, caller2; bool operator<(const Key& o) const { return std::tie(heap, caller, caller2) < std::tie(o.heap, o.caller, o.caller2); } };
    struct Agg { uint32_t count = 0, bytes = 0; };
    std::map<Key, Agg> by_caller;
    uint32_t total = 0;
    for (auto& [ptr, l] : g_live) {
        auto& a = by_caller[Key{l.heap, l.caller, l.caller2}];
        a.count++;
        a.bytes += l.size;
        total += l.size;
    }
    std::vector<std::pair<Key, Agg>> rows(by_caller.begin(), by_caller.end());
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second.bytes > b.second.bytes; });
    char line[256];
    snprintf(line, sizeof(line), "live cells by caller (GCN_HEAP): %u cells, %u bytes\n", (unsigned)g_live.size(), total);
    out += line;
    int shown = 0;
    for (auto& [k, a] : rows) {
        if (shown++ >= 40) { out += "    ...\n"; break; }
        uint32_t start = 0, start2 = 0;
        const char* name = func_containing(k.caller, &start);
        const char* name2 = func_containing(k.caller2, &start2);
        snprintf(line, sizeof(line), "    heap %u %6u cells %9u bytes  %08X %s+0x%X <- %08X %s+0x%X\n", k.heap, a.count, a.bytes,
                 k.caller, name, k.caller - start, k.caller2, name2, k.caller2 - start2);
        out += line;
    }
}

}  // namespace

void heap_report(std::string& out) {
    char line[256];
    if (!g_heap_array_var) { out += "guest heaps: not traced (no OSAllocFromHeap hook in patches.txt)\n"; return; }
    const uint32_t heap_array = mem_r32(g_heap_array_var);
    snprintf(line, sizeof(line), "guest heaps (HeapArray %08X):\n", heap_array);
    out += line;
    if (!in_ram(heap_array)) { out += "    (no HeapArray)\n"; return; }
    for (int h = 0; h < MAX_HEAPS; h++) {
        uint32_t desc = heap_array + 12 * h;
        uint32_t size = mem_r32(desc), free_list = mem_r32(desc + 4), alloc_list = mem_r32(desc + 8);
        // OSInitAlloc marks the unused descriptors with size -1; the array has no other end
        // marker, so the first implausible size is taken as the end of it.
        if (size == 0 || size > RAM_SIZE) break;
        ListStats f = walk(free_list), a = walk(alloc_list);
        snprintf(line, sizeof(line),
                 "    heap %d: size %u; free %u in %u blocks, largest %u%s; allocated %u in %u blocks%s\n",
                 h, size, f.bytes, f.blocks, f.largest, f.broken ? " (list broken)" : "",
                 a.bytes, a.blocks, a.broken ? " (list broken)" : "");
        out += line;
    }
    init();
    if (g_trace) live_by_caller(out);
}

// OSAllocFromHeap(heap r3, size r4), entry. `heap_array_var` is the address of the SDK's
// HeapArray pointer, which only the game's tables know.
extern "C" void heap_trace_alloc(CPU* c, uint32_t heap_array_var) {
    g_heap_array_var = heap_array_var;
    init();
    if (!g_trace) return;
    g_pending[c] = Pending{c->r[4], c->lr, caller_above(c), c->r[3]};
}

// Once per presented frame, on the guest thread (gx::submit_batch): the timed report.
void heap_trace_frame(uint32_t frame) {
    init();
    if (!g_every || frame % g_every) return;
    std::string s;
    char line[64];
    snprintf(line, sizeof(line), "frame %u: ", frame);
    s += line;
    heap_report(s);
    fprintf(stderr, "%s", s.c_str());
}

// Both returns of OSAllocFromHeap: r3 is the cell's payload, or 0 when nothing fit.
extern "C" void heap_trace_alloc_result(CPU* c, int ok) {
    if (!ok) {
        // A failed allocation is rare enough to be worth a report whether or not tracing
        // is on: the panic that usually follows gets its own, but not every caller panics.
        if (g_failures++ < 4) {
            std::string s;
            char line[128];
            snprintf(line, sizeof(line), "OSAllocFromHeap failed: heap %u, %u bytes, from %08X %s\n",
                     c->r[3], g_trace ? g_pending[c].size : 0u, c->lr, func_containing(c->lr, nullptr));
            s += line;
            heap_report(s);
            fprintf(stderr, "%s", s.c_str());
        }
        g_pending.erase(c);
        return;
    }
    if (!g_trace) return;
    auto it = g_pending.find(c);
    if (it == g_pending.end()) return;
    g_live[c->r[3]] = Live{it->second.size, it->second.caller, it->second.caller2, it->second.heap};
    g_pending.erase(it);
}

// OSFreeToHeap(heap r3, ptr r4), entry.
extern "C" void heap_trace_free(CPU* c) {
    if (!g_trace) return;
    g_live.erase(c->r[4]);
}
