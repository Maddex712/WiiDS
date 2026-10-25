#include "gx_builder.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Exercise production GX state packing. Distant lights are points in view space,
// not direction vectors: rescaling them moves them across scene geometry.
int main() {
    gpu::Builder b{};
    const float positions[][3] = {{100000,27000,-5500},{-100000,-2000,-80000},
                                 {33642.578f,100000,37109.375f},{1e8f,2e8f,-3e8f},
                                 {0,0,0},{300,400,-500}};
    unsigned checks=0, failures=0;
    for (unsigned i=0;i<6;++i) {
        for (unsigned k=0;k<3;++k)b.xfMem[0x600+i*16+10+k]=positions[i][k];
    }
    gpu::LightState light{};b.fillLights(light);
    for (unsigned i=0;i<6;++i)for(unsigned k=0;k<3;++k) {
        ++checks;
        if(light.lights[i][3][k]!=positions[i][k])++failures;
    }
    // At a character 5,000 units in front of the camera, the key light is
    // behind its front-facing normal. The old 10,000-unit rescale put it ahead.
    const float* p=light.lights[0][3];
    float dz=p[2]+5000;
    float diffuse=std::fmax(0.f,dz/std::sqrt(p[0]*p[0]+p[1]*p[1]+dz*dz));
    ++checks;if(diffuse!=0.f)++failures;
    // Preserve the exact non-position state alongside the correction.
    ++checks;if(light.lights[0][3][3]!=0.f)++failures;
    std::printf("Light position production packing: %u checks, %u failures; front diffuse %.6f\n",checks,failures,diffuse);
    return failures?1:0;
}
