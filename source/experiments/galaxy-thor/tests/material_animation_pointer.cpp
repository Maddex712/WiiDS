#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include "port/port.h"
#include <cstdio>
#include <cstdint>

// Link-only material operations for this isolated getter test. The getter is
// compiled directly from the production header; no animation is dereferenced.
void J3DMaterial::initialize() { mMaterialAnm = nullptr; }
void J3DMaterial::calc(const f32 (*)[4]) {}
void J3DMaterial::calcDiffTexMtx(const f32 (*)[4]) {}
void J3DMaterial::makeDisplayList() {}
void J3DMaterial::makeSharedDisplayList() {}
void J3DMaterial::load() {}
void J3DMaterial::loadSharedDL() {}
void J3DMaterial::patch() {}
void J3DMaterial::diff(u32) {}
void J3DMaterial::reset() {}
void J3DMaterial::change() {}

int main() {
    J3DMaterial material;
    const uintptr_t addresses[] = {0, PORT_MEM1_BASE + 0x1000,
        PORT_MEM1_BASE + PORT_MEM1_SIZE - 0x1000, PORT_MEM2_BASE,
        PORT_MEM2_BASE + 0x123400, PORT_MEM2_BASE + PORT_MEM2_SIZE - 0x1000,
        PORT_MEM1_UNCACHED + 0x1000, PORT_MEM2_UNCACHED + 0x1000};
    unsigned failures = 0;
    for (uintptr_t address : addresses) {
        material.mMaterialAnm = reinterpret_cast<J3DMaterialAnm*>(address);
        if (material.getMaterialAnm() != material.mMaterialAnm) {
            ++failures;
            std::printf("Rejected material animation at %zx\n",address);
        }
    }
    std::printf("Production material-animation getter: %zu checks, %u failures\n",
                sizeof(addresses)/sizeof(addresses[0]),failures);
    return failures ? 1 : 0;
}
