#include "gx_guest_write.h"

#include "memory.h"
#include "runtime_log.h"

#include <aurora/gfx.h>

#include <array>
#include <cstdio>

namespace GxGuestWrite {
namespace {

// Aurora holds host pointers into guest RAM (GXTexObj::data, GXTlutObj::data,
// EFB copy destinations) and has no guest-address concept. Both MEM1 and MEM2
// are one contiguous host mapping each - every guest alias of a physical byte
// resolves to the same host address - so a single base per bank inverts the
// translation exactly.
struct GuestRamAlias {
    const void* hostBase = nullptr;
    uint64_t size = 0;
    uint32_t guestBase = 0;
};

std::array<GuestRamAlias, 2> g_aliases{};

bool HostToGuest(const void* hostPtr, size_t size, uint32_t& outAddr) noexcept {
    for (const auto& alias : g_aliases) {
        if (HostRangeToGuest(alias.hostBase, alias.size, alias.guestBase, hostPtr, size, outAddr)) {
            return true;
        }
    }
    return false;
}

bool TooLargeForGuestRange(size_t size) noexcept {
    return static_cast<uint64_t>(size) > static_cast<uint64_t>(UINT32_MAX);
}

uint64_t GuestWriteGenerationHook(const void* hostPtr, size_t size) noexcept {
    uint32_t addr = 0;
    if (TooLargeForGuestRange(size) || !HostToGuest(hostPtr, size, addr)) {
        return kUntracked;
    }
    return GenerationForRange(addr, static_cast<uint32_t>(size));
}

void GuestWriteNotifyHook(const void* hostPtr, size_t size) noexcept {
    uint32_t addr = 0;
    if (TooLargeForGuestRange(size) || !HostToGuest(hostPtr, size, addr)) {
        return;
    }
    NotifyWrite(addr, static_cast<uint32_t>(size));
}

GuestRamAlias ResolveAlias(uint32_t guestBase, size_t size) noexcept {
    try {
        if (const void* host = Memory::GetPointer(guestBase, 1)) {
            return {host, static_cast<uint64_t>(size), guestBase};
        }
    } catch (...) {
    }
    return {};
}

} // namespace

void InstallAuroraHooks() {
    g_aliases[0] = ResolveAlias(Memory::kMem1CachedBase, Memory::kMem1Size);
    g_aliases[1] = ResolveAlias(Memory::kMem2CachedBase, Memory::kMem2Size);
    if (g_aliases[0].hostBase == nullptr || g_aliases[1].hostBase == nullptr) {
        // Without both banks a host pointer cannot be classified, so leaving the
        // hooks uninstalled is the only safe answer: aurora then digests every
        // source on every validation, exactly as it did before.
        RT_LOGF(RT_TAG_GX,
                "guest RAM host aliases unavailable; texture source write tracking "
                "disabled\n");
        return;
    }
    aurora_set_guest_write_hooks(&GuestWriteGenerationHook, &GuestWriteNotifyHook);
}

} // namespace GxGuestWrite
