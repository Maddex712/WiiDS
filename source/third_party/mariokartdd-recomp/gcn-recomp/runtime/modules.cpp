// Code the game loads from its data files at run time.
//
// Some games keep code in their data files -- an object's setup routine beside its model --
// and load it wherever their allocator puts it. A static recompilation needs that code at
// build time and at a fixed address, so the game project's DOL fixup extracts each distinct
// module from the disc and lays it out at a canonical address the game never uses, and
// the recompiler compiles it there like any other code and keeps its bytes
// (g_recomp_modules). Such code is position-independent -- it reaches the rest of the
// game through pointers it is handed, and the loader relocates only its data -- so one
// compilation serves every copy.
//
// At run time nothing is known until the game calls into a module. call_indirect then
// finds no function at the address and asks here: the words at the address are looked up
// among the first words of every module function, each candidate names the module and the
// offset, and the module's entry bytes are checked at the base that implies. A match is an
// instance: from then on an address in [base, base + size) maps to the canonical one
// (modules_canon), which is what the function table and the module's own jump tables
// (`switch (modules_canon(c->ctr))`, from the recompiler) are keyed by. An instance is
// checked against the module's bytes whenever it is used, so a module the game unloaded
// and replaced with another at the same place is simply rebound.
#include "modules.h"

#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "recomp.h"
#include "runtime.h"

namespace {

struct Candidate { uint32_t module, off; };
std::unordered_map<uint64_t, std::vector<Candidate>> g_sigs;

// An instance covers the module's code, not its whole region: the game packs modules
// closer than the region's length, and a neighbour's instance must not claim an address.
struct Instance { uint32_t base, size, module; };
constexpr int MAX_INSTANCES = 256;
Instance g_instances[MAX_INSTANCES];
std::atomic<int> g_instance_count{0};

uint64_t key_of(const uint8_t* p) {
    uint64_t k = 0;
    for (int i = 0; i < 16; i++) k = k * 1099511628211ull ^ p[i];
    return k;
}

bool guest_equals(uint32_t addr, const uint8_t* bytes, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (mem_r8(addr + i) != bytes[i]) return false;
    return true;
}

// The instance still holds the module it was bound to.
bool instance_valid(const Instance& in) {
    const RecompModule& m = g_recomp_modules[in.module];
    return in.base && guest_equals(in.base + m.entry, m.bytes + m.entry, 16);
}

}  // namespace

void modules_init() {
    for (uint32_t i = 0; i < g_recomp_func_count; i++) {
        uint32_t a = g_recomp_funcs[i].addr;
        for (uint32_t k = 0; k < g_recomp_module_count; k++) {
            const RecompModule& m = g_recomp_modules[k];
            if (a >= m.canon && a + 16 <= m.canon + m.size) {
                uint32_t off = a - m.canon;
                g_sigs[key_of(m.bytes + off)].push_back({k, off});
            }
        }
    }
}

uint32_t modules_canon(uint32_t a) {
    int n = g_instance_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; i++) {
        Instance& in = g_instances[i];
        if (a - in.base < in.size) {
            if (!instance_valid(in)) { in.base = 0; return a; }
            return g_recomp_modules[in.module].canon + (a - in.base);
        }
    }
    return a;
}

// How much of a module's code, from its entry to its end, is what the memory at `base`
// holds. Modules share library functions and every entry opens with the same prologue,
// so a few words match many of them; the entry function and what follows tell them apart.
static uint32_t match_score(const RecompModule& m, uint32_t base) {
    uint32_t n = 0;
    for (uint32_t i = m.entry; i + 4 <= m.code_end; i += 4)
        if (guest_equals(base + i, m.bytes + i, 4)) n += 4;
    return n;
}

bool modules_bind(uint32_t addr) {
    if (g_sigs.empty() || (addr & 0x3FFFFFFFu) + 16 > RAM_SIZE) return false;
    uint8_t words[16];
    for (int i = 0; i < 16; i++) words[i] = mem_r8(addr + i);
    auto it = g_sigs.find(key_of(words));
    if (it == g_sigs.end()) return false;
    const Candidate* best = nullptr;
    uint32_t best_score = 0;
    for (const Candidate& cand : it->second) {
        const RecompModule& m = g_recomp_modules[cand.module];
        uint32_t base = addr - cand.off;
        if ((base & 0x3FFFFFFFu) + m.size > RAM_SIZE) continue;
        if (!guest_equals(addr, m.bytes + cand.off, 16)) continue;
        uint32_t score = match_score(m, base);
        if (score > best_score) { best_score = score; best = &cand; }
    }
    if (!best) return false;
    const RecompModule& m = g_recomp_modules[best->module];
    uint32_t base = addr - best->off;
    // The code after the entry is relocated nowhere, so a real instance matches nearly all
    // of it; a module that merely shares the called function does not.
    if (best_score * 10 < (m.code_end - m.entry) * 9) {
        LOG(LOG_OS, "module at %08X: best candidate %s matches only %u of %u bytes", addr, m.name, best_score, m.code_end - m.entry);
        return false;
    }
    int n = g_instance_count.load();
    if (n >= MAX_INSTANCES) fatal("modules: more than %d loaded at once", MAX_INSTANCES);
    g_instances[n] = {base, m.code_end, best->module};
    g_instance_count.store(n + 1, std::memory_order_release);
    LOG(LOG_OS, "module %s bound at %08X (called at +%X, %u of %u bytes match)", m.name, base, best->off, best_score, m.code_end - m.entry);
    return true;
}
