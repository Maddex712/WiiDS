#include "lower_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace wiids::mkdd {
namespace {
// USA GM4E01 main.dol SHA1 e389e50357d4a5076ab3d38672204658f411123d.
// See FIELD_EVIDENCE.md. No addresses from the debug decomp are used here.
constexpr uint32_t kManager = 0x803cb7e8, kPause = 0x803cb6dc;
constexpr uint32_t kControl = 0x803cc588;
// r13 (0x803D1420) - 21728: the slot retail getKartEquipItem callers load (e.g. 0x80245488
// `lwz r30,-21728(r13)`). 0x803cbec0 was -0x5560 misread; live check 2026-11-02 found the
// stock kinds (16 = empty), equip pointers and shuffle managers here.
constexpr uint32_t kItemManager = 0x803cbf40;
constexpr uint32_t kRam = 0x80000000;
constexpr size_t kRamBytes = 0x1800000;
int32_t bits(float v) { int32_t result; std::memcpy(&result, &v, 4); return result; }
bool finite(float v) { return std::isfinite(v) && std::abs(v) <= 10000000.0f; }
bool race_mode(int mode) { return mode >= 1 && mode <= 7; }
std::mutex lock;
Packet current = unavailable();
}

bool Memory::span(uint32_t address, size_t length, size_t alignment) const {
    if (!data_ || !alignment || address % alignment || address < kRam) return false;
    const size_t offset = address - kRam;
    const size_t limit = std::min(bytes_, kRamBytes);
    return offset <= limit && length <= limit - offset;
}
uint8_t Memory::u8(uint32_t a) const { return span(a, 1, 1) ? data_[a-kRam] : 0; }
uint16_t Memory::u16(uint32_t a) const {
    return span(a, 2, 2) ? (uint16_t(u8(a)) << 8) | u8(a+1) : 0;
}
uint32_t Memory::u32(uint32_t a) const {
    return span(a, 4) ? (uint32_t(u16(a)) << 16) | u16(a+2) : 0;
}
float Memory::f32(uint32_t a) const {
    uint32_t word = u32(a); float result; std::memcpy(&result, &word, 4); return result;
}

Packet unavailable(uint32_t frame) {
    Packet p{};
    p[Magic] = kMagic; p[Version] = 1; p[Words] = kWords; p[Frame] = int32_t(frame);
    p[Course] = -1; p[TargetKart] = -1;
    for (size_t i = 0; i < kKarts; ++i) {
        auto b = kHeader + i * kStride;
        p[b+Rank] = p[b+Lap] = p[b+TimeMs] = -1;
        p[b+Character0] = p[b+Character1] = p[b+Driver] = -1;
        p[b+Item0] = p[b+Item1] = -1;
        p[b+LapProgress] = bits(NAN);
    }
    return p;
}

Packet sample(const Memory& m, uint32_t frame) {
    Packet p = unavailable(frame);
    if (!m.span(kManager, 4) || !m.span(kPause, 1, 1)) return p;
    uint32_t manager = m.u32(kManager);
    if (!m.span(manager, 0x68)) return p;
    uint32_t director = m.u32(manager+0x18), info = m.u32(manager+0x38);
    if (!m.span(director, 0x34) || !m.span(info, 0x2a0)) return p;
    int mode = int32_t(m.u32(info+8));
    int count = int16_t(m.u16(info+0x1c)), players = int16_t(m.u16(info+0x1e));
    int screens = int16_t(m.u16(info+0x20));
    int phase = int32_t(m.u32(director+0xc)), run = int32_t(m.u32(director+0x30));
    if (!race_mode(mode) || count < 1 || count > 8 || players < 0 || players > 8 ||
        screens < 1 || screens > 4 || phase < 0 || phase > 19 || run < 0 || run > 2) return p;
    p[Flags] = Race | (m.u32(info+0x28) ? Demo : 0) | (m.u8(kPause) ? Paused : 0);
    p[Mode] = mode; p[Phase] = phase; p[RunState] = run;
    p[KartCount] = count; p[Players] = players; p[Screens] = screens;
    p[Mirror] = m.u8(info+0x2c) ? 1 : 0;
    int laps = int16_t(m.u16(manager+0x2e));
    if (mode <= 3 && laps > 0 && laps <= 99) p[TotalLaps] = laps;
    int target = int16_t(m.u16(info+0x114));
    if (target >= 0 && target < count) p[TargetKart] = target;
    uint32_t course = m.u32(manager+0x44);
    if (m.span(course, 4)) {
        int id = int32_t(m.u32(course));
        if (id >= 0x21 && id <= 0x3b) p[Course] = id;
    }
    uint32_t control = m.span(kControl, 4) ? m.u32(kControl) : 0;
    uint32_t items = m.span(kItemManager, 4) ? m.u32(kItemManager) : 0;
    for (int i = 0; i < count; ++i) {
        size_t b = kHeader + i*kStride;
        uint32_t checker = m.u32(manager+0x48+i*4);
        if (m.span(checker, 0x8c)) {
            int rank = int32_t(m.u32(checker+0x88));
            if (rank >= 1 && rank <= count) { p[b+Rank] = rank; p[b+Valid] |= Standing; }
            int lap = int32_t(m.u32(checker+0x2c));
            if (p[TotalLaps] && lap >= 0 && lap <= p[TotalLaps]) {
                p[b+Lap] = lap; p[b+Valid] |= Laps;
                p[b+Finished] = m.u8(checker+0x29) ? 1 : 0;
            }
            int ms = int32_t(m.u32(checker+0x84));
            if (ms >= 0 && ms < 5999999) { p[b+TimeMs] = ms; p[b+Valid] |= Time; }
            float progress = m.f32(checker+0x48);
            if (std::isfinite(progress) && progress >= 0 && progress <= 1)
                p[b+LapProgress] = bits(progress);
        }
        uint32_t ki = info+0x30+i*0x18;
        uint32_t ch0 = m.u32(ki+8), ch1 = m.u32(ki+0x10);
        if (m.span(ch0, 2, 2) && m.span(ch1, 2, 2)) {
            int c0 = m.u16(ch0), c1 = m.u16(ch1);
            if (c0 >= 1 && c0 <= 20 && c1 >= 1 && c1 <= 20) {
                p[b+Character0] = c0; p[b+Character1] = c1; p[b+Valid] |= Characters;
            }
        }
        // Original rider slots, independent of which rider is currently driving.
        // Stocked but not equipped (including roulette transitions) stays unknown.
        if (m.span(items, 0x39c)) {
            int kinds[2] = {-1,-1};
            for (int slot=0; slot<2; ++slot) {
                uint32_t obj=m.u32(items+0x2cc+i*8+slot*4);
                uint32_t shuffle=m.u32(items+0x35c+i*8+slot*4);
                if (obj==0 && m.u32(items+0x24c+i*16+slot*8)==16 &&
                    m.span(shuffle,8) && !(m.u8(shuffle+6)&3)) kinds[slot]=16;
                else if (m.span(obj,0x128) && m.u32(obj+0x120)==uint32_t(i)
                         && m.u8(obj+0x124)==slot && m.u32(obj+0x7c)<=21)
                    kinds[slot]=int32_t(m.u32(obj+0x7c));
            }
            if (kinds[0]>=0 && kinds[1]>=0) {
                p[b+Item0]=kinds[0]; p[b+Item1]=kinds[1]; p[b+Valid]|=Items;
            }
        }
        if (!m.span(control, 0xc0)) continue;
        uint32_t body = m.u32(control+0xa0+i*4);
        if (!m.span(body, 0x5b4)) continue;
        float x = m.f32(body+0x11c), y = m.f32(body+0x12c), z = m.f32(body+0x13c);
        float fx = m.f32(body+0x118), fz = m.f32(body+0x138), speed = m.f32(body+0x444)*2.16f;
        if (m.u32(body+0x578)&0x200) speed=0; // KartCtrl::GetCarSpeed's original gate.
        if (finite(x) && finite(y) && finite(z) && finite(fx) && finite(fz) &&
            std::isfinite(speed) && std::abs(speed) <= 1000 && fx*fx+fz*fz > 0.01f) {
            p[b+X] = bits(x); p[b+Y] = bits(y); p[b+Z] = bits(z);
            p[b+Heading] = bits(std::atan2(fx, fz)); p[b+SpeedKmh] = bits(speed);
            p[b+Valid] |= Position;
        }
        int driver = m.u8(body+0x5b2);
        if (driver <= 1) { p[b+Driver] = driver; p[b+Valid] |= DriverKnown; }
    }
    return p;
}

void publish(Packet packet) { std::lock_guard<std::mutex> guard(lock); current = packet; }
Packet latest() { std::lock_guard<std::mutex> guard(lock); return current; }
}  // namespace wiids::mkdd
