// Overhead race maps for the Mario Kart lower screen, built from the player's own courses.
// Pipeline after reference/mkw-3ds/tools/race_map_bake.py (frame from KMP, roofs removed with
// KCL road heights, top-down render, relief/occlusion/shadow finish). The course model is read
// directly (BRRES/MDL0 as documented by noclip.website's rres/brres.ts, MIT); materials are
// simplified to texture x vertex/material colour with the authored alpha test and blending.
#include "art_internal.h"
#include "wiids_art_layout.h"
#include "wiids_art_profiles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace wiids::art {
namespace {

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Buf {
    const Bytes& d;
    size_t base = 0;
    uint8_t U8(size_t o) const { if (base + o >= d.size()) Fail("Damaged course model"); return d[base + o]; }
    uint16_t U16(size_t o) const { return uint16_t(U8(o) << 8 | U8(o + 1)); }
    int16_t S16(size_t o) const { return int16_t(U16(o)); }
    uint32_t U32(size_t o) const { return uint32_t(U16(o)) << 16 | U16(o + 2); }
    int32_t S32(size_t o) const { return int32_t(U32(o)); }
    float F32(size_t o) const { const uint32_t v = U32(o); float f; std::memcpy(&f, &v, 4); return f; }
    std::string Str(size_t o) const { std::string s; while (base + o < d.size() && d[base + o]) s += char(d[base + o++]); return s; }
    Buf At(size_t o) const { return Buf{d, base + o}; }
};

struct DicEntry { std::string name; size_t offs; }; // offs absolute in the file
std::vector<DicEntry> ResDic(const Buf& b, size_t table) {
    std::vector<DicEntry> out;
    if (!table) return out;
    const uint32_t count = b.U32(table + 4);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = table + 8 + 16 + size_t(i) * 16;
        out.push_back({b.Str(table + b.U32(e + 8)), b.base + table + b.U32(e + 12)});
    }
    return out;
}

// ---------------------------------------------------------------- textures
struct Texture {
    std::vector<std::vector<float>> levels; // RGBA float, box-filtered mips
    std::vector<std::pair<int, int>> sizes;
    float Mean(int c) const {
        double s = 0;
        for (size_t i = size_t(c); i < levels[0].size(); i += 4) s += levels[0][i];
        return float(s / (levels[0].size() / 4));
    }
};

Texture MakeTexture(const Image& image) {
    Texture t;
    std::vector<float> level(image.rgba.size());
    for (size_t i = 0; i < level.size(); ++i) level[i] = image.rgba[i] / 255.0f;
    int w = image.width, h = image.height;
    t.levels.push_back(level);
    t.sizes.push_back({w, h});
    while (std::min(w, h) > 1) {
        const int nw = w / 2, nh = h / 2;
        std::vector<float> next(size_t(nw) * size_t(nh) * 4);
        const auto& prev = t.levels.back();
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c)
                    next[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(c)] =
                        (prev[(size_t(2 * y) * size_t(w) + size_t(2 * x)) * 4 + size_t(c)] + prev[(size_t(2 * y) * size_t(w) + size_t(2 * x + 1)) * 4 + size_t(c)] +
                         prev[(size_t(2 * y + 1) * size_t(w) + size_t(2 * x)) * 4 + size_t(c)] + prev[(size_t(2 * y + 1) * size_t(w) + size_t(2 * x + 1)) * 4 + size_t(c)]) / 4;
        t.levels.push_back(std::move(next));
        t.sizes.push_back({nw, nh});
        w = nw; h = nh;
    }
    return t;
}

long WrapIndex(long c, long n, int mode) {
    if (mode == 1) { c %= n; return c < 0 ? c + n : c; }
    if (mode == 2) { c %= 2 * n; if (c < 0) c += 2 * n; return c < n ? c : 2 * n - 1 - c; }
    return std::min(std::max(c, 0L), n - 1);
}

void Sample(const Texture& t, double u, double v, int wrapS, int wrapT, double lod, float out[4]) {
    const size_t level = size_t(std::min(std::max(int(lod), 0), int(t.levels.size()) - 1));
    const auto [w, h] = t.sizes[level];
    const auto& tex = t.levels[level];
    const double x = u * w - 0.5, y = v * h - 0.5;
    const double x0 = std::floor(x), y0 = std::floor(y), fx = x - x0, fy = y - y0;
    const long xa = WrapIndex(long(x0), w, wrapS), xb = WrapIndex(long(x0) + 1, w, wrapS);
    const long ya = WrapIndex(long(y0), h, wrapT), yb = WrapIndex(long(y0) + 1, h, wrapT);
    for (int c = 0; c < 4; ++c) {
        auto at = [&](long xx, long yy) { return double(tex[(size_t(yy) * size_t(w) + size_t(xx)) * 4 + size_t(c)]); };
        out[c] = float((at(xa, ya) * (1 - fx) + at(xb, ya) * fx) * (1 - fy) + (at(xa, yb) * (1 - fx) + at(xb, yb) * fx) * fy);
    }
}

// ---------------------------------------------------------------- MDL0
struct Vertex { double p[3], n[3], c[4], uv[2][2]; };
struct Sampler { std::string name; int wrapS = 1, wrapT = 1; };
struct TevStage {
    int texMap = 0, texCoord = 0, chan = 0;
    bool texEnable = false;
    uint32_t colorEnv = 0, alphaEnv = 0;
    int kcsel = 0, kasel = 0;
};
struct Material {
    Sampler samplers[8];
    std::vector<TevStage> stages;
    double regs[4][4] = {};   // PREV, C0, C1, C2
    double konst[4][4] = {};  // K0..K3
    bool translucent = false, blend = false, additive = false;
    uint8_t alphaRef0 = 0, alphaRef1 = 0, alphaComp0 = 7, alphaComp1 = 7, alphaOp = 0;
    bool colorFromVertex = true, alphaFromVertex = true, lit = false, ambFromVertex = false;
    double matColor[4] = {1, 1, 1, 1}, ambColor[4] = {0, 0, 0, 1};
    double texMtx[8][6]; // s' = m0 s + m1 t + m2 ; t' = m3 s + m4 t + m5
};
struct Triangle { Vertex v[3]; size_t material; };

void TexMatrix(int mode, double sS, double sT, double rot, double tS, double tT, double m[6]) {
    const double th = rot * 3.141592653589793 / 180, s = std::sin(th), c = std::cos(th);
    // noclip calcTexMtx_*: dst[0],dst[4],dst[12] (s row) and dst[1],dst[5],dst[13] (t row).
    switch (mode) {
        case 0: // Maya
            m[0] = sS * c; m[1] = sS * s; m[2] = sS * ((-0.5 * c) - (0.5 * s - 0.5) - tS);
            m[3] = sT * -s; m[4] = sT * c; m[5] = sT * ((-0.5 * c) + (0.5 * s - 0.5) + tT) + 1.0;
            break;
        case 1: // XSI
            m[0] = sS * c; m[1] = sS * -s; m[2] = (sS * s) - (sS * c * tS) - (sS * s * tT);
            m[3] = sT * s; m[4] = sT * c; m[5] = (sT * -c) - (sT * s * tS) + (sT * c * tT) + 1.0;
            break;
        case 2: // Max
            m[0] = sS * c; m[1] = sS * s; m[2] = sS * ((-c * (tS + 0.5)) + (s * (tT - 0.5))) + 0.5;
            m[3] = sT * -s; m[4] = sT * c; m[5] = sT * ((s * (tS + 0.5)) + (c * (tT - 0.5))) + 0.5;
            break;
        default: // Basic
            m[0] = sS * c; m[1] = sT * -s; m[2] = tS;
            m[3] = sS * s; m[4] = sT * c; m[5] = tT;
    }
}

// The BP register file a material's display lists build (write mask and TEV colour banks as GX).
struct BpRegs {
    uint32_t bp[256] = {};
    uint32_t kcolor[16] = {}; // bank 0 (registers) 0..7, bank 1 (konst) 8..15
    uint32_t mask = 0xFFFFFF;
    void Set(uint32_t bag) {
        const uint32_t addr = bag >> 24;
        const uint32_t value = (bp[addr] & ~mask) | (bag & mask);
        if (addr != 0xFE) mask = 0xFFFFFF; else mask = bag & 0xFFFFFF;
        bp[addr] = value;
        if (addr >= 0xE0 && addr <= 0xE7) kcolor[((value >> 23) & 1) * 8 + (addr - 0xE0)] = value;
    }
    void Run(const Buf& m, size_t at, size_t size) {
        for (size_t i = 0; i < size;) {
            const uint8_t cmd = m.U8(at + i++);
            if (cmd == 0x61) { Set(m.U32(at + i)); i += 4; }
            else if (cmd == 0x08) i += 5;
            else if (cmd == 0x10) { const uint16_t len = uint16_t(m.U16(at + i) + 1); i += 4 + size_t(len) * 4; }
            else if (cmd == 0x00) continue;
            else break;
        }
    }
};

Material ParseMaterial(const Buf& m, uint32_t version, const Buf& file) {
    Material out;
    for (auto& t : out.texMtx) { t[0] = 1; t[1] = 0; t[2] = 0; t[3] = 0; t[4] = 1; t[5] = 0; }
    out.translucent = (m.U32(0x10) & 0x80000000u) != 0;
    const int numChans = m.U8(0x15), numTevs = m.U8(0x16);
    const uint32_t numTex = m.U32(0x2C), texOffs = m.U32(0x30);
    const size_t end = 0x34 + (version >= 10 ? 4 : 0) + 4;
    BpRegs r;
    r.Run(m, m.U32(end), 32 + 128 + 64 + 160);
    const int32_t tevOffs = m.S32(0x28);
    const Buf tev = file.At(size_t(int64_t(m.base) + tevOffs));
    r.Run(tev, 0x20, 480);
    const uint32_t ac = r.bp[0xF3];
    out.alphaRef0 = uint8_t(ac); out.alphaRef1 = uint8_t(ac >> 8);
    out.alphaComp0 = uint8_t((ac >> 16) & 7); out.alphaComp1 = uint8_t((ac >> 19) & 7); out.alphaOp = uint8_t((ac >> 22) & 3);
    // A material that never loads the alpha compare register passes every fragment.
    if (!ac) { out.alphaComp0 = 7; out.alphaComp1 = 7; }
    const uint32_t bm = r.bp[0x41];
    out.blend = bm & 1;
    out.additive = out.blend && ((bm >> 5) & 7) == 1; // dst factor ONE
    for (int i = 0; i < 4; ++i)
        for (int bank = 0; bank < 2; ++bank) {
            const uint32_t lo = r.kcolor[bank * 8 + i * 2], hi = r.kcolor[bank * 8 + i * 2 + 1];
            auto ch = [](uint32_t v) { const int x = int(v & 0x7FF); return (x >= 1024 ? x - 2048 : x) / 255.0; };
            double* c = bank ? out.konst[i] : out.regs[i];
            c[0] = ch(lo); c[3] = ch(lo >> 12); c[2] = ch(hi); c[1] = ch(hi >> 12);
            if (bank) for (int k = 0; k < 4; ++k) c[k] = std::min(1.0, std::max(0.0, c[k]));
        }
    for (int sIdx = 0; sIdx < numTevs; ++sIdx) {
        TevStage st;
        const uint32_t tref = r.bp[0x28 + sIdx / 2] >> ((sIdx & 1) * 12);
        st.texMap = int(tref & 7); st.texCoord = int((tref >> 3) & 7); st.texEnable = (tref >> 6) & 1; st.chan = int((tref >> 7) & 7);
        st.colorEnv = r.bp[0xC0 + sIdx * 2];
        st.alphaEnv = r.bp[0xC1 + sIdx * 2];
        const uint32_t ksel = r.bp[0xF6 + sIdx / 2];
        st.kcsel = int((ksel >> ((sIdx & 1) ? 14 : 4)) & 0x1F);
        st.kasel = int((ksel >> ((sIdx & 1) ? 19 : 9)) & 0x1F);
        out.stages.push_back(st);
    }
    if (numChans > 0) {
        const size_t lc = end + 0x3B4;
        for (int k = 0; k < 4; ++k) { out.matColor[k] = m.U8(lc + 4 + size_t(k)) / 255.0; out.ambColor[k] = m.U8(lc + 8 + size_t(k)) / 255.0; }
        const uint32_t cc = m.U32(lc + 0x0C), ca = m.U32(lc + 0x10);
        out.colorFromVertex = cc & 1;
        out.lit = (cc >> 1) & 1;
        out.ambFromVertex = (cc >> 6) & 1;
        out.alphaFromVertex = ca & 1;
    }
    for (uint32_t i = 0; i < numTex; ++i) {
        const size_t info = texOffs + size_t(i) * 0x34;
        const uint32_t map = m.U32(info + 0x10);
        if (map >= 8) continue;
        out.samplers[map] = {m.Str(info + m.U32(info)), int(m.U32(info + 0x18)), int(m.U32(info + 0x1C))};
    }
    const uint32_t srtFlags = m.U32(end + 0x16C);
    const int texMtxMode = m.S32(end + 0x170);
    for (int i = 0; i < 8; ++i) {
        const size_t srt = end + 0x174 + size_t(i) * 0x14, mtx = end + 0x214 + size_t(i) * 0x34;
        const uint32_t f = (srtFlags >> (i * 4)) & 0xF;
        if (int8_t(m.U8(mtx + 2)) != 0) continue; // environment/projection maps keep raw coordinates
        const double sS = (f & 2) ? 1 : m.F32(srt), sT = (f & 2) ? 1 : m.F32(srt + 4);
        const double rot = (f & 4) ? 0 : m.F32(srt + 8);
        const double tS = (f & 8) ? 0 : m.F32(srt + 12), tT = (f & 8) ? 0 : m.F32(srt + 16);
        TexMatrix(texMtxMode, sS, sT, rot, tS, tT, out.texMtx[i]);
    }
    return out;
}

struct VtxArray { int compCnt = 0, compType = 0, shift = 0, stride = 0, count = 0; size_t data = 0; };

VtxArray ParseArray(const Buf& a, bool color) {
    VtxArray v;
    v.data = a.base + a.U32(0x08);
    v.compCnt = int(a.U32(0x14));
    v.compType = int(a.U32(0x18));
    v.shift = a.U8(0x1C);
    v.stride = a.U8(0x1D);
    v.count = a.U16(0x1E);
    if (color) { v.stride = v.shift; v.shift = 0; }
    return v;
}

double Component(const Bytes& d, size_t at, int type, int shift, bool normal) {
    auto u8 = [&](size_t o) { if (o >= d.size()) Fail("Damaged course model"); return d[o]; };
    double v = 0;
    switch (type) {
        case 0: v = u8(at); break;
        case 1: v = int8_t(u8(at)); break;
        case 2: v = uint16_t(u8(at) << 8 | u8(at + 1)); break;
        case 3: v = int16_t(uint16_t(u8(at) << 8 | u8(at + 1))); break;
        default: { const uint32_t r = uint32_t(u8(at)) << 24 | uint32_t(u8(at + 1)) << 16 | uint32_t(u8(at + 2)) << 8 | u8(at + 3); float f; std::memcpy(&f, &r, 4); return f; }
    }
    if (normal) return v / (type <= 1 ? 64.0 : 16384.0);
    return v / double(1u << shift);
}
int TypeSize(int type) { return type <= 1 ? 1 : type <= 3 ? 2 : 4; }

void ReadColor(const Bytes& d, const VtxArray& a, int index, double out[4]) {
    const size_t at = a.data + size_t(index) * size_t(a.stride);
    auto u8 = [&](size_t o) { if (o >= d.size()) Fail("Damaged course model"); return d[o]; };
    switch (a.compType) {
        case 0: { const int v = u8(at) << 8 | u8(at + 1); out[0] = ((v >> 11) & 31) / 31.0; out[1] = ((v >> 5) & 63) / 63.0; out[2] = (v & 31) / 31.0; out[3] = 1; break; }
        case 1: case 2: for (int k = 0; k < 3; ++k) out[k] = u8(at + size_t(k)) / 255.0; out[3] = 1; break;
        case 3: { const int v = u8(at) << 8 | u8(at + 1); for (int k = 0; k < 4; ++k) out[k] = ((v >> (12 - 4 * k)) & 15) / 15.0; break; }
        case 4: { const int v = u8(at) << 16 | u8(at + 1) << 8 | u8(at + 2); for (int k = 0; k < 4; ++k) out[k] = ((v >> (18 - 6 * k)) & 63) / 63.0; break; }
        default: for (int k = 0; k < 4; ++k) out[k] = u8(at + size_t(k)) / 255.0;
    }
}

struct Model {
    std::vector<Triangle> opaque, translucent;
    std::vector<Material> materials;
};

void Transform(const std::array<double, 12>& m, const double in[3], double out[3], bool direction) {
    for (int r = 0; r < 3; ++r) out[r] = m[size_t(r) * 4] * in[0] + m[size_t(r) * 4 + 1] * in[1] + m[size_t(r) * 4 + 2] * in[2] + (direction ? 0 : m[size_t(r) * 4 + 3]);
}

Model ParseCourseModel(const Bytes& data, std::map<std::string, Texture>& textures) {
    const Buf file{data, 0};
    if (data.size() < 16 || std::memcmp(data.data(), "bres", 4) != 0) Fail("The course model isn't a BRRES archive.");
    const size_t rootOffs = file.U16(0x0C);
    const auto root = ResDic(file, rootOffs + 8);
    std::map<std::string, std::pair<std::string, Bytes>> palettes; // name -> (format, data)
    Model model;
    for (const auto& folder : root) {
        if (folder.name == "Palettes(NW4R)")
            for (const auto& e : ResDic(file, folder.offs)) {
                const Buf p = file.At(e.offs);
                const size_t dataOffs = p.U32(0x10);
                const uint32_t fmt = p.U32(0x18);
                const uint16_t n = p.U16(0x1C);
                palettes[e.name] = {std::to_string(fmt), Bytes(data.begin() + std::ptrdiff_t(e.offs + dataOffs), data.begin() + std::ptrdiff_t(e.offs + dataOffs + size_t(n) * 2))};
            }
    }
    for (const auto& folder : root) {
        if (folder.name != "Textures(NW4R)") continue;
        for (const auto& e : ResDic(file, folder.offs)) {
            const Buf t = file.At(e.offs);
            const size_t dataOffs = t.U32(0x10);
            const int w = t.U16(0x1C), h = t.U16(0x1E);
            const auto fmt = GxFormat(t.U32(0x20));
            try {
                const uint8_t* pixels = data.data() + e.offs + dataOffs;
                const size_t avail = data.size() - (e.offs + dataOffs);
                Image image;
                if (fmt == GxFormat::C4 || fmt == GxFormat::C8 || fmt == GxFormat::C14X2) {
                    const auto it = palettes.find(e.name);
                    if (it == palettes.end()) continue;
                    image = DecodeGx(fmt, pixels, avail, w, h, it->second.second.data(), PaletteFormat(std::stoi(it->second.first)), int(it->second.second.size() / 2));
                } else {
                    image = DecodeGx(fmt, pixels, avail, w, h);
                }
                textures[e.name] = MakeTexture(image);
            } catch (const std::exception&) {}
        }
    }
    for (const auto& folder : root) {
        if (folder.name != "3DModels(NW4R)") continue;
        const auto models = ResDic(file, folder.offs);
        if (models.empty()) Fail("The course archive has no model.");
        const Buf m = file.At(models[0].offs);
        const uint32_t version = m.U32(0x08);
        size_t offs = 0x10;
        auto next = [&]() { auto r = ResDic(m, m.U32(offs)); offs += 4; return r; };
        const auto byteCode = next(), nodeDic = next(), posDic = next(), nrmDic = next(), clrDic = next(), txcDic = next();
        if (version >= 10) { next(); next(); }
        const auto matDic = next();
        next(); // tev
        const auto shpDic = next();
        offs += 8 + (version >= 11 ? 4 : 0);
        const size_t info = offs + 4;
        const size_t mtxTable = info + m.U32(info + 0x24);
        const uint32_t numMtx = m.U32(mtxTable);
        std::vector<int32_t> mtxToNode;
        for (uint32_t i = 0; i < numMtx; ++i) mtxToNode.push_back(m.S32(mtxTable + 4 + 4 * i));
        // Node world matrices: each node's scale/rotation/translation, composed down the parent
        // chain (noclip computeModelMatrixSRT + NodeTree), as the model is drawn. Custom tracks
        // often scale the root node while leaving its stored bind pose at identity.
        std::vector<std::array<double, 12>> nodes;
        std::vector<int> parents;
        for (const auto& e : nodeDic) {
            const Buf n = file.At(e.offs);
            const double sx = n.F32(0x20), sy = n.F32(0x24), sz = n.F32(0x28);
            const double d2r = 3.141592653589793 / 180;
            const double rx = n.F32(0x2C) * d2r, ry = n.F32(0x30) * d2r, rz = n.F32(0x34) * d2r;
            const double sX = std::sin(rx), cX = std::cos(rx), sY = std::sin(ry), cY = std::cos(ry), sZ = std::sin(rz), cZ = std::cos(rz);
            std::array<double, 12> m = {
                sx * (cY * cZ), sy * (sX * cZ * sY - cX * sZ), sz * (cX * cZ * sY + sX * sZ), double(n.F32(0x38)),
                sx * (sZ * cY), sy * (sX * sZ * sY + cX * cZ), sz * (cX * sZ * sY - sX * cZ), double(n.F32(0x3C)),
                sx * (-sY),     sy * (sX * cY),                sz * (cY * cX),                double(n.F32(0x40))};
            nodes.push_back(m);
            const int32_t toParent = n.S32(0x5C);
            parents.push_back(toParent == 0 || nodeDic.empty() ? -1 : int((int64_t(e.offs) + toParent - int64_t(nodeDic[0].offs)) / 0xD0));
        }
        // Parents precede children in the node table; compose in order.
        for (size_t i = 0; i < nodes.size(); ++i) {
            const int parent = parents[i];
            if (parent < 0 || size_t(parent) >= i) continue;
            const auto& a = nodes[size_t(parent)];
            const auto b = nodes[i];
            std::array<double, 12> c{};
            for (int r = 0; r < 3; ++r)
                for (int col = 0; col < 4; ++col)
                    c[size_t(r * 4 + col)] = a[size_t(r * 4)] * b[size_t(col)] + a[size_t(r * 4 + 1)] * b[size_t(4 + col)] +
                                             a[size_t(r * 4 + 2)] * b[size_t(8 + col)] + (col == 3 ? a[size_t(r * 4 + 3)] : 0);
            nodes[i] = c;
        }
        const std::array<double, 12> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        auto mtxNode = [&](int mtxId) -> const std::array<double, 12>& {
            if (mtxId >= 0 && size_t(mtxId) < mtxToNode.size()) {
                const int node = mtxToNode[size_t(mtxId)];
                if (node >= 0 && size_t(node) < nodes.size()) return nodes[size_t(node)];
            }
            return identity;
        };
        for (const auto& e : matDic) model.materials.push_back(ParseMaterial(file.At(e.offs), version, file));
        std::vector<VtxArray> pos, nrm, clr, txc;
        for (const auto& e : posDic) pos.push_back(ParseArray(file.At(e.offs), false));
        for (const auto& e : nrmDic) nrm.push_back(ParseArray(file.At(e.offs), false));
        for (const auto& e : clrDic) clr.push_back(ParseArray(file.At(e.offs), true));
        for (const auto& e : txcDic) txc.push_back(ParseArray(file.At(e.offs), false));

        // Draw lists: which material each shape uses, and whether it is translucent.
        struct Draw { uint16_t mat, shp; bool xlu; };
        std::vector<Draw> draws;
        for (const auto& e : byteCode) {
            if (e.name != "DrawOpa" && e.name != "DrawXlu") continue;
            const Buf b = file.At(e.offs);
            for (size_t i = 0; b.U8(i) == 0x04; i += 8) draws.push_back({b.U16(i + 1), b.U16(i + 3), e.name == "DrawXlu"});
        }
        for (const Draw& draw : draws) {
            if (draw.shp >= shpDic.size() || draw.mat >= model.materials.size()) continue;
            const Buf s = file.At(shpDic[draw.shp].offs);
            const int32_t mtxIdx = s.S32(0x08);
            const size_t preSize = s.U32(0x18), pre = 0x18 + s.U32(0x20);
            const size_t primSize = s.U32(0x24), prim = 0x24 + s.U32(0x2C);
            const int idPos = s.S16(0x48), idNrm = s.S16(0x4A), idClr = s.S16(0x4C), idTxc = s.S16(0x50), idTxc1 = s.S16(0x52);
            const size_t mtxSet = s.U32(0x64);
            std::vector<uint16_t> slots;
            if (mtxSet) for (uint32_t i = 0; i < s.U32(mtxSet); ++i) slots.push_back(s.U16(mtxSet + 4 + 2 * i));
            uint32_t vcdLo = 0, vcdHi = 0;
            for (size_t i = 0; i < preSize;) {
                const uint8_t cmd = s.U8(pre + i++);
                if (cmd == 0x08) { const uint8_t reg = s.U8(pre + i); const uint32_t v = s.U32(pre + i + 1); i += 5; if (reg == 0x50) vcdLo = v; if (reg == 0x60) vcdHi = v; }
                else if (cmd == 0x10) { const uint16_t len = uint16_t(s.U16(pre + i) + 1); i += 4 + size_t(len) * 4; }
                else if (cmd == 0x61) i += 4;
                else if (cmd == 0x00) continue;
                else break;
            }
            int types[21] = {};
            for (int a = 0; a < 9; ++a) types[a] = (vcdLo >> a) & 1;
            types[9] = (vcdLo >> 9) & 3; types[10] = (vcdLo >> 11) & 3; types[11] = (vcdLo >> 13) & 3; types[12] = (vcdLo >> 15) & 3;
            for (int t = 0; t < 8; ++t) types[13 + t] = (vcdHi >> (2 * t)) & 3;
            const VtxArray* P = idPos >= 0 && size_t(idPos) < pos.size() ? &pos[size_t(idPos)] : nullptr;
            const VtxArray* N = idNrm >= 0 && size_t(idNrm) < nrm.size() ? &nrm[size_t(idNrm)] : nullptr;
            const VtxArray* C = idClr >= 0 && size_t(idClr) < clr.size() ? &clr[size_t(idClr)] : nullptr;
            const VtxArray* T = idTxc >= 0 && size_t(idTxc) < txc.size() ? &txc[size_t(idTxc)] : nullptr;
            const VtxArray* T1 = idTxc1 >= 0 && size_t(idTxc1) < txc.size() ? &txc[size_t(idTxc1)] : nullptr;
            if (!P) continue;
            const Material& mat = model.materials[draw.mat];
            auto readIndex = [&](size_t& at, int type) -> int {
                if (type == 2) return s.U8(prim + at++);
                if (type == 3) { const int v = s.U16(prim + at); at += 2; return v; }
                return -1;
            };
            for (size_t at = 0; at < primSize;) {
                const uint8_t op = s.U8(prim + at++);
                if (op == 0) continue;
                const int kind = op & 0xF8;
                if (kind < 0x80) break;
                const uint16_t count = s.U16(prim + at);
                at += 2;
                std::vector<Vertex> verts;
                for (uint16_t v = 0; v < count; ++v) {
                    Vertex vx{};
                    vx.c[0] = vx.c[1] = vx.c[2] = vx.c[3] = 1;
                    int pnmtx = -1;
                    for (int a = 0; a < 9; ++a) if (types[a]) { const int val = s.U8(prim + at++); if (a == 0) pnmtx = val; }
                    // POS
                    {
                        const int idx = readIndex(at, types[9]);
                        if (idx < 0) Fail("Unsupported direct vertex data in the course model.");
                        const int n = P->compCnt ? 3 : 2, size = TypeSize(P->compType);
                        const size_t base = P->data + size_t(idx) * size_t(P->stride ? P->stride : n * size);
                        double p[3] = {0, 0, 0};
                        for (int k = 0; k < n; ++k) p[k] = Component(data, base + size_t(k * size), P->compType, P->shift, false);
                        const auto& mtx = pnmtx >= 0 && size_t(pnmtx / 3) < slots.size() ? mtxNode(slots[size_t(pnmtx / 3)]) : mtxNode(mtxIdx);
                        Transform(mtx, p, vx.p, false);
                        if (types[10]) {
                            const int nidx = readIndex(at, types[10]);
                            if (N && N->compCnt == 2) { readIndex(at, types[10]); readIndex(at, types[10]); }
                            if (N && nidx >= 0) {
                                const int size2 = TypeSize(N->compType);
                                const size_t nb = N->data + size_t(nidx) * size_t(N->stride ? N->stride : 3 * size2 * (N->compCnt == 1 ? 3 : 1));
                                double nn[3];
                                for (int k = 0; k < 3; ++k) nn[k] = Component(data, nb + size_t(k * size2), N->compType, 0, true);
                                Transform(mtx, nn, vx.n, true);
                            }
                        } else { vx.n[1] = 1; }
                    }
                    if (types[11]) { const int idx = readIndex(at, types[11]); if (C && idx >= 0) ReadColor(data, *C, idx, vx.c); }
                    if (types[12]) readIndex(at, types[12]);
                    for (int t = 0; t < 8; ++t) {
                        if (!types[13 + t]) continue;
                        const int idx = readIndex(at, types[13 + t]);
                        const VtxArray* A = t == 0 ? T : t == 1 ? T1 : nullptr;
                        if (A && idx >= 0) {
                            const int n = A->compCnt ? 2 : 1, size = TypeSize(A->compType);
                            const size_t base = A->data + size_t(idx) * size_t(A->stride ? A->stride : n * size);
                            double st[2] = {0, 0};
                            for (int k = 0; k < n; ++k) st[k] = Component(data, base + size_t(k * size), A->compType, A->shift, false);
                            const double* tm = mat.texMtx[t];
                            vx.uv[t][0] = tm[0] * st[0] + tm[1] * st[1] + tm[2];
                            vx.uv[t][1] = tm[3] * st[0] + tm[4] * st[1] + tm[5];
                            if (t == 0 && !T1) { vx.uv[1][0] = vx.uv[0][0]; vx.uv[1][1] = vx.uv[0][1]; }
                        }
                    }
                    verts.push_back(vx);
                }
                auto emit = [&](const Vertex& a, const Vertex& b, const Vertex& c) {
                    Triangle tri{{a, b, c}, draw.mat};
                    ((draw.xlu || mat.translucent || mat.blend) ? model.translucent : model.opaque).push_back(tri);
                };
                if (kind == 0x90) for (size_t i = 0; i + 2 < verts.size(); i += 3) emit(verts[i], verts[i + 1], verts[i + 2]);
                else if (kind == 0x98) for (size_t i = 0; i + 2 < verts.size(); ++i) { if (i % 2) emit(verts[i + 1], verts[i], verts[i + 2]); else emit(verts[i], verts[i + 1], verts[i + 2]); }
                else if (kind == 0xA0) for (size_t i = 1; i + 1 < verts.size(); ++i) emit(verts[0], verts[i], verts[i + 1]);
                else if (kind == 0x80 || kind == 0x88) for (size_t i = 0; i + 3 < verts.size(); i += 4) { emit(verts[i], verts[i + 1], verts[i + 2]); emit(verts[i], verts[i + 2], verts[i + 3]); }
            }
        }
    }
    if (model.opaque.empty() && model.translucent.empty()) Fail("The course model has no geometry.");
    return model;
}

// ---------------------------------------------------------------- KMP / KCL
struct Frame {
    double x0, z0, x1, z1;
    int width, height;
    double sx() const { return width / (x1 - x0); }
    double sz() const { return height / (z1 - z0); }
};

Frame CourseFrame(const Bytes& kmp, double margin = 3000.0) {
    const Buf b{kmp, 0};
    if (kmp.size() < 16 || std::memcmp(kmp.data(), "RKMD", 4) != 0) Fail("Invalid KMP");
    const uint16_t n = b.U16(8), header = b.U16(10);
    std::vector<std::array<double, 2>> pts;
    for (uint16_t i = 0; i < n; ++i) {
        const size_t start = header + b.U32(16 + 4 * size_t(i));
        const std::string tag(reinterpret_cast<const char*>(&kmp.at(start)), 4);
        const uint16_t count = b.U16(start + 4);
        const size_t body = start + 8;
        if (tag == "CKPT")
            for (uint16_t k = 0; k < count; ++k) {
                const size_t r = body + size_t(k) * 20;
                pts.push_back({b.F32(r), b.F32(r + 4)});
                pts.push_back({b.F32(r + 8), b.F32(r + 12)});
            }
        else if (tag == "ENPT" || tag == "ITPT" || tag == "JGPT" || tag == "KTPT") {
            const size_t stride = (tag == "ENPT" || tag == "ITPT") ? 20 : 28;
            for (uint16_t k = 0; k < count; ++k) { const size_t r = body + size_t(k) * stride; pts.push_back({b.F32(r), b.F32(r + 8)}); }
        }
    }
    if (pts.empty()) Fail("The course has no checkpoints.");
    double x0 = kInf, x1 = -kInf, z0 = kInf, z1 = -kInf;
    for (const auto& p : pts) { x0 = std::min(x0, p[0]); x1 = std::max(x1, p[0]); z0 = std::min(z0, p[1]); z1 = std::max(z1, p[1]); }
    x0 -= margin; x1 += margin; z0 -= margin; z1 += margin;
    const double w = x1 - x0, h = z1 - z0;
    int size[2] = {1024, 1024};
    if (w >= h * 1.6) size[1] = 512;
    else if (h >= w * 1.6) size[0] = 512;
    const double texel = std::max(w / size[0], h / size[1]);
    const double cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    return {cx - texel * size[0] / 2, cz - texel * size[1] / 2, cx + texel * size[0] / 2, cz + texel * size[1] / 2, size[0], size[1]};
}

struct KclTri { double a[3], b[3], c[3], face[3]; int kind; };

std::vector<KclTri> KclTriangles(const Bytes& kcl) {
    const Buf b{kcl, 0};
    const uint32_t posOff = b.U32(0), nrmOff = b.U32(4), prismOff = b.U32(8), blockOff = b.U32(12);
    auto vec = [&](uint32_t base, size_t i, double out[3]) { for (int k = 0; k < 3; ++k) out[k] = b.F32(base + i * 12 + size_t(k) * 4); };
    const size_t positions = (nrmOff - posOff) / 12, normals = ((prismOff + 0x10 - nrmOff) / 4) / 3;
    const size_t count = (blockOff - prismOff - 0x10) / 0x10;
    std::vector<KclTri> out;
    for (size_t i = 0; i < count; ++i) {
        const size_t r = prismOff + 0x10 + i * 0x10;
        const double h = b.F32(r);
        const uint16_t p = b.U16(r + 4), f = b.U16(r + 6), e1 = b.U16(r + 8), e2 = b.U16(r + 10), e3 = b.U16(r + 12), attr = b.U16(r + 14);
        if (p >= positions || f >= normals || e1 >= normals || e2 >= normals || e3 >= normals) continue;
        KclTri t;
        double face[3], n1[3], n2[3], n3[3];
        vec(posOff, p, t.a); vec(nrmOff, f, face); vec(nrmOff, e1, n1); vec(nrmOff, e2, n2); vec(nrmOff, e3, n3);
        auto cross = [](const double u[3], const double v[3], double o[3]) { o[0] = u[1] * v[2] - u[2] * v[1]; o[1] = u[2] * v[0] - u[0] * v[2]; o[2] = u[0] * v[1] - u[1] * v[0]; };
        double ca[3], cb[3];
        cross(n1, face, ca); cross(n2, face, cb);
        const double db = cb[0] * n3[0] + cb[1] * n3[1] + cb[2] * n3[2], da = ca[0] * n3[0] + ca[1] * n3[1] + ca[2] * n3[2];
        if (db == 0 || da == 0) continue;
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            t.b[k] = t.a[k] + cb[k] * (h / db);
            t.c[k] = t.a[k] + ca[k] * (h / da);
            ok = ok && std::isfinite(t.b[k]) && std::isfinite(t.c[k]);
        }
        if (!ok) continue;
        std::memcpy(t.face, face, sizeof(face));
        t.kind = attr & 0x1F;
        out.push_back(t);
    }
    return out;
}

bool IsRoad(int k) { static const int r[] = {0x00, 0x01, 0x05, 0x06, 0x07, 0x08, 0x09, 0x13, 0x15, 0x16, 0x17, 0x1D}; return std::find(std::begin(r), std::end(r), k) != std::end(r); }
bool IsFloor(int k) { return IsRoad(k) || k == 0x02 || k == 0x03 || k == 0x04 || k == 0x0A || k == 0x0B; }

using Grid = std::vector<double>;

// Visits pixel centres covered by a 2D triangle with barycentric weights.
template <typename Fn>
void Raster(const double xy[3][2], int width, int height, Fn&& fn) {
    const double ax = xy[0][0], ay = xy[0][1], bx = xy[1][0], by = xy[1][1], cx = xy[2][0], cy = xy[2][1];
    const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::fabs(area) < 1e-12) return;
    const int x0 = std::max(int(std::floor(std::min({ax, bx, cx}))), 0), x1 = std::min(int(std::ceil(std::max({ax, bx, cx}))), width - 1);
    const int y0 = std::max(int(std::floor(std::min({ay, by, cy}))), 0), y1 = std::min(int(std::ceil(std::max({ay, by, cy}))), height - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
            const double w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 >= -1e-9 && w1 >= -1e-9 && w2 >= -1e-9) fn(y, x, w0, w1, w2);
        }
}

Grid BoxBlur(const Grid& in, int w, int h, int radius) {
    Grid out = in;
    if (radius < 1) return out;
    const int k = 2 * radius + 1;
    Grid tmp(out.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) { // vertical pass first (axis 0), then horizontal
            (void)y;
        }
        // axis 0 (rows/vertical)
        for (int x = 0; x < w; ++x) {
            std::vector<double> c(size_t(h + k) + 1, 0.0);
            double acc = 0;
            for (int i = 0; i < h + k; ++i) {
                const int src = std::min(std::max(i - radius - 1, 0), h - 1);
                acc += out[size_t(src) * size_t(w) + size_t(x)];
                c[size_t(i) + 1] = acc;
            }
            for (int y = 0; y < h; ++y) tmp[size_t(y) * size_t(w) + size_t(x)] = (c[size_t(y + k) + 1] - c[size_t(y) + 1]) / k;
        }
        // axis 1 (columns/horizontal)
        for (int y = 0; y < h; ++y) {
            std::vector<double> c(size_t(w + k) + 1, 0.0);
            double acc = 0;
            for (int i = 0; i < w + k; ++i) {
                const int src = std::min(std::max(i - radius - 1, 0), w - 1);
                acc += tmp[size_t(y) * size_t(w) + size_t(src)];
                c[size_t(i) + 1] = acc;
            }
            for (int x = 0; x < w; ++x) out[size_t(y) * size_t(w) + size_t(x)] = (c[size_t(x + k) + 1] - c[size_t(x) + 1]) / k;
        }
    }
    return out;
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0;
    const size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(n), v.end());
    if (v.size() % 2) return v[n];
    const double hi = v[n];
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(n - 1), v.end());
    return (v[n - 1] + hi) / 2;
}

Image BakeCourse(const Model& model, const std::map<std::string, Texture>& textures, const std::vector<KclTri>& kcl, const Frame& f) {
    const int W = f.width, H = f.height;
    const double upp = (f.x1 - f.x0) / W;
    auto toPix = [&](const double p[3], double out[2]) { out[0] = (p[0] - f.x0) * f.sx(); out[1] = (p[2] - f.z0) * f.sz(); };
    // Floors from collision.
    Grid road(size_t(W) * size_t(H), -kInf), ground(road);
    for (const auto& t : kcl) {
        if (!(t.face[1] > 0.2) || !IsFloor(t.kind)) continue;
        double xy[3][2];
        toPix(t.a, xy[0]); toPix(t.b, xy[1]); toPix(t.c, xy[2]);
        const bool isRoad = IsRoad(t.kind);
        Raster(xy, W, H, [&](int y, int x, double w0, double w1, double w2) {
            const double hgt = w0 * t.a[1] + w1 * t.b[1] + w2 * t.c[1];
            double& g = ground[size_t(y) * size_t(W) + size_t(x)];
            g = std::max(g, hgt);
            if (isRoad) { double& r = road[size_t(y) * size_t(W) + size_t(x)]; r = std::max(r, hgt); }
        });
    }
    // Ceiling: CLEARANCE over the road beneath (dilated), capped at the top road far from ground.
    double top = -kInf;
    for (double r : road) if (std::isfinite(r)) top = std::max(top, r);
    const int dil = std::max(1, int(std::lround(150.0 / upp)));
    Grid ceiling(road.size(), -kInf);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double best = -kInf;
            for (int dy = -dil; dy <= dil; ++dy)
                for (int dx = -dil; dx <= dil; ++dx) {
                    if (dx * dx + dy * dy > dil * dil) continue;
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                    best = std::max(best, road[size_t(sy) * size_t(W) + size_t(sx)]);
                }
            ceiling[size_t(y) * size_t(W) + size_t(x)] = std::isfinite(best) ? best + 700.0 : kInf;
        }
    {
        Grid has(road.size());
        for (size_t i = 0; i < has.size(); ++i) has[i] = std::isfinite(ground[i]) ? 1 : 0;
        const Grid nearGround = BoxBlur(has, W, H, std::max(1, int(std::lround(1000.0 / upp))));
        for (size_t i = 0; i < ceiling.size(); ++i) if (!(nearGround[i] > 1e-4)) ceiling[i] = std::min(ceiling[i], top);
    }
    // Top-down render.
    std::vector<std::array<double, 3>> colour(road.size(), {0.10, 0.13, 0.18});
    Grid depth(road.size(), -kInf);
    const double light[3] = {-0.3 / 1.06, 0.95 / 1.06, -0.2 / 1.06};
    auto draw = [&](const Triangle& tri, bool blend) {
        const Material& mat = model.materials[tri.material];
        const Texture* tex[8] = {};
        double lod[8] = {};
        double xy[3][2];
        for (int k = 0; k < 3; ++k) toPix(tri.v[k].p, xy[k]);
        const double pixelArea = std::fabs((xy[1][0] - xy[0][0]) * (xy[2][1] - xy[0][1]) - (xy[1][1] - xy[0][1]) * (xy[2][0] - xy[0][0]));
        for (int m = 0; m < 8; ++m) {
            if (mat.samplers[m].name.empty()) continue;
            auto it = textures.find(mat.samplers[m].name);
            // Pattern-animated textures are stored as name.1, name.2...: use the first frame.
            if (it == textures.end()) it = textures.find(mat.samplers[m].name + ".1");
            if (it == textures.end()) continue;
            tex[m] = &it->second;
            const auto& a = tri.v[0].uv[0]; const auto& b = tri.v[1].uv[0]; const auto& c = tri.v[2].uv[0];
            const double texelArea = std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) * tex[m]->sizes[0].first * tex[m]->sizes[0].second;
            lod[m] = pixelArea <= 1e-9 ? double(tex[m]->levels.size() - 1) : std::max(0.0, 0.5 * std::log2(std::max(texelArea / pixelArea, 1e-9)));
        }
        Raster(xy, W, H, [&](int y, int x, double w0, double w1, double w2) {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            const double hgt = w0 * tri.v[0].p[1] + w1 * tri.v[1].p[1] + w2 * tri.v[2].p[1];
            if (!(hgt >= depth[i]) || !(hgt <= ceiling[i])) return;
            double vc[4], n[3];
            for (int k = 0; k < 4; ++k) vc[k] = w0 * tri.v[0].c[k] + w1 * tri.v[1].c[k] + w2 * tri.v[2].c[k];
            for (int k = 0; k < 3; ++k) n[k] = w0 * tri.v[0].n[k] + w1 * tri.v[1].n[k] + w2 * tri.v[2].n[k];
            double ras[4];
            for (int k = 0; k < 3; ++k) ras[k] = mat.colorFromVertex ? vc[k] : mat.matColor[k];
            ras[3] = mat.alphaFromVertex ? vc[3] : mat.matColor[3];
            if (mat.lit) {
                const double len = std::max(1e-6, std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
                const double d = std::max(0.0, (n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / len);
                for (int k = 0; k < 3; ++k) ras[k] *= std::min(1.0, (mat.ambFromVertex ? vc[k] : mat.ambColor[k]) + 0.75 * d);
            }
            double uv[2][2];
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 2; ++k) uv[c][k] = w0 * tri.v[0].uv[c][k] + w1 * tri.v[1].uv[c][k] + w2 * tri.v[2].uv[c][k];
            double rgb[3], alpha;
            if (mat.stages.empty()) {
                float t[4] = {1, 1, 1, 1};
                if (tex[0]) Sample(*tex[0], uv[0][0], uv[0][1], mat.samplers[0].wrapS, mat.samplers[0].wrapT, lod[0], t);
                for (int k = 0; k < 3; ++k) rgb[k] = std::min(1.0, std::max(0.0, t[k] * ras[k]));
                alpha = std::min(1.0, std::max(0.0, double(t[3]) * ras[3]));
            } else {
                // GX TEV: each stage computes D +/- lerp(A, B, C), with bias, scale and clamp.
                double reg[4][4];
                std::memcpy(reg, mat.regs, sizeof(reg));
                for (const TevStage& st : mat.stages) {
                    float t[4] = {1, 1, 1, 1};
                    if (st.texEnable && st.texMap < 8 && tex[st.texMap]) {
                        const int c = std::min(st.texCoord, 1);
                        Sample(*tex[st.texMap], uv[c][0], uv[c][1], mat.samplers[st.texMap].wrapS, mat.samplers[st.texMap].wrapT, lod[st.texMap], t);
                    }
                    const double zero[4] = {0, 0, 0, 0};
                    const double* r = st.chan == 7 ? zero : ras;
                    auto konstC = [&](int sel, int k) {
                        if (sel < 8) return (8 - sel) / 8.0;
                        if (sel >= 12 && sel < 16) return mat.konst[sel - 12][k];
                        if (sel >= 16) return mat.konst[(sel - 16) % 4][(sel - 16) / 4];
                        return 0.0;
                    };
                    auto colorIn = [&](int code, int k) -> double {
                        if (code < 8) return code % 2 == 0 ? reg[code / 2][k] : reg[code / 2][3];
                        switch (code) {
                            case 8: return t[k]; case 9: return t[3]; case 10: return r[k]; case 11: return r[3];
                            case 12: return 1; case 13: return 0.5; case 14: return konstC(st.kcsel, k); default: return 0;
                        }
                    };
                    auto alphaIn = [&](int code) -> double {
                        if (code < 4) return reg[code][3];
                        switch (code) { case 4: return t[3]; case 5: return r[3]; case 6: return konstC(st.kasel, 3); default: return 0; }
                    };
                    const double scales[4] = {1, 2, 4, 0.5}, biases[4] = {0, 0.5, -0.5, 0};
                    auto combine = [&](double a, double b, double c, double d, uint32_t env) {
                        const bool sub = (env >> 18) & 1, clamp = (env >> 19) & 1;
                        const int bias = (env >> 16) & 3, scale = (env >> 20) & 3;
                        double v = (a * (1 - c) + b * c);
                        v = (sub ? d - v : d + v) + biases[bias];
                        v *= scales[scale];
                        return clamp ? std::min(1.0, std::max(0.0, v)) : std::min(4.0, std::max(-4.0, v));
                    };
                    const uint32_t ce = st.colorEnv, ae = st.alphaEnv;
                    double outC[3];
                    for (int k = 0; k < 3; ++k)
                        outC[k] = combine(colorIn((ce >> 12) & 15, k), colorIn((ce >> 8) & 15, k), colorIn((ce >> 4) & 15, k), colorIn(ce & 15, k), ce);
                    const double outA = combine(alphaIn((ae >> 13) & 7), alphaIn((ae >> 10) & 7), alphaIn((ae >> 7) & 7), alphaIn((ae >> 4) & 7), ae);
                    const int cd = (ce >> 22) & 3, ad = (ae >> 22) & 3;
                    for (int k = 0; k < 3; ++k) reg[cd][k] = outC[k];
                    reg[ad][3] = outA;
                }
                for (int k = 0; k < 3; ++k) rgb[k] = std::min(1.0, std::max(0.0, reg[0][k]));
                alpha = std::min(1.0, std::max(0.0, reg[0][3]));
            }
            const int a8 = int(std::lround(alpha * 255));
            auto cmp = [&](int kind, int ref) {
                switch (kind) { case 0: return false; case 1: return a8 < ref; case 2: return a8 == ref; case 3: return a8 <= ref;
                                case 4: return a8 > ref; case 5: return a8 != ref; case 6: return a8 >= ref; default: return true; }
            };
            const bool ta = cmp(mat.alphaComp0, mat.alphaRef0), tb = cmp(mat.alphaComp1, mat.alphaRef1);
            const bool pass = mat.alphaOp == 0 ? (ta && tb) : mat.alphaOp == 1 ? (ta || tb) : mat.alphaOp == 2 ? (ta != tb) : (ta == tb);
            if (!pass) return;
            // Translucent-list materials with blending off are drawn opaque, as GX does.
            if (blend && mat.blend) {
                for (int k = 0; k < 3; ++k) colour[i][size_t(k)] = std::min(1.0, std::max(0.0, rgb[k] * alpha + colour[i][size_t(k)] * (mat.additive ? 1 : 1 - alpha)));
                return;
            }
            for (int k = 0; k < 3; ++k) colour[i][size_t(k)] = rgb[k];
            depth[i] = hgt;
        });
    };
    for (const auto& tri : model.opaque) draw(tri, false);
    std::vector<const Triangle*> blended;
    for (const auto& tri : model.translucent) blended.push_back(&tri);
    std::stable_sort(blended.begin(), blended.end(), [](const Triangle* a, const Triangle* b) {
        return (a->v[0].p[1] + a->v[1].p[1] + a->v[2].p[1]) < (b->v[0].p[1] + b->v[1].p[1] + b->v[2].p[1]);
    });
    for (const Triangle* tri : blended) draw(*tri, true);

    // Finish: occlusion and relief from the height field, soft shadows, saturation, surround.
    std::vector<double> groundVals;
    for (double g : ground) if (std::isfinite(g)) groundVals.push_back(g);
    const double groundHeight = Median(groundVals);
    Grid h(road.size()), covered(road.size());
    for (size_t i = 0; i < h.size(); ++i) { covered[i] = std::isfinite(depth[i]) ? 1 : 0; h[i] = covered[i] ? depth[i] : groundHeight; }
    Grid ao(h.size(), 1.0);
    for (const auto& [radiusWorld, strength] : {std::pair<double, double>{350.0, 0.45}, {1400.0, 0.30}}) {
        const Grid blurred = BoxBlur(h, W, H, std::max(1, int(std::lround(radiusWorld / upp))));
        for (size_t i = 0; i < h.size(); ++i) ao[i] -= strength * std::min(1.0, std::max(0.0, std::max(0.0, blurred[i] - h[i]) / (radiusWorld * 1.2)));
    }
    const Grid hb = BoxBlur(h, W, H, std::max(1, int(std::lround(120.0 / upp))));
    const double lx = -0.45, ly = 0.80, lz = -0.40, ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    Grid relief(h.size());
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            auto at = [&](int xx, int yy) { return hb[size_t(yy) * size_t(W) + size_t(xx)]; };
            const double gx = W == 1 ? 0 : (x == 0 ? (at(1, y) - at(0, y)) : x == W - 1 ? (at(x, y) - at(x - 1, y)) : (at(x + 1, y) - at(x - 1, y)) / 2) / upp;
            const double gy = H == 1 ? 0 : (y == 0 ? (at(x, 1) - at(x, 0)) : y == H - 1 ? (at(x, y) - at(x, y - 1)) : (at(x, y + 1) - at(x, y - 1)) / 2) / upp;
            double n[3] = {-gx, 1, -gy};
            const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            const double dot = (n[0] * lx + n[1] * ly + n[2] * lz) / (nl * ll);
            relief[size_t(y) * size_t(W) + size_t(x)] = 0.82 + 0.18 * std::min(1.25, std::max(0.0, dot / (ly / ll)));
        }
    const int shift = std::max(1, int(std::lround(220.0 / upp)));
    Grid shadow(h.size(), 0.0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (y < shift || x < shift) continue;
            const double raised = h[size_t(y - shift) * size_t(W) + size_t(x - shift)];
            shadow[size_t(y) * size_t(W) + size_t(x)] = std::min(1.0, std::max(0.0, (raised - h[size_t(y) * size_t(W) + size_t(x)] - 250.0) / 900.0));
        }
    shadow = BoxBlur(shadow, W, H, std::max(1, int(std::lround(180.0 / upp))));
    std::vector<std::array<double, 3>> out(h.size());
    for (size_t i = 0; i < h.size(); ++i) {
        const double s = std::min(1.0, std::max(0.35, ao[i])) * relief[i] * (1 - 0.35 * shadow[i]);
        std::array<double, 3> c = {colour[i][0] * s, colour[i][1] * s, colour[i][2] * s};
        const double mean = (c[0] + c[1] + c[2]) / 3;
        for (double& v : c) v = std::pow(std::min(1.0, std::max(0.0, mean + (v - mean) * 1.12)), 0.92);
        out[i] = c;
    }
    std::array<double, 3> background = {0.10, 0.13, 0.18};
    {
        const Grid near = BoxBlur(covered, W, H, std::max(2, W / 128));
        std::vector<double> ch[3];
        for (size_t i = 0; i < h.size(); ++i)
            if (covered[i] > 0 && near[i] < 0.9) for (int k = 0; k < 3; ++k) ch[k].push_back(out[i][size_t(k)]);
        if (!ch[0].empty()) for (int k = 0; k < 3; ++k) background[size_t(k)] = 0.42 * Median(ch[k]);
    }
    const Grid soft = BoxBlur(covered, W, H, std::max(2, W / 64)), edge = BoxBlur(covered, W, H, 2);
    Image image(W, H);
    for (size_t i = 0; i < h.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const double surround = background[size_t(k)] * (0.8 + 0.2 * soft[i]);
            const double v = out[i][size_t(k)] * edge[i] + surround * (1 - edge[i]);
            image.rgba[i * 4 + size_t(k)] = uint8_t(std::min(1.0, std::max(0.0, v)) * 255 + 0.5);
            image.rgba[i * 4 + 3] = 255;
        }
    return image;
}

struct Course { int id; const char* archive; const char* name; };
const Course kCourses[32] = {
    {0, "castle_course", "Mario Circuit"}, {1, "farm_course", "Moo Moo Meadows"}, {2, "kinoko_course", "Mushroom Gorge"},
    {3, "volcano_course", "Grumble Volcano"}, {4, "factory_course", "Toad's Factory"}, {5, "shopping_course", "Coconut Mall"},
    {6, "boardcross_course", "DK Summit"}, {7, "truck_course", "Wario's Gold Mine"}, {8, "beginner_course", "Luigi Circuit"},
    {9, "senior_course", "Daisy Circuit"}, {10, "ridgehighway_course", "Moonview Highway"}, {11, "treehouse_course", "Maple Treeway"},
    {12, "koopa_course", "Bowser's Castle"}, {13, "rainbow_course", "Rainbow Road"}, {14, "desert_course", "Dry Dry Ruins"},
    {15, "water_course", "Koopa Cape"}, {16, "old_peach_gc", "GCN Peach Beach"}, {17, "old_mario_gc", "GCN Mario Circuit"},
    {18, "old_waluigi_gc", "GCN Waluigi Stadium"}, {19, "old_donkey_gc", "GCN DK Mountain"}, {20, "old_falls_ds", "DS Yoshi Falls"},
    {21, "old_desert_ds", "DS Desert Hills"}, {22, "old_garden_ds", "DS Peach Gardens"}, {23, "old_town_ds", "DS Delfino Square"},
    {24, "old_mario_sfc", "SNES Mario Circuit 3"}, {25, "old_obake_sfc", "SNES Ghost Valley 2"}, {26, "old_mario_64", "N64 Mario Raceway"},
    {27, "old_sherbet_64", "N64 Sherbet Land"}, {28, "old_koopa_64", "N64 Bowser's Castle"}, {29, "old_donkey_64", "N64 DK's Jungle Parkway"},
    {30, "old_koopa_gba", "GBA Bowser Castle 3"}, {31, "old_heyho_gba", "GBA Shy Guy Beach"}};

} // namespace

std::string BuildMarioKartMap(const std::string& discRoot, int id, const std::string& out) {
    if (id < 0 || id >= 32) Fail("No such course");
    const Course& course = kCourses[id];
    const std::string relative = std::string("Race/Course/") + course.archive + ".szs";
    const Bytes szs = ReadFile(discRoot + "/files/" + relative);
    const Archive archive = OpenArchive(szs);
    std::map<std::string, Texture> textures;
    const Model model = ParseCourseModel(Member(archive, "course_model.brres"), textures);
    const Frame frame = CourseFrame(Member(archive, "course.kmp"));
    const auto kcl = KclTriangles(Member(archive, "course.kcl"));
    const Image image = BakeCourse(model, textures, kcl, frame);
    std::filesystem::create_directories(std::filesystem::u8path(out));
    char stem[32];
    std::snprintf(stem, sizeof(stem), "course_%02d", id);
    const Bytes png = EncodePng(image);
    WriteFile(out + "/" + stem + ".png", png.data(), png.size());
    Json frameJson = Json::Array();
    for (double v : {frame.x0, frame.z0, frame.x1, frame.z1}) frameJson.Push(Json::Real(v));
    Json size = Json::Array();
    size.Push(Json::Num(frame.width)).Push(Json::Num(frame.height));
    const std::string text = Json::Object().Set("id", Json::Num(id)).Set("name", Json::Str(course.name)).Set("frame", std::move(frameJson))
        .Set("size", std::move(size)).Set("source", Json::Str(relative)).Set("source_sha256", Json::Str(Sha256Hex(szs)))
        .Set("png_sha256", Json::Str(Sha256Hex(png))).Set("triangles", Json::Num(long(model.opaque.size() + model.translucent.size())))
        .Set("baker", Json::Str("WiiDS on-device map baker")).Dump() + "\n";
    WriteFile(out + "/" + stem + ".json", text.data(), text.size());
    return std::string(course.name) + " map built";
}

std::string BuildMarioKartMaps(const std::string& discRoot, const std::string& out) {
    std::string failures;
    int built = 0;
    for (int id = 0; id < 32; ++id) {
        try { BuildMarioKartMap(discRoot, id, out); ++built; }
        catch (const std::exception& e) { failures += std::string(kCourses[id].name) + ": " + e.what() + "; "; }
    }
    if (!built) Fail("No course map could be built. " + failures);
    return std::to_string(built) + " course maps built" + (failures.empty() ? "" : " (" + failures + ")");
}

} // namespace wiids::art

namespace wiids::art {
namespace {

// The receipt and manifest fields ThorRaceMapView checks for a Retro Rewind course.
Json RetroRecord(const std::string& key, const std::string& sha, const Frame& f, const std::string& pngSha) {
    Json frameJson = Json::Array();
    for (double v : {f.x0, f.z0, f.x1, f.z1}) frameJson.Push(Json::Real(v));
    Json size = Json::Array();
    size.Push(Json::Num(f.width)).Push(Json::Num(f.height));
    return Json::Object().Set("schema", Json::Num(1)).Set("profile", Json::Str("retro_rewind")).Set("key", Json::Str(key))
        .Set("source_sha256", Json::Str(sha)).Set("archive_name", Json::Str(key + ".szs"))
        .Set("dvd_path", Json::Str("/Race/Course/" + key + ".szs")).Set("png", Json::Str("course_" + key + ".png"))
        .Set("receipt", Json::Str("course_" + key + ".json")).Set("png_sha256", Json::Str(pngSha))
        .Set("frame", std::move(frameJson)).Set("size", std::move(size));
}

} // namespace

std::string BuildRetroRewindMaps(const std::string& packRoot, const std::string& out, const std::function<bool()>& cancelled) {
    namespace fs = std::filesystem;
    const std::string codeSha = Sha256Hex(ReadFile(packRoot + "/Binaries/Code.pul"));
    fs::create_directories(fs::u8path(out));
    // Previous receipts let an update rebake only changed tracks.
    struct Done { std::string sha, pngSha; Frame frame; };
    Json courses = Json::Object();
    int built = 0, reused = 0, failed = 0;
    auto writeManifest = [&]() {
        const std::string text = Json::Object().Set("schema", Json::Num(1)).Set("profile", Json::Str("retro_rewind"))
            .Set("code_pul_sha256", Json::Str(codeSha)).Set("courses", courses).Dump() + "\n";
        WriteFile(out + "/maps.json.part", text.data(), text.size());
        fs::rename(fs::u8path(out + "/maps.json.part"), fs::u8path(out + "/maps.json"));
    };
    for (const char* folder : {"Tracks", "CT/Tracks", "BT/Tracks"}) {
        const fs::path dir = fs::u8path(packRoot + "/" + folder);
        if (!fs::is_directory(dir)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".szs") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            if (cancelled && cancelled()) { writeManifest(); return "cancelled"; }
            const std::string key = path.stem().u8string();
            if (key.empty() || key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) continue;
            try {
                const Bytes szs = ReadFile(path.u8string());
                const std::string sha = Sha256Hex(szs);
                const std::string receipt = out + "/course_" + key + ".json", png = out + "/course_" + key + ".png";
                // Reuse a current receipt (same source archive) as is.
                if (fs::is_regular_file(fs::u8path(receipt)) && fs::is_regular_file(fs::u8path(png))) {
                    const Bytes old = ReadFile(receipt);
                    const std::string text(old.begin(), old.end());
                    if (text.find("\"source_sha256\": \"" + sha + "\"") != std::string::npos) {
                        const Bytes pngBytes = ReadFile(png);
                        // Re-read frame/size from the receipt text.
                        const size_t f = text.find("\"frame\": [");
                        const size_t z = text.find("\"size\": [");
                        if (f != std::string::npos && z != std::string::npos) {
                            Frame fr{};
                            std::sscanf(text.c_str() + f, "\"frame\": [ %lf , %lf , %lf , %lf", &fr.x0, &fr.z0, &fr.x1, &fr.z1);
                            std::sscanf(text.c_str() + z, "\"size\": [ %d , %d", &fr.width, &fr.height);
                            courses.Set(key, RetroRecord(key, sha, fr, Sha256Hex(pngBytes)));
                            ++reused;
                            continue;
                        }
                    }
                }
                const Archive archive = OpenArchive(szs);
                std::map<std::string, Texture> textures;
                const Model model = ParseCourseModel(Member(archive, "course_model.brres"), textures);
                const Frame frame = CourseFrame(Member(archive, "course.kmp"));
                const auto kcl = KclTriangles(Member(archive, "course.kcl"));
                const Bytes pngBytes = EncodePng(BakeCourse(model, textures, kcl, frame));
                WriteFile(png, pngBytes.data(), pngBytes.size());
                Json record = RetroRecord(key, sha, frame, Sha256Hex(pngBytes));
                const std::string text = record.Dump() + "\n";
                WriteFile(receipt, text.data(), text.size());
                courses.Set(key, std::move(record));
                ++built;
            } catch (const std::exception&) {
                ++failed; // no map: the lower screen shows its standings without one
            }
            writeManifest();
        }
    }
    writeManifest();
    return std::to_string(built) + " built, " + std::to_string(reused) + " current, " + std::to_string(failed) + " without a map";
}

} // namespace wiids::art
