// Run the actual TDrawSyncManager::threadFunc over scripted messages: FIFO
// pointers in MEM1 at each arena base, u16 draw-sync tokens and the 0x10000
// shutdown message.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
#include DRAW_SYNC_SOURCE
#undef main

static std::vector<void*> s_script;
static size_t s_next;
static std::vector<void*> s_breaks; // GXEnableBreakPt targets, nullptr for GXDisableBreakPt
extern "C" int OSReceiveMessage(struct OSMessageQueue*, void* msg, long)
{
    *static_cast<void**>(msg) = s_next < s_script.size() ? s_script[s_next] : (void*)0x10000;
    ++s_next;
    return 1;
}
void GXEnableBreakPt(void* p) { s_breaks.push_back(p); }
void GXDisableBreakPt(void) { s_breaks.push_back(nullptr); }

static int failures, checks;
static void check(bool ok, const char* what, uintptr_t base) {
    ++checks;
    if (!ok && failures++ < 4)
        std::fprintf(stderr, "FAIL %s: arena %#llx\n", what, static_cast<unsigned long long>(base));
}
int main(int argc, char**) {
    const uintptr_t bases[] = {0x80000000u, 0x40800000u, 0x40100000u, 0x84000000u};
    const unsigned count = argc > 1 ? 1 : sizeof bases / sizeof bases[0];
    for (unsigned i = 0; i < count; ++i) {
        const uintptr_t b = bases[i];
        void* const a = (void*)(b + 0x123400), *const c = (void*)(b + 0x200020), *const d = (void*)(b + 0x3ffffe0);
        s_script = {a, c, (void*)0, (void*)0x7d, d, (void*)0xffff, (void*)0x10000, a};
        s_next = 0; s_breaks.clear();
        alignas(TDrawSyncManager) unsigned char storage[sizeof(TDrawSyncManager)];
        std::memset(storage, 0, sizeof storage);
        TDrawSyncManager* self = reinterpret_cast<TDrawSyncManager*>(storage);
        self->mFifo = new TFifo(8);
        TDrawSyncManager::threadFunc(self);
        // Stops at the shutdown message, not at the first pointer.
        check(s_next == 7, "thread runs until the shutdown message", b);
        // A second queued pointer sets a breakpoint there; a token retiring
        // back to one queued pointer clears it.
        const std::vector<void*> wanted = {c, nullptr};
        check(s_breaks == wanted, "breakpoints follow queued FIFO pointers", b);
        delete self->mFifo;
    }
    std::printf("%s draw sync FIFO pointers: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
