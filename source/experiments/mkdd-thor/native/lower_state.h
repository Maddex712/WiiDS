#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wiids::mkdd {
constexpr uint32_t kMagic = 0x4d4b4444;
constexpr size_t kHeader = 16, kStride = 16, kKarts = 8;
constexpr size_t kWords = kHeader + kStride * kKarts;
enum Header { Magic, Version, Words, Frame, Flags, Mode, Phase, RunState,
              Course, KartCount, Players, Screens, TotalLaps, TargetKart, Mirror, CourseSet };
enum Flag { Race = 1, Demo = 2, Paused = 4 };
enum KartField { Valid, Rank, Lap, TimeMs, Character0, Character1, Driver,
                 Item0, Item1, X, Y, Z, Heading, SpeedKmh, LapProgress, Finished };
enum KartFlag { Position = 1, Standing = 2, Characters = 4, Items = 8,
                Laps = 16, Time = 32, DriverKnown = 64 };
using Packet = std::array<int32_t, kWords>;

// A read-only, bounded view of the runtime's 24 MiB MEM1. Sample only on the
// guest frame thread. No guest pointers escape into the published packet.
class Memory {
public:
    Memory(const uint8_t* data, size_t bytes) : data_(data), bytes_(bytes) {}
    bool span(uint32_t address, size_t length, size_t alignment = 4) const;
    uint32_t u32(uint32_t address) const;
    uint16_t u16(uint32_t address) const;
    uint8_t u8(uint32_t address) const;
    float f32(uint32_t address) const;
private:
    const uint8_t* data_;
    size_t bytes_;
};

Packet unavailable(uint32_t frame = 0);
Packet sample(const Memory& memory, uint32_t frame);
// Called by the native guest-frame hook, then copied under a short mutex for JNI.
void publish(Packet packet);
Packet latest();
}  // namespace wiids::mkdd
