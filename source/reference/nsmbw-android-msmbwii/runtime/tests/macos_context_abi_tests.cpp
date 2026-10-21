#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" void mkw_co_switch(void** targetSp, void** sourceSp);
extern "C" void* mkw_co_init(void* stackTop, void (*entry)(void*), void* argument);

namespace {
// Exercise the raw AArch64 context ABI independently of HostContext so a
// callee-saved-register or stack-frame regression is localized to this layer.

std::array<std::byte, 64 * 1024> g_workerStack{};
void* g_schedulerSp = nullptr;
void* g_workerSp = nullptr;
std::vector<int> g_events;

void Worker(void*) {
    g_events.push_back(1);
    mkw_co_switch(&g_schedulerSp, &g_workerSp);
    g_events.push_back(2);
    mkw_co_switch(&g_schedulerSp, &g_workerSp);
    std::abort();
}

} // namespace

int main() {
    g_workerSp = mkw_co_init(g_workerStack.data() + g_workerStack.size(), Worker, nullptr);
    if (!g_workerSp) {
        std::cerr << "failed to create AArch64 context frame\n";
        return 1;
    }
    mkw_co_switch(&g_workerSp, &g_schedulerSp);
    if (g_events != std::vector<int>{1}) {
        std::cerr << "worker did not yield to scheduler\n";
        return 1;
    }
    mkw_co_switch(&g_workerSp, &g_schedulerSp);
    if (g_events != std::vector<int>{1, 2}) {
        std::cerr << "worker did not resume from saved context\n";
        return 1;
    }
    return 0;
}
