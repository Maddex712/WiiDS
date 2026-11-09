// Generates GLSL for the GX pixel pipeline (TEV) from BP state.
#include "shadergen.h"
#include "gl.h"   // GCN_GLSL_VERSION
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

namespace gx {

bool g_dual_source = false;

uint32_t dst_alpha_mode(const uint32_t* bp) {
    const uint32_t bm = bp[0x41];
    if (!((bp[0x42] >> 8) & 1) || !((bm >> 4) & 1) || (bp[0x43] & 7) != 1) return 0;
    const bool blend = (bm & 1) && !((bm >> 11) & 1);  // subtract is ONE, ONE
    const uint32_t sf = (bm >> 8) & 7, df = (bm >> 5) & 7;
    if (!blend || (sf != 4 && sf != 5 && df != 4 && df != 5)) return 1;
    return g_dual_source ? 2 : 0;
}

ShaderKey make_shader_key(const PixelState& st) {
    ShaderKey k;
    memset(&k, 0, sizeof(k));
    const uint32_t* bp = st.bp;
    k.genmode = bp[0x00] & ~0xC000u;  // without cull mode
    uint32_t nstages = ((bp[0x00] >> 10) & 15) + 1;
    uint32_t nind = (bp[0x00] >> 16) & 7;
    for (uint32_t i = 0; i < (nstages + 1) / 2; i++) k.order[i] = bp[0x28 + i];
    for (uint32_t i = 0; i < nstages; i++) {
        k.cenv[i] = bp[0xC0 + 2 * i];
        k.aenv[i] = bp[0xC1 + 2 * i];
        k.indcmd[i] = bp[0x10 + i];
    }
    for (int i = 0; i < 8; i++) k.ksel[i] = bp[0xF6 + i];
    k.alpha_func = (bp[0xF3] >> 16) & 0xFF;
    k.iref = nind ? bp[0x27] & ((1u << (6 * nind)) - 1) : 0;
    k.num_texgens = st.num_texgens;
    k.efb_has_alpha = (bp[0x43] & 7) == 1;
    if ((bp[0xF5] >> 2) & 3) k.ztex = bp[0xF5] & 0xF;
    k.dst_alpha = dst_alpha_mode(bp);
    uint32_t used = 0;
    for (uint32_t s = 0; s < nstages; s++) {
        uint32_t order = bp[0x28 + s / 2] >> ((s & 1) * 12);
        if (order & 0x40) used |= 1u << (order & 7);
    }
    for (uint32_t i = 0; i < nind; i++) used |= 1u << ((bp[0x27] >> (6 * i)) & 7);
    for (int m = 0; m < 8; m++) {
        if (!(used & (1u << m)) || !st.tex_is_efb[m]) continue;
        k.efb_tex_mask |= 1u << m;
        if (st.tex_pal[m]) k.efb_pal_mask |= 1u << (st.tex_pal_bits[m] == 8 ? 8 + m : m);
        k.efb_conv |= (uint32_t)efb_conv_for(st.tex_efb_fmt[m]) << (4 * m);
    }
    canonicalize_shader_key(k);
    return k;
}

// The registers keep whatever a game last wrote to the parts no stage reads: the konstant
// selectors and the order of stages past the last, swap tables nothing refers to, an
// indirect command's matrix and format on a stage with no indirect stage behind it. Keyed
// raw, a game that sets up only the stages it uses got a program for every leftover it
// carried -- ten for each one it needed: a cache of 23,673 programs held 2,193 distinct
// ones once these were cleared, and restoring it from binaries took 47 s on a Quest 3.
// So everything gen_pixel_shader does not read is cleared, and two keys that generate
// the same source are one key.
void canonicalize_shader_key(ShaderKey& k) {
    const uint32_t nstages = ((k.genmode >> 10) & 15) + 1;
    const uint32_t nind = (k.genmode >> 16) & 7;
    k.genmode &= (15u << 10) | (7u << 16) | (1u << 19);  // stages, indirect stages, z freeze
    // Of the konstant selectors only the swap tables (bits 0-3) are compiled in, and only
    // those a stage refers to. Which konstant each stage reads is a uniform (u_kstage): a
    // game sets it per material, often leaving it as it was on a stage that reads none,
    // and keyed, one object's program came in five variants that drew alike.
    uint32_t swaps = 0;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t order = 0;
        for (uint32_t h = 0; h < 2; h++) {
            const uint32_t st = 2 * i + h;
            if (st >= nstages) break;
            order |= k.order[i] & (0xFFFu << (12 * h));
            swaps |= 1u << (k.aenv[st] & 3) | 1u << ((k.aenv[st] >> 2) & 3);
        }
        k.order[i] = order;
    }
    for (uint32_t i = 0; i < 8; i++)  // swap table t is ksel[2t] and ksel[2t+1]
        k.ksel[i] = (swaps >> (i / 2)) & 1 ? k.ksel[i] & 15 : 0;
    for (uint32_t st = nstages; st < 16; st++) k.cenv[st] = k.aenv[st] = k.indcmd[st] = 0;
    for (uint32_t st = 0; st < nstages; st++) {
        // The wraps and "add previous" always apply; the rest only through an indirect stage.
        const uint32_t ind = k.indcmd[st];
        const uint32_t bt = ind & 3, bs = (ind >> 7) & 3, mid = (ind >> 9) & 15;
        uint32_t keep = (7u << 13) | (7u << 16) | (1u << 20);
        if (bt < nind && (mid != 0 || bs != 0)) keep |= 0x1FFF;
        k.indcmd[st] = ind & keep;
    }
    // An alpha test that passes whatever the alpha is -- always AND always, always OR
    // anything, and so on -- is no test (0x3F, which gen_pixel_shader leaves out).
    {
        const uint32_t f0 = k.alpha_func & 7, f1 = (k.alpha_func >> 3) & 7, logic = (k.alpha_func >> 6) & 3;
        const int c0 = f0 == 7 ? 1 : f0 == 0 ? 0 : -1, c1 = f1 == 7 ? 1 : f1 == 0 ? 0 : -1;  // -1: depends
        bool pass = false;
        if (logic == 0) pass = c0 == 1 && c1 == 1;
        else if (logic == 1) pass = c0 == 1 || c1 == 1;
        else if (c0 >= 0 && c1 >= 0) pass = logic == 2 ? c0 != c1 : c0 == c1;
        if (pass) k.alpha_func = 0x3F;
    }
    // Fog is a uniform (u_fogmode): whether there is any, its curve and its projection. A
    // game changes them with the scene, and keyed, every program on screen was built again
    // when the fog came or went or changed its curve, with nothing else drawn differently.
    k.fog = 0;
}

EfbConv efb_conv_for(uint32_t copy_fmt) {
    if (copy_fmt & 0x10) {
        switch (copy_fmt & 15) {
        case 0: return EfbConvI4;
        case 1: return EfbConvI8;
        case 2: case 3: return EfbConvIA;
        default: return EfbConvNone;
        }
    }
    // The same as kCopyFS takes them without the intensity bit. A game that draws ids into
    // the frame's alpha and copies the whole frame as A8 to colour what it marked, through a
    // palette, would otherwise have the eye's colour indexing the palette.
    switch (copy_fmt & 15) {
    case 0: return EfbConvR4;
    case 1: case 8: return EfbConvR8;
    case 4: return EfbConvOpaque;
    case 7: return EfbConvA8;
    case 9: return EfbConvG8;
    case 10: return EfbConvB8;
    case 11: return EfbConvRG8;
    case 12: return EfbConvGB8;
    default: return EfbConvNone;
    }
}

static const char* kColorIn[16] = {
    "prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa", "c1.rgb", "c1.aaa", "c2.rgb", "c2.aaa",
    "tex.rgb", "tex.aaa", "ras.rgb", "ras.aaa", "ivec3(255)", "ivec3(128)", "konst.rgb", "ivec3(0)"};
static const char* kAlphaIn[8] = {"prev.a", "c0.a", "c1.a", "c2.a", "tex.a", "ras.a", "konst.a", "0"};
static const char* kDest[4] = {"prev", "c0", "c1", "c2"};

void stage_konsts(const uint32_t* bp, const int konst[16], int out[64]) {
    // kcsel and kasel: 0-7 a fixed fraction, 12-15 a konstant register's colour (kcsel
    // only), 16 and up one channel of a register, (sel - 16) & 3 which register and
    // (sel - 16) >> 2 which channel. What is left is reserved, and reads as 255.
    static const int frac[8] = {255, 223, 191, 159, 128, 96, 64, 32};
    for (uint32_t st = 0; st < 16; st++) {
        const uint32_t ks = bp[0xF6 + st / 2] >> (10 * (st & 1));
        const uint32_t kc = (ks >> 4) & 31, ka = (ks >> 9) & 31;
        int* o = out + 4 * st;
        if (kc < 8) o[0] = o[1] = o[2] = frac[kc];
        else if (kc >= 12 && kc <= 15) for (int c = 0; c < 3; c++) o[c] = konst[4 * (kc - 12) + c];
        else if (kc >= 16) o[0] = o[1] = o[2] = konst[4 * ((kc - 16) & 3) + ((kc - 16) >> 2)];
        else o[0] = o[1] = o[2] = 255;
        if (ka < 8) o[3] = frac[ka];
        else if (ka >= 16) o[3] = konst[4 * ((ka - 16) & 3) + ((ka - 16) >> 2)];
        else o[3] = 255;
    }
}

static std::string swizzle(const ShaderKey& k, uint32_t table) {
    // Swap table n lives in ksel[2n] (r,g) and ksel[2n+1] (b,a).
    static const char* comp = "rgba";
    uint32_t a = k.ksel[table * 2], b = k.ksel[table * 2 + 1];
    std::string s;
    s += comp[a & 3];
    s += comp[(a >> 2) & 3];
    s += comp[b & 3];
    s += comp[(b >> 2) & 3];
    return s;
}

static const char* kCompare[8] = {"false", "(%s < %s)", "(%s == %s)", "(%s <= %s)", "(%s > %s)", "(%s != %s)", "(%s >= %s)", "true"};

// The morph between theater and stereo crops the scene to a window that opens out of the
// game's own frustum, which is what clip distances are for. Desktop GL has them in core; ES
// needs an extension, and its directive has to come straight after #version, ahead of the
// precision statements GCN_GLSL_VERSION carries. Without it the morph runs uncropped rather
// than not at all.
std::string glsl_header_with_clip() {
    std::string s = GCN_GLSL_VERSION;
#ifdef GCN_GL_ES
    s.insert(s.find('\n') + 1,
             "#ifdef GL_EXT_clip_cull_distance\n"
             "#extension GL_EXT_clip_cull_distance : enable\n"
             "#define GCN_CLIP 1\n"
             "#endif\n");
#else
    s += "#define GCN_CLIP 1\n";
#endif
    return s;
}

std::string gen_vertex_shader() {
    std::string s = glsl_header_with_clip();
    s += R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_col0;
layout(location = 2) in vec4 a_col1;
)";
    // GLSL ES does not allow arrays of vertex inputs ("cannot declare arrays of this
    // qualifier"), though desktop GLSL does, so declare the eight texture coordinate
    // sets individually. Locations 3..10 match the attribute setup in render_init().
    char buf[96];
    for (int i = 0; i < 8; i++) {
        snprintf(buf, sizeof(buf), "layout(location = %d) in vec3 a_tex%d;\n", 3 + i, i);
        s += buf;
    }
    s += R"(uniform mat4 u_proj;
uniform vec4 u_vp_a;   // x: 2*(offx-xoff)/w - 1, y: 2*sx/w, z: 2*(offy-yoff)/h - 1, w: 2*sy/h (scissor offset)
uniform vec4 u_vp_b;   // x: 2*offz/16777215 - 1, y: 2*sz/16777215
uniform float u_point_size;
// VR. a_pos arrives in the game's view space, so an eye is just another transform in
// front of the projection -- which is why both eyes can share one vertex buffer and
// the CPU-side transform in xf.cpp runs once, not twice.
//   0 = flat, the game's own projection and GX viewport transform
//   1 = world geometry through the eye projection
//   2 = a 2D element, painted on the HUD frame out in front of the game's camera
//   3 = part way between theater and stereo: u_proj is the whole morphed chain, folded
//       on the CPU, and u_crop cuts the scene to the window opening out of the game's
//       frustum. See render_set_vr_morph.
//   4 = as 1, but the depth written within the draw's band is the game's own: a
//       foreground layer (render_set_depth_layers), where the band is too thin for the
//       eye's.
//   6 = a 2D element over the whole picture on the HUD frame (EyeFrameScreen): u_crop is
//       the game's own projection; u_view takes the game's clip space onto the frame and
//       into the eye, for a vertex on the picture's edge, taken 40 times out along it; and
//       u_proj does the same for the others, onto the frame as a facing panel turns it
//       (EyeFacing) when there is one. u_uvgrad[i] is how texture coordinate i changes with
//       the game's clip x and y over the draw (ds/dx, ds/dy, dt/dx, dt/dy), to carry it out
//       with the vertex.
uniform int u_vr;
uniform vec4 u_uvgrad[8];
uniform mat4 u_view;       // eye transform, relative to the game's camera; under 3, fog's
uniform mat4 u_crop;       // four clip distances, each linear in the vertex
// The game's own projection reduced to what depth needs, as a function of view-space z:
// clip z = .x * z + .y and clip w = .z * z + .w, so the last pair is (-1, 0) for a
// frustum and (0, 1) for an ortho batch. Fog needs the depth GX would have written
// whatever matrix the vertex is actually drawn with; see v_fogz.
uniform vec4 u_zproj;
out vec4 v_col0;
out vec4 v_col1;
out vec3 v_tex[8];
// The game's screen z as a numerator and the w to divide it by. Both are affine in the
// vertex position, so perspective-correct interpolation delivers them exactly and the
// ratio is the real value at the fragment -- which a single interpolated z/w would not be.
out vec2 v_fogz;
// The game's clip z and w at the vertex, for a foreground layer's depth (u_vr 4), which is
// worked out from their ratio per fragment for the same reason.
out vec2 v_gdepth;
void main() {
)";
    // Fog is a curve over the 24-bit depth GX writes, and in an eye gl_FragCoord.z is
    // not that: it comes from the headset's own frustum, whose near and far are nothing
    // like the game's. Fed to the curve it saturates a few metres out, so everything
    // beyond came back the fog colour -- which is what turned the sea bed black in
    // stereo while the flat view was fine. So the game's own screen z is rebuilt here
    // from its projection, independently of the matrix the vertex is drawn with.
    //
    // The depth fed in is this eye's, not the game camera's. Fog is a distance cue, and
    // the distance that matters to a viewer who has turned their head is the one along
    // their own line of sight: with the game's z, something off to the side fogs as
    // though it were as near as its forward depth, and stops fogging at all when it
    // leaves the game's 60-degree frustum. GCN_EYE_FOGZ=0 takes the game's depth instead,
    // which fogs exactly as the flat view does whatever the head is doing.
    static const bool eye_fog_z = !(getenv("GCN_EYE_FOGZ") && atoi(getenv("GCN_EYE_FOGZ")) == 0);
    s += "    vec2 gx_clip = vec2(1.0);\n";
    s += "    vec2 push_d = vec2(0.0);\n";
    s += "    v_gdepth = vec2(-1.0, 1.0);\n";
    s += eye_fog_z ? "    float fz = (u_vr == 1 || u_vr == 3 || u_vr == 4) ? (u_view * vec4(a_pos, 1.0)).z : a_pos.z;\n"
                   : "    float fz = a_pos.z;\n";
    s += R"(    float gz = u_zproj.x * fz + u_zproj.y;
    float gw = u_zproj.z * fz + u_zproj.w;
    v_fogz = vec2((0.5 * u_vp_b.x + 0.5) * gw + 0.5 * u_vp_b.y * gz, gw);
    if (u_vr == 1 || u_vr == 4) {
        // Straight to clip space: the eye's render target is the whole viewport, so
        // the GX viewport transform does not apply. No Y negation either -- that
        // exists only to cancel the flip the EFB blit does, and nothing blits here.
        gl_Position = u_proj * (u_view * vec4(a_pos, 1.0));
        // Except for its depth range. A game can confine a draw to a band of the depth
        // buffer through the viewport's z -- sky at the back, world in the middle, a
        // weapon or a HUD in front -- so that the layers never cross whatever their
        // geometry's real distances. The eye's own depth is mapped into the same band,
        // which keeps that order and the eye's depth within each layer. A full-range
        // viewport maps to itself. u_vp_b is (2*hi - 1, 2*(hi - lo)) for the band [lo, hi].
        gl_Position.z = (u_vp_b.x - 0.5 * u_vp_b.y) * gl_Position.w + 0.5 * u_vp_b.y * gl_Position.z;
        // A foreground layer's band can be a few hundredths of a percent of the buffer,
        // and the eye's near plane is a few centimetres: squeezed into the band, its depth
        // cannot tell apart surfaces closer than a large fraction of a unit at the
        // distances a game models a HUD at, and they fight. The game's own projection was
        // made for that band, so the depth it would have written goes in instead, from the
        // camera's position: what the flat view writes.
        // The vertex's depth decides only clipping where the fragment shader works the depth
        // out per fragment from v_gdepth (ShaderKey::fg_depth).
        if (u_vr == 4) {
            float cz = u_zproj.x * a_pos.z + u_zproj.y;
            float cw = u_zproj.z * a_pos.z + u_zproj.w;
            v_gdepth = vec2(cz, cw);
            gl_Position.z = (u_vp_b.x + u_vp_b.y * cz / cw) * gl_Position.w;
        }
    } else if (u_vr == 2) {
        // A 2D element in an eye. u_proj here is not the game's projection alone but the
        // whole chain folded on the CPU: that projection, the frame the overlay is
        // painted on, and the eye. GX's viewport transform is left out on purpose -- it
        // places the frame in the EFB, which an eye does not render into. Every term is
        // constant for the draw, so there is nothing left to do per vertex.
        gl_Position = u_proj * vec4(a_pos, 1.0);
    } else if (u_vr == 6) {
        // A 2D element over the whole picture whose inside lines up with the other 2D
        // elements -- a hole cut for a window -- and whose edges must not show: on the
        // HUD frame, but a vertex on the picture's edge goes far out along the frame's
        // plane, past the eye's field, so the draw covers the view the frame leaves.
        // Each axis on its own: a vertex at the picture's left edge and the window's top
        // goes out to the left and stays at the window's top. Taken out in both, the
        // bands above and below a window went out of sight, and those beside it were
        // stretched into slanting wedges.
        // The texture coordinates go out with it, by how they change over the draw: a
        // full-screen quad has every corner on the edge, and taken out without its
        // coordinates its texture was stretched as far -- a filter's scan lines, forty
        // times as wide, were gone.
        // The inside -- a hole for a window, a tint over it -- goes where the window does,
        // turned with it when it faces the viewer; the edges stay on the frame's plane, which
        // the eye faces, so that taken far out they stay in front of it.
        vec4 g = u_crop * vec4(a_pos, 1.0);
        vec2 g0 = g.xy;
        bool edge = false;
        if (abs(g.x) >= 0.98 * g.w) { g.x *= 40.0; edge = true; }
        if (abs(g.y) >= 0.98 * g.w) { g.y *= 40.0; edge = true; }
        push_d = (g.xy - g0) / g.w;
        gl_Position = (edge ? u_view : u_proj) * g;
    } else if (u_vr == 3) {
        // Part way between theater and stereo (morph_chain). The depth bands are kept as
        // the eye keeps them (u_vr 1, above): without them a sky modelled a few tens of
        // units out had its modelled depth for the whole transition and hid the world
        // behind it, which reappeared only as the morph finished.
        gl_Position = u_proj * vec4(a_pos, 1.0);
        gl_Position.z = (u_vp_b.x - 0.5 * u_vp_b.y) * gl_Position.w + 0.5 * u_vp_b.y * gl_Position.z;
    } else if (u_vr == 5) {
        // A 3D draw composed into the 2D picture, on the HUD frame like a 2D element (the
        // chain on the CPU includes its viewport), but with the depth the game's own
        // projection gives it, so that its parts hide one another as they do on the
        // screen. GX's clip depth runs from -w to 0.
        gl_Position = u_proj * vec4(a_pos, 1.0);
        float cz = u_zproj.x * a_pos.z + u_zproj.y;
        float cw = u_zproj.z * a_pos.z + u_zproj.w;
        gl_Position.z = (2.0 * cz / cw + 1.0) * gl_Position.w;
    } else {
        vec4 clip = u_proj * vec4(a_pos, 1.0);
        vec4 p;
        p.x = u_vp_a.x * clip.w + u_vp_a.y * clip.x;
        p.y = u_vp_a.z * clip.w + u_vp_a.w * clip.y;
        p.z = u_vp_b.x * clip.w + u_vp_b.y * clip.z;
        p.w = clip.w;
        // GX's viewport transform points Y down and the EFB blit flips the image back,
        // so the flat path negates Y here to cancel it.
        gl_Position = vec4(p.x, -p.y, p.z, p.w);
        // GX clips depth before the viewport, at -w <= z <= 0, and clamps the depth the
        // viewport makes of what is left to the buffer's range. A viewport may reach past
        // that range (a far of 10 instead of 1 squeezes the scene's depth into the front
        // tenth and leaves everything farther at the buffer's maximum): GL, clipping after
        // the viewport, threw away all of the scene beyond that tenth. So the renderer
        // clamps depth (GL_DEPTH_CLAMP, which also turns off GL's own near and far
        // clipping) and clips here where GX does.
        gx_clip = vec2(clip.z + clip.w, -clip.z);
    }
#ifdef GCN_CLIP
    // Where the renderer clamps depth, GL no longer clips at near and far, so every other
    // path clips at GL's own planes.
    if (u_vr != 0) gx_clip = vec2(gl_Position.z + gl_Position.w, gl_Position.w - gl_Position.z);
    gl_ClipDistance[4] = gx_clip.x;
    gl_ClipDistance[5] = gx_clip.y;
    // Ignored unless enabled, which the morph does, and an eye with a 3D draw composed into
    // its HUD (u_vr 5: its own clip space, which the eye's target has no viewport to bound).
    vec4 cd = (u_vr == 3 || u_vr == 5) ? u_crop * vec4(a_pos, 1.0) : vec4(1.0);
    gl_ClipDistance[0] = cd.x;
    gl_ClipDistance[1] = cd.y;
    gl_ClipDistance[2] = cd.z;
    gl_ClipDistance[3] = cd.w;
#endif
    gl_PointSize = u_point_size;
    v_col0 = a_col0;
    v_col1 = a_col1;
)";
    for (int i = 0; i < 8; i++) {
        snprintf(buf, sizeof(buf),
                 "    v_tex[%d] = a_tex%d + vec3(dot(u_uvgrad[%d].xy, push_d), dot(u_uvgrad[%d].zw, push_d), 0.0);\n",
                 i, i, i, i);
        s += buf;
    }
    s += "}\n";
    return s;
}

std::string gen_pixel_shader(const ShaderKey& k) {
    static const int tev_stop = getenv("GCN_TEV_STOP") ? atoi(getenv("GCN_TEV_STOP")) : -1;
    // GCN_TEV_STOP_STAGES=<n> confines the stop to programs of exactly n stages, so a
    // draw can be probed without every later draw of at least as many stages painting
    // its own probe over it.
    static const int tev_stop_stages = getenv("GCN_TEV_STOP_STAGES") ? atoi(getenv("GCN_TEV_STOP_STAGES")) : 0;
    static const int tev_reg = getenv("GCN_TEV_REG") ? atoi(getenv("GCN_TEV_REG")) : 0;
    static const int tev_alpha = getenv("GCN_TEV_ALPHA") ? atoi(getenv("GCN_TEV_ALPHA")) : 0;
    static const bool no_alpha_test = getenv("GCN_NO_ALPHA_TEST") != nullptr;
    static const bool no_fog = getenv("GCN_NO_FOG") != nullptr;
    static const bool ras_white = getenv("GCN_RAS_WHITE") != nullptr;
    // GCN_TEX_WHITE=1 makes every texture sample opaque white, which leaves a draw's
    // colour to its channels and konstants alone. The cache keys programs by their
    // source, so the variant is kept apart from the real one.
    static const bool tex_white = getenv("GCN_TEX_WHITE") != nullptr;
    static const int snap_stage = getenv("GCN_SNAP") ? atoi(getenv("GCN_SNAP")) : -1;
    std::string s;
    char buf[1024];
    // The zero-argument case appends directly: handing a runtime format string to
    // snprintf with no arguments trips -Wformat-security, and it is wasted work anyway.
    auto W = [&](const char* fmt, auto... args) {
        if constexpr (sizeof...(args) == 0) {
            s += fmt;
        } else {
            snprintf(buf, sizeof(buf), fmt, args...);
            s += buf;
        }
    };
    uint32_t nstages = ((k.genmode >> 10) & 15) + 1;
    uint32_t nind = (k.genmode >> 16) & 7;

    s += GCN_GLSL_VERSION;
    s += "in vec4 v_col0;\nin vec4 v_col1;\nin vec3 v_tex[8];\n";
    s += "in vec2 v_fogz;\n";
    if (k.fg_depth) s += "in vec2 v_gdepth;\nuniform int u_vr;\nuniform vec4 u_vp_b;\n";
    s += "uniform sampler2D u_tex[8];\n";
    // The palettes EFB copies are read through (efb_pal_mask), on units 8-15.
    if (k.efb_pal_mask) s += "uniform sampler2D u_pal[8];\n";
    s += "uniform ivec4 u_reg[4];\n";
    s += "uniform ivec4 u_kstage[16];\n";  // the konstant each stage selects (kcsel, kasel)
    s += "uniform vec2 u_texsize[8];\n";
    s += "uniform ivec3 u_indmtx[6];\n";  // 3 matrices x 2 rows (11-bit signed entries)
    s += "uniform int u_indscale[3];\n";
    s += "uniform ivec2 u_alpharef;\n";
    s += "uniform vec4 u_fog;\n";       // A, C, bmag, bshift
    s += "uniform int u_fogmode;\n";    // GXSetFog's type: fsel, and 8 for an orthographic projection
    s += "uniform vec3 u_fogcolor;\n";
    if (k.ztex) s += "uniform int u_zbias;\n";  // bp[0xF4]
    const bool zfreeze = (k.genmode >> 19) & 1;
    if (zfreeze) s += "uniform vec4 u_zplane;\n";  // see the z freeze below
    s += "layout(location = 0) out vec4 o_color;\n";
    if (k.dst_alpha) s += "uniform float u_dstalpha;\n";
    if (k.dst_alpha == 2) s += "layout(location = 0, index = 1) out vec4 o_src1;\n";
    s += "uniform vec2 u_indcoordscale[4];\n";
    s += "vec2 u_indscalef(int i) { return u_indcoordscale[i]; }\n";
    // The maps sampled nearest whose coordinate is snapped to the centre of the texel GX
    // would read (the floor of the coordinate in texels), set for EFB copies of depth.
    // See apply_state().
    // A point's texture offsets (see line_point_tex_offset in xf.cpp): the range each
    // coordinate runs across the point, for a draw of points; u_points is 0 for any other,
    // where gl_PointCoord means nothing.
    s += "uniform int u_points;\nuniform float u_point_off[8];\n";
    s += "uniform int u_texsnap;\n";
    s += "#define snap_tex(m, t) (((u_texsnap >> (m)) & 1) != 0 ? floor(t) + 0.5 : (t))\n";
    // A macro, not a function: a sampler array has to be indexed by a constant
    // expression in GLSL ES (and strictly in GLSL 330 as well). Every call site
    // passes a literal map index, so expansion makes the index constant.
    s += "uniform int u_screen_uv;\nuniform vec2 u_screen_px;\n";
    // Converts an indirect offset from the copy's own texels to the eye grab's, for
    // the stages whose coordinate an eye takes over. (1,1) everywhere else, so the
    // flat path is unaffected. See apply_state().
    s += "uniform vec2 u_screen_ripple;\n";
    s += "#define sample_tex(m, uv) ivec4(round(texture(u_tex[m], (uv)) * 255.0))\n";
    s += "void main() {\n";
    s += "  ivec4 prev = u_reg[0], c0 = u_reg[1], c1 = u_reg[2], c2 = u_reg[3];\n";
    s += "  ivec4 col0 = ivec4(round(v_col0 * 255.0)), col1 = ivec4(round(v_col1 * 255.0));\n";
    s += "  ivec4 tex = ivec4(0), ras = ivec4(0), konst = ivec4(0);\n";
    s += "  int alphabump = 0;\n";
    s += "  ivec4 snap = ivec4(0);\n";  // GCN_SNAP target; unused unless GCN_SHOW=snap
    s += "  vec2 tc_prev = vec2(0.0);\n";

    // Which texgens feed a stage that reads an EFB copy, and so may have their
    // coordinate taken over in an eye. Collected up front because the substituted
    // coordinate is declared with the texgens but only read down in the stages.
    uint32_t subst_coords = 0;
    for (uint32_t st = 0; st < nstages; st++) {
        uint32_t order = k.order[st / 2] >> ((st & 1) * 12);
        if ((k.efb_tex_mask >> (order & 7)) & 1) subst_coords |= 1u << ((order >> 3) & 7);
    }
    // texcoords (projective divide)
    for (uint32_t i = 0; i < 8; i++) {
        if (i < k.num_texgens)
            W("  vec2 uv%u = (v_tex[%u].xy + (u_points != 0 ? u_point_off[%u] * (gl_PointCoord - 0.5) * v_tex[%u].z : vec2(0.0)))"
              " / (v_tex[%u].z == 0.0 ? 1.0 : v_tex[%u].z);\n", i, i, i, i, i, i);
        else W("  vec2 uv%u = vec2(0.0);\n", i);
        // A texgen that addresses a copy of the whole frame, or one of the spray's
        // grabs, is a screen-space lookup, and in an eye the screen is this eye's.
        // The substituted coordinate is kept beside the game's rather than replacing
        // it, because an indirect stage sampling an ordinary bump map through the
        // same texgen still wants the game's -- addressing that map by the fragment's
        // position stretches a per-droplet ripple across the whole eye. u_screen_uv is
        // 0 everywhere else, so the flat path takes the game's coordinate either way.
        if ((subst_coords >> i) & 1)
            W("  vec2 suv%u = ((u_screen_uv & %u) != 0) ? gl_FragCoord.xy * u_screen_px : uv%u;\n",
              i, 1u << i, i);
    }
    // indirect stages
    for (uint32_t i = 0; i < nind; i++) {
        uint32_t map = (k.iref >> (6 * i)) & 7, coord = (k.iref >> (6 * i + 3)) & 7;
        W("  ivec4 indtex%u = sample_tex(%u, snap_tex(%u, uv%u * u_indscalef(%u) * u_texsize[%u]) / u_texsize[%u]);\n", i, map,
          map, coord, i, map, map);
    }

    for (uint32_t st = 0; st < nstages; st++) {
        uint32_t order = k.order[st / 2] >> ((st & 1) * 12);
        uint32_t map = order & 7, coord = (order >> 3) & 7;
        bool tex_en = (order >> 6) & 1;
        uint32_t chan = (order >> 7) & 7;
        uint32_t cenv = k.cenv[st], aenv = k.aenv[st];
        uint32_t ind = k.indcmd[st];
        W("  // stage %u\n  {\n", st);
        // A stage that reads an EFB copy is one whose coordinate an eye may take
        // over, so the parts of the lookup that only mean something in the copy's
        // own space are made conditional on it. Every other shader is generated
        // exactly as before, which keeps the flat path byte for byte unchanged.
        const bool subst = (k.efb_tex_mask >> map) & 1;
        const uint32_t cbit = 1u << coord;
        // ---- indirect texture offset ----
        W("    vec2 tc = %s%u * u_texsize[%u];\n", subst ? "suv" : "uv", coord, map);
        uint32_t bt = ind & 3, fmt = (ind >> 2) & 3, bias = (ind >> 4) & 7, bs = (ind >> 7) & 3, mid = (ind >> 9) & 15;
        uint32_t sw = (ind >> 13) & 7, tw = (ind >> 16) & 7;
        bool fb = (ind >> 20) & 1;
        if (bt < nind && (mid != 0 || bs != 0)) {
            static const int mask[4] = {255, 31, 15, 7};
            W("    ivec3 icrd = indtex%u.abg & %d;\n", bt, mask[fmt]);
            if (bs) {
                static const char* bsc[4] = {"", "a", "b", "g"};
                static const int bmask[4] = {0xF8, 0xE0, 0xF0, 0xF8};
                W("    alphabump = indtex%u.%s & %d;\n", bt, bsc[bs], bmask[fmt]);
            }
            int b = fmt == 0 ? -128 : 1;
            if (bias & 1) W("    icrd.x += %d;\n", b);
            if (bias & 2) W("    icrd.y += %d;\n", b);
            if (bias & 4) W("    icrd.z += %d;\n", b);
            if (mid >= 1 && mid <= 3) {
                uint32_t m = mid - 1;
                W("    vec2 ioff = vec2(dot(vec3(u_indmtx[%u]), vec3(icrd)), dot(vec3(u_indmtx[%u]), vec3(icrd))) * exp2(float(u_indscale[%u])) / 1024.0;\n",
                  m * 2, m * 2 + 1, m);
            } else if (mid >= 5 && mid <= 7) {
                // Dynamic matrices: the stage's own coordinate scaled by one component of
                // the indirect sample -- [s 0 0; t 0 0] for S, [0 s 0; 0 t 0] for T.
                W("    vec2 ioff = tc * float(icrd.x) / 256.0 * exp2(float(u_indscale[%u]));\n", mid - 5);
            } else if (mid >= 9 && mid <= 11) {
                W("    vec2 ioff = tc * float(icrd.y) / 256.0 * exp2(float(u_indscale[%u]));\n", mid - 9);
            } else {
                W("    vec2 ioff = vec2(0.0);\n");
            }
        } else {
            W("    vec2 ioff = vec2(0.0);\n");
        }
        // An offset from a static indirect matrix is an absolute displacement in the
        // copy's texels. Substituted, those texels are the eye grab's and each covers
        // a different angle, so it is converted. The dynamic matrices (5..11) scale
        // the stage's own coordinate instead, so their offset is already relative to
        // whatever space the coordinate is in and needs no conversion.
        if (subst && bt < nind && mid >= 1 && mid <= 3)
            W("    if ((u_screen_uv & %u) != 0) ioff *= u_screen_ripple;\n", cbit);
        // A wrap folds the coordinate into the copy's own size. Substituted, the
        // coordinate is the fragment's position on the whole eye target, and folding
        // that tiles the eye into 32-pixel squares instead of addressing it.
        static const float wrapsz[8] = {0, 256, 128, 64, 32, 16, 0.001f, 0};
        const char* wi = "";
        if (subst && (sw || tw)) {
            W("    if ((u_screen_uv & %u) == 0) {\n", cbit);
            wi = "  ";
        }
        if (sw == 6) W("    %stc.x = 0.0;\n", wi);
        else if (sw) W("    %stc.x = mod(tc.x, %.1f);\n", wi, wrapsz[sw]);
        if (tw == 6) W("    %stc.y = 0.0;\n", wi);
        else if (tw) W("    %stc.y = mod(tc.y, %.1f);\n", wi, wrapsz[tw]);
        if (*wi) W("    }\n");
        W("    tc += ioff;\n");
        // "Add previous" carries the previous stage's whole coordinate, not just its
        // offset: a stage can zero its own coordinate with a wrap of 0 and look up at
        // the stage before's plus a bump, which is how the racer's reflection is warped.
        if (fb) W("    tc += tc_prev;\n");
        W("    tc_prev = tc;\n");
        // ---- texture ----
        // An EFB copy the game reads through a palette, or one an eye may replace with
        // its own grab when the copy was converted from the frame's colour: the texel is
        // worked on as a float before it becomes the TEV's integer texel. The grab is the
        // eye's colour, so it is converted as the copy was (kCopyFS in render_gl.cpp,
        // with the same 4-bit truncation, which the palette's index depends on), and
        // then, like the copy, it indexes the palette by its intensity.
        const uint32_t pal_bits = ((k.efb_pal_mask >> (8 + map)) & 1) ? 8 : ((k.efb_pal_mask >> map) & 1) ? 4 : 0;
        const uint32_t conv = subst ? (k.efb_conv >> (4 * map)) & 15 : EfbConvNone;
        if (tex_en && !tex_white && (pal_bits || conv != EfbConvNone)) {
            uint32_t tswap = (aenv >> 2) & 3;
            W("    vec4 tf = texture(u_tex[%u], snap_tex(%u, tc) / u_texsize[%u]);\n", map, map, map);
            if (conv != EfbConvNone) {
                W("    if ((u_screen_uv & %u) != 0) {\n", cbit);
                if (conv == EfbConvI4 || conv == EfbConvI8 || conv == EfbConvIA)
                    W("      float y = clamp(dot(tf.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0, 0.0, 1.0);\n");
                if (conv == EfbConvI4) W("      tf = vec4(floor(floor(y * 255.0 + 0.5) / 16.0) / 15.0);\n");
                else if (conv == EfbConvI8) W("      tf = vec4(y);\n");
                else if (conv == EfbConvIA) W("      tf = vec4(vec3(y), tf.a);\n");
                else if (conv == EfbConvA8) W("      tf = vec4(tf.a);\n");
                else if (conv == EfbConvR4) W("      tf = vec4(floor(tf.r * 15.0 + 0.5) / 15.0);\n");
                else if (conv == EfbConvR8) W("      tf = vec4(tf.r);\n");
                else if (conv == EfbConvG8) W("      tf = vec4(tf.g);\n");
                else if (conv == EfbConvB8) W("      tf = vec4(tf.b);\n");
                else if (conv == EfbConvRG8) W("      tf = vec4(tf.g, tf.g, tf.g, tf.r);\n");
                else if (conv == EfbConvGB8) W("      tf = vec4(tf.b, tf.b, tf.b, tf.g);\n");
                else if (conv == EfbConvOpaque) W("      tf.a = 1.0;\n");
                W("    }\n");
            }
            if (pal_bits)
                W("    tf = texelFetch(u_pal[%u], ivec2(int(round(tf.r * %.1f)), 0), 0);\n", map,
                  pal_bits == 8 ? 255.0 : 15.0);
            W("    tex = ivec4(round(tf * 255.0)).%s;\n", swizzle(k, tswap).c_str());
        } else if (tex_en && !tex_white) {
            uint32_t tswap = (aenv >> 2) & 3;
            W("    tex = sample_tex(%u, snap_tex(%u, tc) / u_texsize[%u]).%s;\n", map, map, map, swizzle(k, tswap).c_str());
        } else {
            W("    tex = ivec4(255);\n");
        }
        // ---- rasterized color ----
        {
            uint32_t rswap = aenv & 3;
            std::string sw_ = swizzle(k, rswap);
            // GCN_RAS_WHITE=1 forces the rasterized colour opaque white, to test whether
            // a bad image comes from the CPU-side vertex colour/lighting in xf.cpp
            // rather than from the TEV program itself.
            if (ras_white) W("    ras = ivec4(255);\n");
            else switch (chan) {
            case 0: W("    ras = col0.%s;\n", sw_.c_str()); break;
            case 1: W("    ras = col1.%s;\n", sw_.c_str()); break;
            case 5: W("    ras = ivec4(alphabump);\n"); break;
            case 6: W("    ras = ivec4(alphabump | (alphabump >> 5));\n"); break;
            default: W("    ras = ivec4(0);\n"); break;
            }
        }
        W("    konst = u_kstage[%u];\n", st);
        // ---- color combiner ----
        {
            uint32_t d = cenv & 15, c = (cenv >> 4) & 15, b = (cenv >> 8) & 15, a = (cenv >> 12) & 15;
            uint32_t bias_ = (cenv >> 16) & 3, op = (cenv >> 18) & 1, clamp = (cenv >> 19) & 1, scale = (cenv >> 20) & 3, dest = (cenv >> 22) & 3;
            W("    ivec3 ca = %s & 255, cb = %s & 255, cc = %s & 255, cd = %s;\n", kColorIn[a], kColorIn[b], kColorIn[c], kColorIn[d]);
            if (bias_ != 3) {
                static const char* biast[3] = {"", " + 128", " - 128"};
                static const char* sl[4] = {"", " << 1", " << 2", ""};
                static const char* lb[4] = {"", " + 128", "", " + 127"};
                // The lerp weights both terms rather than using GX's (b - a) * c form.
                // The two are algebraically identical, but (b - a) is negative whenever
                // b < a, and GLSL ES leaves >> on a negative value undefined -- a driver
                // may reassociate the shift onto that term. On an Adreno 740 the original
                // form returned zero for most of the screen, which is what made the title
                // screen render black.
                W("    ivec3 ccx = cc + (cc >> 7);\n");
                W("    ivec3 cr = (((cd%s)%s) %c ((((ca * (256 - ccx) + cb * ccx)%s)%s) >> 8))%s;\n",
                  biast[bias_], sl[scale], op ? '-' : '+', sl[scale],
                  lb[2 * op + (scale != 3 ? 1 : 0)], scale == 3 ? " / 2" : "");
            } else {
                uint32_t mode = (scale << 1) | op;
                switch (mode) {
                case 0: W("    ivec3 cr = cd + ((ca.r > cb.r) ? cc : ivec3(0));\n"); break;
                case 1: W("    ivec3 cr = cd + ((ca.r == cb.r) ? cc : ivec3(0));\n"); break;
                case 2: W("    ivec3 cr = cd + (((ca.g << 8 | ca.r) > (cb.g << 8 | cb.r)) ? cc : ivec3(0));\n"); break;
                case 3: W("    ivec3 cr = cd + (((ca.g << 8 | ca.r) == (cb.g << 8 | cb.r)) ? cc : ivec3(0));\n"); break;
                case 4: W("    ivec3 cr = cd + (((ca.b << 16 | ca.g << 8 | ca.r) > (cb.b << 16 | cb.g << 8 | cb.r)) ? cc : ivec3(0));\n"); break;
                case 5: W("    ivec3 cr = cd + (((ca.b << 16 | ca.g << 8 | ca.r) == (cb.b << 16 | cb.g << 8 | cb.r)) ? cc : ivec3(0));\n"); break;
                case 6: W("    ivec3 cr = cd + ivec3(greaterThan(ca, cb)) * cc;\n"); break;
                case 7: W("    ivec3 cr = cd + ivec3(equal(ca, cb)) * cc;\n"); break;
                }
            }
            if (clamp) W("    %s.rgb = clamp(cr, 0, 255);\n", kDest[dest]);
            else W("    %s.rgb = clamp(cr, -1024, 1023);\n", kDest[dest]);
        }
        // ---- alpha combiner ----
        {
            uint32_t d = (aenv >> 4) & 7, c = (aenv >> 7) & 7, b = (aenv >> 10) & 7, a = (aenv >> 13) & 7;
            uint32_t bias_ = (aenv >> 16) & 3, op = (aenv >> 18) & 1, clamp = (aenv >> 19) & 1, scale = (aenv >> 20) & 3, dest = (aenv >> 22) & 3;
            W("    int aa = %s & 255, ab = %s & 255, ac = %s & 255, ad = %s;\n", kAlphaIn[a], kAlphaIn[b], kAlphaIn[c], kAlphaIn[d]);
            if (bias_ != 3) {
                static const char* biast[3] = {"", " + 128", " - 128"};
                static const char* sl[4] = {"", " << 1", " << 2", ""};
                static const char* lb[4] = {"", " + 128", "", " + 127"};
                // Same weighted form as the colour combiner, for the same reason: (ab - aa)
                // is negative whenever ab < aa, and >> on a negative is undefined in
                // GLSL ES. The rounding-bias condition also matched on `scale == 3` here
                // where the colour path uses `scale != 3`; the two are the same equation,
                // so this was inverted and is corrected to agree.
                W("    int acx = ac + (ac >> 7);\n");
                W("    int ar = (((ad%s)%s) %c ((((aa * (256 - acx) + ab * acx)%s)%s) >> 8))%s;\n",
                  biast[bias_], sl[scale], op ? '-' : '+', sl[scale],
                  lb[2 * op + (scale != 3 ? 1 : 0)], scale == 3 ? " / 2" : "");
            } else {
                uint32_t mode = (scale << 1) | op;
                switch (mode) {
                case 0: W("    int ar = ad + ((ca.r > cb.r) ? ac : 0);\n"); break;
                case 1: W("    int ar = ad + ((ca.r == cb.r) ? ac : 0);\n"); break;
                case 2: W("    int ar = ad + (((ca.g << 8 | ca.r) > (cb.g << 8 | cb.r)) ? ac : 0);\n"); break;
                case 3: W("    int ar = ad + (((ca.g << 8 | ca.r) == (cb.g << 8 | cb.r)) ? ac : 0);\n"); break;
                case 4: W("    int ar = ad + (((ca.b << 16 | ca.g << 8 | ca.r) > (cb.b << 16 | cb.g << 8 | cb.r)) ? ac : 0);\n"); break;
                case 5: W("    int ar = ad + (((ca.b << 16 | ca.g << 8 | ca.r) == (cb.b << 16 | cb.g << 8 | cb.r)) ? ac : 0);\n"); break;
                case 6: W("    int ar = ad + ((aa > ab) ? ac : 0);\n"); break;
                case 7: W("    int ar = ad + ((aa == ab) ? ac : 0);\n"); break;
                }
            }
            if (clamp) W("    %s.a = clamp(ar, 0, 255);\n", kDest[dest]);
            else W("    %s.a = clamp(ar, -1024, 1023);\n", kDest[dest]);
        }
        s += "  }\n";
        // GCN_SNAP=N copies prev after stage N into `snap`, which GCN_SHOW=snap displays
        // at the end. This bisects the chain without altering control flow, so unlike
        // GCN_TEV_STOP it cannot change what it measures.
        if (snap_stage >= 0 && (int)st == snap_stage) s += "  snap = prev;\n";
        // Debug: GCN_TEV_STOP=N ends the chain after stage N and writes the running
        // result straight out, to bisect which stage a bad image comes from.
        // GCN_TEV_REG picks which register to show (0=prev, 1=c0, 2=c1, 3=c2), and
        // GCN_TEV_ALPHA=1 shows that register's alpha as greyscale instead of its rgb.
        if (tev_stop >= 0 && (int)st == tev_stop && (!tev_stop_stages || (int)nstages == tev_stop_stages)) {
            // 0-3 pick a TEV register, 4 the texel this stage sampled, 5 the
            // rasterized colour, 6 the konst. Showing the texel separates a bad
            // texture or texcoord from bad combiner inputs.
            // 7 and 8 are the two vertex colours as the fragment sees them.
            static const char* kShow[9] = {"prev", "c0", "c1", "c2", "tex", "ras", "konst", "col0", "col1"};
            const char* reg = kShow[tev_reg % 9];
            if (tev_alpha) W("  o_color = vec4(vec3(float(%s.a & 255) / 255.0), 1.0);\n", reg);
            else W("  o_color = vec4(vec3(%s.rgb & 255) / 255.0, 1.0);\n", reg);
            s += "  return;\n";
            break;
        }
    }
    s += "  ivec4 outc = prev & 255;\n";

    // alpha test
    {
        uint32_t f0 = k.alpha_func & 7, f1 = (k.alpha_func >> 3) & 7, logic = (k.alpha_func >> 6) & 3;
        char t0[128], t1[128];
        snprintf(t0, sizeof(t0), kCompare[f0], "outc.a", "u_alpharef.x");
        snprintf(t1, sizeof(t1), kCompare[f1], "outc.a", "u_alpharef.y");
        static const char* lop[4] = {"&&", "||", "!=", "=="};
        // GCN_NO_ALPHA_TEST=1 skips it, to tell a discard apart from a shading bug.
        if (!(f0 == 7 && f1 == 7 && logic == 0) && !no_alpha_test)
            W("  if (!(%s %s %s)) discard;\n", t0, lop[logic], t1);
    }
    // fog, when u_fogmode says there is any (fsel 2 and up)
    if (!no_fog) {
        // Not gl_FragCoord.z: that is the depth of whatever projection actually drew
        // this fragment, and in an eye that is the headset's, not the game's. v_fogz
        // carries the game's own screen z instead -- see the vertex shader. Clamping
        // stands in for the depth range the hardware would have applied, and covers
        // the geometry an eye can see outside the game's frustum, whose reconstructed
        // z lands outside [0,1] (behind the game's camera it even goes negative).
        s += "  int fsel = u_fogmode & 7;\n"
             "  if (fsel >= 2) {\n"
             "    float zs = clamp(v_fogz.x / v_fogz.y, 0.0, 1.0) * 16777215.0;\n"
             "    float ze = (u_fogmode & 8) != 0 ? u_fog.x * (zs / 16777215.0)\n"
             "                                    : (u_fog.x * 16777216.0) / (u_fog.z - floor(zs / exp2(u_fog.w)));\n"
             "    float fog = clamp(ze - u_fog.y, 0.0, 1.0);\n"
             "    if (fsel == 4) fog = 1.0 - exp2(-8.0 * fog);\n"
             "    else if (fsel == 5) fog = 1.0 - exp2(-8.0 * fog * fog);\n"
             "    else if (fsel == 6) fog = exp2(-8.0 * (1.0 - fog));\n"
             "    else if (fsel == 7) fog = exp2(-8.0 * (1.0 - fog) * (1.0 - fog));\n"
             "    vec3 fc = mix(vec3(outc.rgb), u_fogcolor * 255.0, fog);\n"
             "    outc.rgb = ivec3(round(fc));\n"
             "  }\n";
    }
    // Z texture (GXSetZTexture): the last stage's texel, read as a depth, replaces the
    // fragment's depth or is added to it. A frame is commonly cleared this way -- a
    // quad at the near plane with a texture holding the far depth, Z compare off and Z
    // update on -- and without it that quad wrote the nearest depth over the whole
    // frame and nothing drawn after it passed the depth test. The texel layouts are the
    // ones the texture decoder gives the Z formats' memory: Z8 in alpha, Z16 as alpha
    // (high byte) and red, Z24X8 as red, green, blue.
    // Z freeze (GXSetCoPlanar, gen mode bit 19): the triangle's depth is not its own but
    // the plane of the last triangle drawn without it, evaluated at this pixel. A game
    // sets the plane with a triangle it culls -- a sky drawn after one placed at the far
    // end of the frustum keeps out of the way of everything beyond its own geometry, the
    // planets and the far terrain. u_zplane is that plane in this target's window
    // coordinates (depth = x * gl_FragCoord.x + y * gl_FragCoord.y + z); w is 0 where the
    // renderer has none, and the draw keeps its own depth.
    const char* fragz = "gl_FragCoord.z";
    if (zfreeze) {
        s += "  float zfrag = u_zplane.w != 0.0 ? clamp(u_zplane.x * gl_FragCoord.x + u_zplane.y * gl_FragCoord.y + u_zplane.z, 0.0, 1.0) : gl_FragCoord.z;\n";
        if (!k.ztex) s += "  gl_FragDepth = zfrag;\n";
        fragz = "zfrag";
    }
    // A foreground layer's game depth (u_vr 4), per fragment. Interpolated from the vertices
    // in the eye's screen, the game's depth is right only where the eye looks the way the
    // game's camera does: it is linear in the camera's screen, not in an eye turned away
    // from it. A weapon held in the hand and looked at sideways spans a wide angle close to
    // the camera, and across its large faces the error let the parts inside it win the
    // depth test against its shell. The game's clip z and w are linear in the vertex, so
    // their perspective-correct interpolation is exact, and the ratio is the depth the game
    // would give this point, put in the band as the vertex shader puts it. (The flat view
    // interpolates the game's z/w in the game's own screen, which is what makes it right
    // there.)
    if (k.fg_depth && !zfreeze && !k.ztex) {
        s += "  float gdr = v_gdepth.y > 0.0 ? clamp(v_gdepth.x / v_gdepth.y, -1.0, 0.0) : -1.0;\n";
        s += "  gl_FragDepth = u_vr == 4 ? 0.5 * (u_vp_b.x + u_vp_b.y * gdr) + 0.5 : gl_FragCoord.z;\n";
    }
    if (k.ztex) {
        static const char* kZTexel[4] = {"tex.a", "tex.a << 8 | tex.r", "tex.r << 16 | tex.g << 8 | tex.b",
                                         "tex.r << 16 | tex.g << 8 | tex.b"};
        W("  int ztexel = (%s) + u_zbias;\n", kZTexel[k.ztex & 3]);
        if (((k.ztex >> 2) & 3) == 1) W("  ztexel += int(%s * 16777215.0);\n", fragz);
        s += "  gl_FragDepth = float(ztexel & 0xFFFFFF) / 16777215.0;\n";
    }
    s += "  o_color = vec4(outc) / 255.0;\n";
    // The constant destination alpha (dst_alpha_mode): stored in place of the TEV's,
    // which the blend then reads from the second output.
    if (k.dst_alpha == 2) s += "  o_src1 = o_color;\n";
    if (k.dst_alpha) s += "  o_color.a = u_dstalpha;\n";
    if (getenv("GCN_FLAT")) s += "  o_color = vec4(1.0, 0.0, 1.0, 1.0);\n";
    // GCN_SHOW=prev|c0|c1|c2|konst (plus GCN_SHOW_ALPHA=1) replaces the final colour
    // with a TEV value. Unlike GCN_TEV_STOP this changes no control flow at all --
    // every stage, the alpha test and fog still run exactly as they normally would --
    // so it cannot perturb what it is measuring.
    // GCN_SHOW_STAGES=<n> confines it to programs of exactly n stages, as
    // GCN_TEV_STOP_STAGES does for the stop; col0 and col1 name the vertex colours.
    static const int show_stages = getenv("GCN_SHOW_STAGES") ? atoi(getenv("GCN_SHOW_STAGES")) : 0;
    // GCN_SHOW_MIN_STAGES=<n> leaves programs of fewer stages alone instead, which keeps
    // a one-stage full-screen blit from painting its own value over everything.
    static const int show_min_stages = getenv("GCN_SHOW_MIN_STAGES") ? atoi(getenv("GCN_SHOW_MIN_STAGES")) : 0;
    const char* show = getenv("GCN_SHOW");
    if (show && show_stages && (int)nstages != show_stages) show = nullptr;
    if (show && show_min_stages && (int)nstages < show_min_stages) show = nullptr;
    if (show) {
        // "stages" paints the program's stage count as a grey level, n/16, so one frame
        // says which program drew each pixel.
        static char stages_expr[32];
        snprintf(stages_expr, sizeof(stages_expr), "ivec4(%u * 16)", nstages);
        if (!strcmp(show, "stages")) show = stages_expr;
        // "probe" packs stage 12's three inputs into one image so they are measured in
        // a single run, on the same pixels of the same frame: red is prev as it entered
        // the stage (GCN_SNAP), green is c1's red, blue is c1's alpha. Comparing these
        // across separate runs is unsound because the attract sequence drifts.
        if (!strcmp(show, "probe"))
            s += "  o_color = vec4(float(snap.r & 255) / 255.0, float(c1.r & 255) / 255.0,"
                 " float(c1.a & 255) / 255.0, 1.0);\n";
        // "probe2" shows one stage's input, its blend factor and its output together:
        // red is prev entering stage GCN_SNAP+1, green is c1's alpha (the factor), blue
        // is prev at the end. If red is bright and green is zero, blue must match red.
        else if (!strcmp(show, "probe2"))
            s += "  o_color = vec4(float(snap.r & 255) / 255.0, float(c1.a & 255) / 255.0,"
                 " float(prev.r & 255) / 255.0, 1.0);\n";
        else if (getenv("GCN_SHOW_ALPHA")) W("  o_color = vec4(vec3(float(%s.a & 255) / 255.0), 1.0);\n", show);
        else W("  o_color = vec4(vec3(%s.rgb & 255) / 255.0, 1.0);\n", show);
        // GCN_SHOW_SPLIT=<x> shows the value only right of EFB column x (in the target's
        // own pixels, so 640 is the middle at --scale=2), leaving the left half as it
        // normally renders: the two can then be read off one frame, which matters when
        // no two runs reach the same frame at the same moment.
        if (const char* split = getenv("GCN_SHOW_SPLIT")) {
            s.insert(s.rfind("  o_color = vec4(vec3("), "  if (gl_FragCoord.x > " + std::string(split) + ".0)\n  ");
        }
    }
    s += "}\n";
#ifdef GCN_GL_ES
    // An #extension has to precede every non-preprocessor token, the precision statements
    // of GCN_GLSL_VERSION among them, so it goes straight after the #version line.
    if (k.dst_alpha == 2) s.insert(s.find('\n') + 1, "#extension GL_EXT_blend_func_extended : require\n");
#endif
    return s;
}

}  // namespace gx
