#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <mutex>

struct CpuContext { uint32_t gpr[32]{}; uint32_t lr = 0, ctr = 0; };
class CpuContextScope { public: explicit CpuContextScope(CpuContext*) {} };
CpuContext& GetPersistentCpuContext();
void InvokeIndirectCpu(uint32_t, CpuContext*);
namespace TranslatedFunctionRegistry { const void* FindByAddressPtr(uint32_t); }
void LogNandError(const char*, const char*, ...);
class Memory { public: static void* GetPointer(uint32_t); };
namespace Fiber {
class GuestFiberManager {
public:
    static uint32_t GetCurrentGuestThread();
    static bool HasRegisteredGuestThreads();
};
}
#define PPC_NATIVE_OVERRIDE(...)
#define PPC_NATIVE_OVERRIDE_VOID(...)
