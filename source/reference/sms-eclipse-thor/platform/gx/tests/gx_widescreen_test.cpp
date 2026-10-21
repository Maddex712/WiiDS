// Actual renderer placement checks for the widescreen fast paths, no ROM.
#include <sms_gx/gxvert_pc.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include "sms_gx/gx_pc.h"
#include "gx_internal.h"
#include "gl_funcs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void OSPanic(const char* file, int line, const char* msg, ...) {
    fprintf(stderr, "OSPanic %s:%d %s\n", file, line, msg); abort();
}
void DCFlushRange(void* p, u32 n) { GXPC_InvalidateRange(p, n); }
extern "C" void port_ptr32_trap(const void*) { abort(); }

static int failures = 0, checks = 0;
static float scale = 1;
static void pixel(const char* name, int x, int y, int r, int g, int b) {
    int w, h;
    GXPC_ReadEFB(nullptr, &w, &h);
    std::vector<uint8_t> data(size_t(w) * h * 4);
    GXPC_ReadEFB(data.data(), &w, &h);
    const uint8_t* p = data.data() + (size_t(int(y * scale)) * w + int(x * scale)) * 4;
    const bool ok = abs(int(p[0]) - r) <= 2 && abs(int(p[1]) - g) <= 2 && abs(int(p[2]) - b) <= 2;
    printf("%s %s (%d,%d): %d %d %d\n", ok ? "PASS" : "FAIL", name, x, y, p[0], p[1], p[2]);
    ++checks; if (!ok) ++failures;
}
static void projection(bool perspective = false) {
    Mtx44 p{};
    if (perspective) { p[0][0] = 1; p[1][1] = 1; p[2][2] = -0.1f; p[2][3] = -1; p[3][2] = -1; }
    else { p[0][0] = 2.0f / 640; p[0][3] = -1; p[1][1] = -2.0f / 480; p[1][3] = 1;
           p[2][2] = -0.1f; p[2][3] = -1; p[3][3] = 1; }
    GXSetProjection(p, perspective ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC);
    Mtx m = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    GXLoadPosMtxImm(m, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
}
static void setup(bool textured = false) {
    GXClearVtxDesc(); GXSetVtxDesc(GX_VA_POS, GX_DIRECT); GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    if (textured) {
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetNumTexGens(1); GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    } else GXSetNumTexGens(0);
    GXSetNumChans(1); GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, textured ? GX_TEXCOORD0 : GX_TEXCOORD_NULL, textured ? GX_TEXMAP0 : GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, textured ? GX_MODULATE : GX_PASSCLR);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE); GXSetCullMode(GX_CULL_NONE);
}
static void quad(float x0, float y0, float x1, float y1, u8 r, u8 g, u8 b, bool tex = false, float z = -1) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    const float x[] = {x0,x1,x1,x0}, y[] = {y0,y0,y1,y1};
    for (int i = 0; i < 4; ++i) {
        GXPosition3f32(x[i], y[i], z); GXColor4u8(r,g,b,255);
        if (tex) GXTexCoord2f32(i == 1 || i == 2, i >= 2);
    }
    GXEnd();
}
static void clear() {
    GXPC_SetHud(0); GXPC_SetStretch2D(0); projection(); setup();
    GXSetScissor(0,0,640,480); quad(0,0,640,480,0,0,0); GXFlush();
}

int main(int argc, char** argv) {
    if (argc > 1) scale = float(atof(argv[1]));
    setenv("SMS_GX_SYNC_READS", "1", 1); setenv("SMS_WIDESCREEN_HUD", "edges", 1);
    GXPC_SetWidescreen(4.0f/3); GXPC_SetHeadless(1); GXPC_SetAutoPresent(0);
    if (!GXPC_InitAuto(scale)) return 77;
    static u8 memory[8 << 20] __attribute__((aligned(32)));
    static u8 fifo[256 * 1024];
    GXPC_SetMemoryWindow(memory, sizeof memory); GXInit(fifo, sizeof fifo);
    GXSetViewport(0,0,640,480,0,1); GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
    int w,h; GXPC_ReadEFB(nullptr,&w,&h);
    if (w != int(854*scale) || h != int(528*scale)) return 1;
    clear();
    quad(0,0,640,480,0,255,0);
    pixel("full-width untextured reaches left edge",10,50,0,255,0);
    pixel("full-width untextured reaches right edge",840,50,0,255,0);

    // 32x32 RGBA8 solid-white tiled texture, recognized as artwork.
    memset(memory + 0x1000, 255, 32*32*4);
    GXTexObj texture; GXInitTexObj(&texture,memory+0x1000,32,32,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GXInitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    clear(); GXLoadTexObj(&texture,GX_TEXMAP0); setup(true);
    quad(0,0,640,480,255,0,0,true);
    pixel("full-width artwork stays centred",120,50,255,0,0);
    pixel("full-width artwork does not stretch left",20,50,0,0,0);
    pixel("full-width artwork does not stretch right",830,50,0,0,0);
    clear(); GXLoadTexObj(&texture,GX_TEXMAP0); setup(true);
    quad(40,40,80,80,255,0,0,true);
    pixel("partial artwork stays centred",160,60,255,0,0);
    pixel("partial artwork leaves original left position empty",60,60,0,0,0);

    clear(); GXLoadTexObj(&texture,GX_TEXMAP0); setup(true); GXPC_SetHud(1);
    GXPC_HudPaneBegin(0,100); quad(40,40,80,80,255,0,0,true); GXPC_HudPaneEnd();
    GXPC_HudPaneBegin(540,640); quad(550,40,590,80,0,0,255,true); GXPC_HudPaneEnd();
    GXPC_SetHud(0);
    pixel("left HUD artwork uses pane anchor",60,60,255,0,0);
    pixel("right HUD artwork uses pane anchor",780,60,0,0,255);
    pixel("HUD artwork leaves centre empty",170,60,0,0,0);

    clear(); projection(true); GXPC_SetHud(1);
    GXPC_HudPaneBegin(0,100); quad(-0.5f,-0.5f,0.5f,0.5f,255,0,0,false,-5); GXPC_HudPaneEnd();
    GXPC_HudPaneBegin(540,640);
    GXPC_HudPaneBegin(0,100); // outer pane controls its nested geometry
    quad(-0.5f,-0.5f,0.5f,0.5f,0,0,255,false,-5);
    GXPC_HudPaneEnd(); GXPC_HudPaneEnd(); GXPC_SetHud(0);
    pixel("3D HUD left anchor",320,240,255,0,0);
    pixel("3D HUD nested right anchor",534,240,0,0,255);
    pixel("3D HUD leaves centred location empty",427,240,0,0,0);

    clear(); projection(true); GXPC_SetHud(1);
    quad(-4.5f,-0.5f,-3.5f,0.5f,255,0,0,false,-5); GXFlush();
    quad(3.5f,-0.5f,4.5f,0.5f,0,0,255,false,-5); GXPC_SetHud(0);
    pixel("unanchored 3D HUD bounds find left edge",60,240,255,0,0);
    pixel("unanchored 3D HUD bounds find right edge",780,240,0,0,255);

    clear(); GXPC_SetStretch2D(1); quad(0,40,80,80,255,0,0); GXPC_SetStretch2D(0);
    pixel("wipe keeps explicit stretch mapping",60,60,255,0,0);
    pixel("wipe does not acquire centre offset",160,60,0,0,0);

    clear(); GXSetScissor(40,40,40,40); quad(0,0,640,480,0,255,0);
    pixel("widened scissor includes mapped interior",80,60,0,255,0);
    pixel("widened scissor excludes outside",150,60,0,0,0);

    clear();
    struct Packed { float p[3]; uint8_t rgba[4]; };
    const Packed left[] = {{{0,0,-1},{0,255,0,255}}, {{320,0,-1},{0,255,0,255}},
                           {{320,480,-1},{0,255,0,255}}, {{0,480,-1},{0,255,0,255}}};
    const Packed right[] = {{{320,0,-1},{0,0,255,255}}, {{640,0,-1},{0,0,255,255}},
                            {{640,480,-1},{0,0,255,255}}, {{320,480,-1},{0,0,255,255}}};
    const uint32_t indices[] = {0,1,2,0,2,3};
    gx::appendDecoded(gx::PRIM_TRIS,gx::VF_CLR0,sizeof(Packed),reinterpret_cast<const uint8_t*>(left),4,indices,6);
    gx::ArenaRef ref;
    if (!gx::arenaUpload(reinterpret_cast<const uint8_t*>(right),4,sizeof(Packed),indices,6,ref)) return 1;
    gx::appendArena(gx::PRIM_TRIS,gx::VF_CLR0,sizeof(Packed),ref,4,6,reinterpret_cast<const uint8_t*>(right));
    pixel("mixed stream/arena bounds stretch left half",10,60,0,255,0);
    pixel("mixed stream/arena bounds stretch right half",840,60,0,0,255);
    pixel("mixed stream/arena split stays left of centre",410,60,0,255,0);
    pixel("mixed stream/arena split stays right of centre",440,60,0,0,255);
    if (glGetError() != GL_NO_ERROR) ++failures;
    printf("Widescreen %.0fx: %d pixel checks, %d failures\n", scale, checks, failures);
    GXPC_Shutdown(); return failures ? 1 : 0;
}
