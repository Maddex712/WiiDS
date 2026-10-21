// Compile the actual JRenderer writers and inspect their emitted GD bytes.
#undef main
#include <JSystem/JRenderer.hpp>
#include <dolphin/os.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>

extern "C" {
u8* port_mem1_base;
GDLObj* __GDCurrentDL;
void GDOverflowed() { std::abort(); }
}
// Standard BP register table normally provided by J3DTevs.cpp.
u8 GXTexImage3Ids[8] = {0x94,0x95,0x96,0x97,0xb4,0xb5,0xb6,0xb7};

static int failures, checks;
static void check(bool ok, const char* what, uintptr_t base, uint32_t offset) {
    ++checks;
    if (!ok && failures++ < 4)
        std::fprintf(stderr, "FAIL %s: arena %#llx offset %#x\n", what,
                     static_cast<unsigned long long>(base), offset);
}
static uint32_t bp(const u8* bytes) {
    return (uint32_t(bytes[1]) << 24) | (uint32_t(bytes[2]) << 16) |
           (uint32_t(bytes[3]) << 8) | bytes[4];
}
static void sample(uintptr_t base, uint32_t offset, int map) {
    port_mem1_base = reinterpret_cast<u8*>(base);
    void* image = reinterpret_cast<void*>(base + offset);
    // Surround each real display-list write with guard bytes.
    u8 bytes[64]; std::memset(bytes, 0xa5, sizeof bytes);
    GDLObj dl = {bytes + 4, 48, bytes + 4, bytes + 52};
    __GDCurrentDL = &dl;
    J3DGDSetTexImgPtr(GXTexMapID(map), image);
    const uint8_t ids[8] = {0x94,0x95,0x96,0x97,0xb4,0xb5,0xb6,0xb7};
    const uint32_t command = bp(bytes + 4);
    check(dl.ptr == bytes + 9 && bytes[4] == GX_CMD_LOAD_BP_REG,
          "texture command length/opcode", base, offset);
    check(command == (uint32_t(ids[map]) << 24 | offset >> 5),
          "texture physical address/register", base, offset);
    check(base + ((command & 0xffffff) << 5) == reinterpret_cast<uintptr_t>(image),
          "texture renderer pointer round trip", base, offset);
    check(bytes[3] == 0xa5 && bytes[9] == 0xa5, "texture bounds", base, offset);

    std::memset(bytes, 0xa5, sizeof bytes); dl.ptr = bytes + 4;
    const uint32_t tmem = 0xc0000 + uint32_t(map) * 0x200;
    const GXTlutSize size = GX_TLUT_256;
    J3DGDLoadTlut(image, tmem, size);
    const uint32_t wanted[] = {0xfeffff00,0x0f000000,0x64000000 | (offset >> 5),
        0x65000000 | ((tmem - 0x80000) >> 9) | (uint32_t(size) << 10),
        0xfeffff00,0x0f000000};
    check(dl.ptr == bytes + 34, "palette command length", base, offset);
    for (unsigned i = 0; i < 6; ++i)
        check(bytes[4 + i * 5] == GX_CMD_LOAD_BP_REG && bp(bytes + 4 + i * 5) == wanted[i],
              "palette physical address/register/flush sequence", base, offset);
    check(base + ((bp(bytes + 14) & 0xffffff) << 5) == reinterpret_cast<uintptr_t>(image),
          "palette renderer pointer round trip", base, offset);
    check(bytes[3] == 0xa5 && bytes[34] == 0xa5, "palette bounds", base, offset);
}
int main(int argc, char**) {
    // The original address is also checked with the unchanged desktop SDK.
    const uintptr_t bases[] = {0x80000000u,0x40800000u,0x40100000u,0x84000000u,0xb0000000u};
    const unsigned count = argc > 1 ? 1 : sizeof bases / sizeof bases[0];
    for (unsigned i = 0; i < count; ++i) {
        for (int map = 0; map < 8; ++map) {
            for (uint32_t offset : {0x20u,0x4000u,0x100000u,0x3ffffe0u}) sample(bases[i], offset, map);
            // Varied aligned addresses across the full 64 MiB arena.
            for (uint32_t n = 1; n <= 128; ++n)
                sample(bases[i], ((n * 0x1f123u) & 0x3ffffffu) & ~31u, map);
        }
    }
    std::printf("%s J3D texture/palette command bytes: %d checks, %d failures\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
