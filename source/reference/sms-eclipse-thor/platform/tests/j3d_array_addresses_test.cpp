// Compile the actual J3DLoadArrayBasePtr and inspect the CP command it emits.
#undef main
#include <JSystem/J3D/J3DGraphBase/J3DTevs.hpp>
#include <dolphin/os.h>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" u8* port_mem1_base;
u8* port_mem1_base;
static std::vector<uint32_t> s_words;
void GXCmd1u8(u8 x) { s_words.push_back(x); }
void GXCmd1u32(u32 x) { s_words.push_back(x); }

static int failures, checks;
static void check(bool ok, const char* what, uintptr_t base, uint32_t offset) {
    ++checks;
    if (!ok && failures++ < 4)
        std::fprintf(stderr, "FAIL %s: arena %#llx offset %#x\n", what,
                     static_cast<unsigned long long>(base), offset);
}
int main(int argc, char**) {
    // The original address is also checked with the unchanged desktop code.
    const uintptr_t bases[] = {0x80000000u,0x40800000u,0x40100000u,0x84000000u,0xb0000000u};
    const unsigned count = argc > 1 ? 1 : sizeof bases / sizeof bases[0];
    for (unsigned i = 0; i < count; ++i) {
        port_mem1_base = reinterpret_cast<u8*>(bases[i]);
        for (uint32_t n = 0; n <= 256; ++n) {
            const uint32_t offset = n == 256 ? 0x3ffffe0u : (n * 0x3f123u) & 0x3ffffe0u;
            for (GXAttr attr : {GX_VA_POS, GX_VA_NRM, GX_VA_CLR0, GX_VA_TEX0, GX_VA_NBT}) {
                s_words.clear();
                J3DLoadArrayBasePtr(attr, reinterpret_cast<void*>(bases[i] + offset));
                const uint32_t reg = attr == GX_VA_NBT ? 0xa1 : 0xa0 + attr - GX_VA_POS;
                check(s_words.size() == 3 && s_words[0] == 8 && s_words[1] == reg,
                      "array base command", bases[i], offset);
                // The renderer resolves this as arena base + (value & 0x3fffffff).
                check(s_words.size() == 3 && bases[i] + (s_words[2] & 0x3fffffffu) == bases[i] + offset,
                      "array base renderer pointer round trip", bases[i], offset);
            }
        }
    }
    s_words.clear();
    J3DLoadArrayBasePtr(GX_VA_POS, nullptr);
    check(s_words.size() == 3 && s_words[2] == 0, "null array base", 0, 0);
    std::printf("%s J3D vertex array base commands: %d checks, %d failures\n",
                failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
