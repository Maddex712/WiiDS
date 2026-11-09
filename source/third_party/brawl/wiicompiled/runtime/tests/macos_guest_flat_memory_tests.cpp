#include "guest_flat_memory.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

int main() {
    GuestFlat::Initialize({
        {0x00000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x80000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x10000000u, 0x4000u, GuestFlat::Backing::Mem2},
        {0x90000000u, 0x4000u, GuestFlat::Backing::Mem2},
    });
    if (!GuestFlat::IsActive()) {
        std::cerr << "guest address space did not become active\n";
        return 1;
    }
    if (GuestFlat::RequiresCheckedAccess() !=
        (static_cast<size_t>(getpagesize()) > GuestFlat::kGuestPageSize)) {
        std::cerr << "guest access mode does not reflect the host page size\n";
        return 1;
    }
    auto* mem1Physical = GuestFlat::HostPointer(0x00000000u);
    auto* mem1Cached = GuestFlat::HostPointer(0x80000000u);
    auto* mem2Physical = GuestFlat::HostPointer(0x10000000u);
    auto* mem2Cached = GuestFlat::HostPointer(0x90000000u);
    if (!mem1Physical || !mem1Cached || !mem2Physical || !mem2Cached) {
        std::cerr << "missing host alias\n";
        return 1;
    }
    if (GuestFlat::HostPointer(0x4000u) != nullptr ||
        GuestFlat::HostPointer(0xa0000000u) != nullptr) {
        std::cerr << "unmapped guest address resolved to host memory\n";
        return 1;
    }
    mem1Physical[7] = 0x5a;
    mem2Cached[9] = 0xa5;
    const auto* guest = reinterpret_cast<const uint8_t*>(GuestFlat::kFixedFlatGuestBase);
    if (mem1Cached[7] != 0x5a || guest[0x80000007u] != 0x5a ||
        mem2Physical[9] != 0xa5 || guest[0x10000009u] != 0xa5) {
        std::cerr << "guest aliases are not coherent\n";
        return 1;
    }
    auto* guestWritable = reinterpret_cast<uint8_t*>(GuestFlat::kFixedFlatGuestBase);
    guestWritable[0x80000008u] = 0x3c;
    guestWritable[0x1000000au] = 0xc3;
    if (mem1Physical[8] != 0x3c || mem2Cached[10] != 0xc3) {
        std::cerr << "guest writes were not visible through host aliases\n";
        return 1;
    }

    GuestFlat::Initialize({
        {0x00000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x80000000u, 0x4000u, GuestFlat::Backing::Mem1},
        {0x10000000u, 0x4000u, GuestFlat::Backing::Mem2},
        {0x90000000u, 0x4000u, GuestFlat::Backing::Mem2},
    });
    try {
        GuestFlat::Initialize({{0x00000000u, 0x8000u, GuestFlat::Backing::Mem1}});
        std::cerr << "guest address space accepted a different layout\n";
        return 1;
    } catch (const std::runtime_error&) {
    }
    return 0;
}
