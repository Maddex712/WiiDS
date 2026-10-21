// NW4R layout (BRLYT/BRLAN/BRCTR) baking for the Mario Kart menu and HUD art, ported from
// reference/mkw-3ds/tools/menu_layout.py render_layout (and cook_race_hud animation/sample).
// numpy's float32/float64 promotion (NEP 50, numpy 2) and Pillow's integer alpha_composite are
// reproduced so the baked pixels match the PC tool.
#include "wiids_art_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace wiids::art {
namespace {

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }

uint16_t Be16(const Bytes& d, size_t at) {
    if (at + 2 > d.size()) Fail("Damaged layout");
    return uint16_t(d[at] << 8 | d[at + 1]);
}
uint32_t Be32(const Bytes& d, size_t at) {
    if (at + 4 > d.size()) Fail("Damaged layout");
    return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}
int16_t S16(const Bytes& d, size_t at) { return int16_t(Be16(d, at)); }
float F32(const Bytes& d, size_t at) { const uint32_t v = Be32(d, at); float f; std::memcpy(&f, &v, 4); return f; }
uint8_t U8(const Bytes& d, size_t at) { if (at >= d.size()) Fail("Damaged layout"); return d[at]; }

std::string Str(const Bytes& d, size_t at) {
    if (at >= d.size()) Fail("Damaged layout");
    size_t end = at;
    while (end < d.size() && d[end]) ++end;
    return std::string(d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(end));
}

struct Section { std::string tag; size_t block, start; };
std::vector<Section> Sections(const Bytes& d) {
    std::vector<Section> out;
    size_t offset = Be16(d, 12);
    while (offset < d.size()) {
        const uint32_t size = Be32(d, offset + 4);
        if (size < 8 || offset + size > d.size()) Fail("Invalid NW4R section");
        out.push_back({std::string(reinterpret_cast<const char*>(&d[offset]), 4), offset, offset + 8});
        offset += size;
    }
    return out;
}

// ---------------------------------------------------------------- animation (cook_race_hud)
struct Key { double frame, value, slope; };
using TrackKey = std::tuple<std::string, std::string, int>;

std::map<TrackKey, std::vector<Key>> Animation(const Bytes& d) {
    std::map<TrackKey, std::vector<Key>> tracks;
    for (const Section& s : Sections(d)) {
        if (s.tag != "pai1") continue;
        const uint16_t count = Be16(d, s.start + 6);
        const uint32_t table = Be32(d, s.start + 8);
        for (uint32_t i = 0; i < count; ++i) {
            const size_t bind = s.block + Be32(d, s.block + table + 4 * i);
            const std::string name = Str(d, bind);
            for (uint32_t j = 0; j < U8(d, bind + 20); ++j) {
                const size_t anim = bind + Be32(d, bind + 24 + 4 * j);
                const std::string kind(reinterpret_cast<const char*>(&d.at(anim)), 4);
                for (uint32_t k = 0; k < U8(d, anim + 4); ++k) {
                    const size_t track = anim + Be32(d, anim + 8 + 4 * k);
                    const int target = U8(d, track + 1), curve = U8(d, track + 2);
                    const uint16_t keys = Be16(d, track + 4);
                    const size_t frames = track + Be32(d, track + 8);
                    std::vector<Key> list;
                    for (uint32_t n = 0; n < keys; ++n) {
                        if (curve == 2) {
                            const size_t at = frames + size_t(n) * 12;
                            list.push_back({F32(d, at), F32(d, at + 4), F32(d, at + 8)});
                        } else {
                            const size_t at = frames + size_t(n) * 8;
                            list.push_back({F32(d, at), double(Be16(d, at + 4)), double(Be16(d, at + 6))});
                        }
                    }
                    tracks[{name, kind, target}] = std::move(list);
                }
            }
        }
    }
    return tracks;
}

double Sample(const std::vector<Key>& keys, double frame) {
    if (frame < keys[0].frame) return keys[0].value;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (frame < keys[i].frame) {
            const Key& a = keys[i - 1];
            const Key& b = keys[i];
            const double span = b.frame - a.frame, t = (frame - a.frame) / span;
            const double t2 = t * t, t3 = t2 * t;
            return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * a.slope * span +
                   (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * b.slope * span;
        }
    }
    return keys.back().value;
}

// ---------------------------------------------------------------- numpy-typed scalars
// One channel value with its numpy dtype: float32 results are rounded to float32.
struct V {
    double v;
    bool f32;
};
V F64(double v) { return {v, false}; }
V F32v(double v) { return {double(float(v)), true}; }
V Round(double v, bool f32) { return f32 ? V{double(float(v)), true} : V{v, false}; }
V Add(V a, V b) { return Round(a.v + b.v, a.f32 && b.f32); }
V Sub(V a, V b) { return Round(a.v - b.v, a.f32 && b.f32); }
V Mul(V a, V b) { return Round(a.v * b.v, a.f32 && b.f32); }
V Scalar(V a, double s, char op) { // array op python scalar: keeps the array's dtype
    switch (op) {
        case '+': return Round(a.v + s, a.f32);
        case '*': return Round(a.v * s, a.f32);
        default: return Round(s - a.v, a.f32); // s - array
    }
}

struct Material {
    std::string name;
    float regs[3][4];   // float32(raw)/255 in float32
    float constants[4][4];
    std::vector<std::pair<uint16_t, uint16_t>> samplers;
    double matrix[5];
    std::vector<std::array<uint8_t, 16>> stages;
    bool vertex = true;
    float matcol[4] = {1, 1, 1, 1};
    uint8_t alphaCompare[4] = {0x77, 0, 0, 0};
};

struct Pane {
    std::string name;
    double m[3][3];
    double alpha, left, top, w, h;
    float colors[4][4]; // float32(raw)/255
    size_t material;
    double uv[8];
    double z;
    double rect[4];
    bool draw;
};

struct Pose { double m[3][3]; double alpha; bool visible; double z; };

void MatMul(const double a[3][3], const double b[3][3], double out[3][3]) {
    double r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    std::memcpy(out, r, sizeof(r));
}

// Pillow ImagingAlphaComposite (libImaging/AlphaComposite.c).
void AlphaComposite(uint8_t* dst, const uint8_t* src) {
    if (src[3] == 0) return;
    constexpr uint32_t P = 7;
    const uint32_t blend = uint32_t(dst[3]) * (255 - src[3]);
    const uint32_t outa255 = uint32_t(src[3]) * 255 + blend;
    const uint32_t coef1 = uint32_t(src[3]) * 255 * 255 * (1u << P) / outa255;
    const uint32_t coef2 = 255 * (1u << P) - coef1;
    auto div255 = [](uint32_t a) { return ((a >> 8) + a) >> 8; };
    for (int c = 0; c < 3; ++c) {
        const uint32_t tmp = uint32_t(src[c]) * coef1 + uint32_t(dst[c]) * coef2;
        dst[c] = uint8_t(div255(tmp + (0x80u << P)) >> P);
    }
    dst[3] = uint8_t(div255(outa255 + 0x80));
}

struct Texture { int w = 0, h = 0; std::vector<float> px; }; // float32 RGBA / 255

double Wrap(double c, int mode) {
    if (mode == 1) { double r = std::fmod(c, 1.0); if (r < 0) r += 1.0; return r; }
    if (mode == 2) { double r = std::fmod(c, 2.0); if (r < 0) r += 2.0; return r <= 1 ? r : 2 - r; }
    return std::min(std::max(c, 0.0), 1.0);
}

void SampleTexture(const Texture& t, double u, double v, uint16_t sampler, double out[4]) {
    const int ws = (sampler >> 8) & 3, wt = sampler & 3;
    u = Wrap(u, ws); v = Wrap(v, wt);
    const double x = u * t.w - .5, y = v * t.h - .5;
    const double fx0 = std::floor(x), fy0 = std::floor(y);
    const long x0 = long(fx0), y0 = long(fy0);
    const double fx = x - fx0, fy = y - fy0;
    auto at = [&](long xx, long yy, int c) {
        if (ws == 1) { xx %= t.w; if (xx < 0) xx += t.w; }
        if (wt == 1) { yy %= t.h; if (yy < 0) yy += t.h; }
        xx = std::min<long>(std::max<long>(xx, 0), t.w - 1);
        yy = std::min<long>(std::max<long>(yy, 0), t.h - 1);
        return double(t.px[(size_t(yy) * size_t(t.w) + size_t(xx)) * 4 + size_t(c)]);
    };
    for (int c = 0; c < 4; ++c)
        out[c] = (at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx) * (1 - fy) +
                 (at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx) * fy;
}

} // namespace

std::string ArchivePath(const std::string& path) {
    std::string out = path;
    while (out.compare(0, 2, "./") == 0) out.erase(0, 2);
    return out;
}

Archive OpenArchive(const Bytes& szs) {
    const Bytes raw = szs.size() >= 4 && std::memcmp(szs.data(), "Yaz0", 4) == 0 ? Yaz0Decompress(szs) : szs;
    Archive out;
    for (auto& [name, data] : U8Files(raw)) out[ArchivePath(name)] = std::move(data);
    return out;
}

const Bytes& Member(const Archive& archive, const std::string& path) {
    const auto it = archive.find(path);
    if (it == archive.end()) Fail("The disc is missing " + path);
    return it->second;
}

ControllerBindings ControllerAnimations(const Bytes& d) {
    ControllerBindings out;
    const uint16_t offset = Be16(d, 12), strings = Be16(d, 16);
    const uint16_t count = Be16(d, offset + 2);
    const size_t table = offset + 8 + size_t(count) * 8;
    out.layout = Str(d, strings + Be16(d, 6));
    for (uint16_t i = 0; i < count; ++i) {
        const size_t at = offset + 8 + size_t(i) * 8;
        ControllerBindings::Group g;
        g.name = Str(d, strings + Be16(d, at));
        g.panes = Str(d, strings + Be16(d, at + 2));
        const uint16_t first = Be16(d, at + 4), length = Be16(d, at + 6);
        for (uint32_t k = first; k < uint32_t(first) + length; ++k) {
            const size_t e = table + size_t(k) * 12;
            g.animations.push_back({Str(d, strings + Be16(d, e)), Str(d, strings + Be16(d, e + 2))});
        }
        out.groups.push_back(std::move(g));
    }
    return out;
}

Image RenderLayout(const Archive& archive, const LayoutRequest& request, double outBounds[4]) {
    const Bytes& d = Member(archive, request.layout);
    const std::string root = request.layout.substr(0, request.layout.rfind('/'));
    const std::string timg = root.substr(0, root.rfind('/')) + "/timg/";
    const auto sections = Sections(d);

    std::map<std::string, std::vector<std::string>> groups;
    std::vector<std::string> materialNames;
    std::map<std::string, std::string> paneMaterials;
    for (const Section& s : sections) {
        if (s.tag == "grp1") {
            std::vector<std::string> members;
            for (uint32_t i = 0; i < Be16(d, s.start + 16); ++i) members.push_back(Str(d, s.start + 20 + size_t(i) * 16));
            groups[Str(d, s.start)] = members;
        } else if (s.tag == "mat1") {
            materialNames.clear();
            for (uint32_t i = 0; i < Be16(d, s.start); ++i) materialNames.push_back(Str(d, s.block + Be32(d, s.start + 4 + 4 * i)));
        } else if (s.tag == "pic1" || s.tag == "txt1" || s.tag == "wnd1") {
            const size_t content = s.tag == "pic1" ? s.start + 68 : s.tag == "wnd1" ? s.block + Be32(d, s.start + 88) : s.start + 56;
            paneMaterials[Str(d, s.start + 4)] = materialNames.at(Be16(d, content + 16));
        }
    }
    std::map<TrackKey, std::vector<Key>> tracks;
    for (size_t index = 0; index < request.animations.size(); ++index) {
        auto incoming = Animation(Member(archive, request.animations[index]));
        if (index < request.animationGroups.size() && !request.animationGroups[index].empty()) {
            const auto& members = groups.at(request.animationGroups[index]);
            std::vector<std::string> allowed(members);
            for (const auto& p : members)
                if (paneMaterials.count(p)) allowed.push_back(paneMaterials[p]);
            for (auto it = incoming.begin(); it != incoming.end();)
                it = std::find(allowed.begin(), allowed.end(), std::get<0>(it->first)) == allowed.end() ? incoming.erase(it) : std::next(it);
        }
        for (auto& [key, keys] : incoming) tracks[key] = std::move(keys);
    }
    const double frame = request.frame;
    auto value = [&](const std::string& name, const char* kind, int target, double fallback) {
        const auto it = tracks.find({name, kind, target});
        return it == tracks.end() ? fallback : Sample(it->second, frame);
    };

    std::vector<std::string> textures;
    std::vector<Material> materials;
    std::vector<Pane> panes;
    std::vector<Pose> stack;
    Pose pose{};
    for (const Section& s : sections) {
        if (s.tag == "txl1") {
            const size_t table = s.start + 4;
            textures.clear();
            for (uint32_t i = 0; i < Be16(d, s.start); ++i) textures.push_back(Str(d, table + Be32(d, table + 8 * i)));
        } else if (s.tag == "mat1") {
            for (uint32_t i = 0; i < Be16(d, s.start); ++i) {
                const size_t o = s.block + Be32(d, s.start + 4 + 4 * i);
                Material m;
                m.name = Str(d, o);
                float regs[12];
                for (int c = 0; c < 12; ++c) regs[c] = float(S16(d, o + 20 + 2 * size_t(c)));
                const auto ov = request.materialOverrides.find(m.name);
                if (ov != request.materialOverrides.end())
                    for (int c = 0; c < 12; ++c) regs[c] = float(ov->second[c / 4][c % 4]);
                for (int c = 0; c < 12; ++c) regs[c] = float(value(m.name, "RLMC", c + 4, regs[c]));
                for (int c = 0; c < 12; ++c) m.regs[c / 4][c % 4] = regs[c] / 255.0f;
                for (int c = 0; c < 16; ++c) m.constants[c / 4][c % 4] = float(U8(d, o + 44 + size_t(c))) / 255.0f;
                const uint32_t flags = Be32(d, o + 60);
                for (uint32_t j = 0; j < (flags & 15); ++j) m.samplers.push_back({Be16(d, o + 64 + 4 * j), Be16(d, o + 66 + 4 * j)});
                size_t off = o + 64 + m.samplers.size() * 4;
                if ((flags >> 4) & 15) for (int c = 0; c < 5; ++c) m.matrix[c] = F32(d, off + 4 * size_t(c));
                else { const double def[5] = {0, 0, 0, 1, 1}; std::memcpy(m.matrix, def, sizeof(def)); }
                for (int c = 0; c < 5; ++c) m.matrix[c] = value(m.name, "RLTS", c, m.matrix[c]);
                off += ((flags >> 4) & 15) * 20 + ((flags >> 8) & 15) * 4;
                if (flags & (1u << 25)) { m.vertex = U8(d, off) == 1 || U8(d, off + 1) == 1; off += 4; }
                if (flags & (1u << 27)) { for (int c = 0; c < 4; ++c) m.matcol[c] = float(U8(d, off + size_t(c))) / 255.0f; off += 4; }
                if (flags & (1u << 12)) off += 4;
                off += ((flags >> 13) & 3) * 20 + ((flags >> 15) & 7) * 4;
                for (uint32_t j = 0; j < ((flags >> 18) & 31); ++j) {
                    std::array<uint8_t, 16> stage{};
                    for (int c = 0; c < 16; ++c) stage[c] = U8(d, off + 16 * j + size_t(c));
                    m.stages.push_back(stage);
                }
                off += m.stages.size() * 16;
                if (flags & (1u << 23)) { for (int c = 0; c < 4; ++c) m.alphaCompare[c] = U8(d, off + size_t(c)); }
                materials.push_back(std::move(m));
            }
        } else if (s.tag == "pan1" || s.tag == "pic1" || s.tag == "txt1" || s.tag == "bnd1" || s.tag == "wnd1") {
            const std::string name = Str(d, s.start + 4);
            double v[10];
            for (int i = 0; i < 10; ++i) v[i] = value(name, "RLPA", i, F32(d, s.start + 28 + 4 * size_t(i)));
            double alpha = value(name, "RLVC", 16, U8(d, s.start + 2)) / 255;
            bool visible = value(name, "RLVI", 0, U8(d, s.start) & 1) != 0;
            if (U8(d, s.start) & 4) { v[6] *= request.aspect[0]; v[7] *= request.aspect[1]; }
            const double rad = v[5] * (3.141592653589793 / 180.0);
            const double co = std::cos(rad), si = std::sin(rad);
            double m[3][3] = {{co * v[6], -si * v[7], v[0]}, {si * v[6], co * v[7], v[1]}, {0, 0, 1}};
            if (!stack.empty()) {
                MatMul(stack.back().m, m, m);
                alpha *= stack.back().alpha;
                visible = visible && stack.back().visible;
            }
            const double z = v[2] + (stack.empty() ? 0 : stack.back().z);
            std::memcpy(pose.m, m, sizeof(m));
            pose.alpha = (U8(d, s.start) & 2) ? alpha : (stack.empty() ? 1 : stack.back().alpha);
            pose.visible = visible;
            pose.z = z;
            const int base = U8(d, s.start + 1);
            const double left = -(base % 3) * v[8] / 2, top = (base / 3) * v[9] / 2;
            const double cx[4] = {left, left + v[8], left, left + v[8]}, cy[4] = {top, top, top - v[9], top - v[9]};
            double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300;
            for (int k = 0; k < 4; ++k) {
                const double px = m[0][0] * cx[k] + m[0][1] * cy[k] + m[0][2] * 1;
                const double py = m[1][0] * cx[k] + m[1][1] * cy[k] + m[1][2] * 1;
                minx = std::min(minx, px); maxx = std::max(maxx, px); miny = std::min(miny, py); maxy = std::max(maxy, py);
            }
            if (s.tag != "pic1" && s.tag != "wnd1") continue;
            const size_t content = s.tag == "pic1" ? s.start + 68 : s.block + Be32(d, s.start + 88);
            Pane p;
            p.name = name;
            for (int i = 0; i < 16; ++i) p.colors[i / 4][i % 4] = float(value(name, "RLVC", i, U8(d, content + size_t(i)))) / 255.0f;
            p.material = Be16(d, content + 16);
            if (!visible || alpha <= 0) continue;
            p.draw = request.include.empty() || std::find(request.include.begin(), request.include.end(), name) != request.include.end();
            if (U8(d, content + 18)) for (int i = 0; i < 8; ++i) p.uv[i] = F32(d, content + 20 + 4 * size_t(i));
            else { const double def[8] = {0, 0, 1, 0, 0, 1, 1, 1}; std::memcpy(p.uv, def, sizeof(def)); }
            std::memcpy(p.m, m, sizeof(m));
            p.alpha = alpha; p.left = left; p.top = top; p.w = v[8]; p.h = v[9]; p.z = z;
            p.rect[0] = minx; p.rect[1] = -maxy; p.rect[2] = maxx; p.rect[3] = -miny;
            panes.push_back(p);
        } else if (s.tag == "pas1") {
            stack.push_back(pose);
        } else if (s.tag == "pae1") {
            if (stack.empty()) Fail("Damaged layout");
            stack.pop_back();
        }
    }

    double b[4];
    if (request.hasBounds) {
        std::memcpy(b, request.bounds, sizeof(b));
    } else {
        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        bool any = false;
        for (const Pane& p : panes) {
            if (!p.draw) continue;
            any = true;
            minx = std::min(minx, p.rect[0]); miny = std::min(miny, p.rect[1]);
            maxx = std::max(maxx, p.rect[2]); maxy = std::max(maxy, p.rect[3]);
        }
        if (!any) Fail("Layout has no visible picture panes: " + request.layout);
        b[0] = std::floor(minx) - 2; b[1] = std::floor(miny) - 2; b[2] = std::ceil(maxx) + 2; b[3] = std::ceil(maxy) + 2;
    }
    std::memcpy(outBounds, b, sizeof(b));
    Image out(int(std::nearbyint(b[2] - b[0])), int(std::nearbyint(b[3] - b[1])));
    std::vector<double> depth(size_t(out.width) * size_t(out.height), -std::numeric_limits<double>::infinity());
    std::map<std::string, Texture> cache;
    auto texture = [&](const std::string& name) -> const Texture& {
        auto it = cache.find(name);
        if (it != cache.end()) return it->second;
        const Image image = DecodeTpl(Member(archive, timg + name));
        Texture t;
        t.w = image.width; t.h = image.height;
        t.px.resize(image.rgba.size());
        for (size_t i = 0; i < image.rgba.size(); ++i) t.px[i] = float(image.rgba[i]) / 255.0f;
        return cache.emplace(name, std::move(t)).first->second;
    };

    for (const Pane& p : panes) {
        if (p.w <= 0 || p.h <= 0) continue;
        const long x0 = long(std::max(b[0], std::floor(p.rect[0]))), y0 = long(std::max(b[1], std::floor(p.rect[1])));
        const long x1 = long(std::min(b[2], std::ceil(p.rect[2]))), y1 = long(std::min(b[3], std::ceil(p.rect[3])));
        if (x1 <= x0 || y1 <= y0) continue;
        // Inverse of the affine pane matrix.
        const double a = p.m[0][0], bb = p.m[0][1], c = p.m[1][0], dd = p.m[1][1], tx = p.m[0][2], ty = p.m[1][2];
        const double det = a * dd - bb * c;
        if (det == 0) continue;
        const double i00 = dd / det, i01 = -bb / det, i10 = -c / det, i11 = a / det;
        const double i02 = -(i00 * tx + i01 * ty), i12 = -(i10 * tx + i11 * ty);
        const Material& mat = materials.at(p.material);
        const Texture* tex = mat.samplers.empty() ? nullptr : &texture(textures.at(mat.samplers[0].first));
        const double rr = mat.matrix[2] * (3.141592653589793 / 180.0);
        const double tco = std::cos(rr), tsi = std::sin(rr);
        for (long yy = y0; yy < y1; ++yy) {
            for (long xx = x0; xx < x1; ++xx) {
                const double px = xx + .5, py = -yy - .5;
                const double lx = i00 * px + i01 * py + i02 * 1, ly = i10 * px + i11 * py + i12 * 1;
                const double xs = (lx - p.left) / p.w, ys = (p.top - ly) / p.h;
                if (!(xs >= 0 && xs <= 1 && ys >= 0 && ys <= 1)) continue;
                double& region = depth[size_t(yy - long(b[1])) * size_t(out.width) + size_t(xx - long(b[0]))];
                if (!(p.z >= region)) continue;
                V grad[4];
                for (int k = 0; k < 4; ++k)
                    grad[k] = F64((p.colors[0][k] * (1 - xs) + p.colors[1][k] * xs) * (1 - ys) +
                                  (p.colors[2][k] * (1 - xs) + p.colors[3][k] * xs) * ys);
                V tx4[4] = {F64(1), F64(1), F64(1), F64(1)};
                if (tex) {
                    const double u = (p.uv[0] * (1 - xs) + p.uv[2] * xs) * (1 - ys) + (p.uv[4] * (1 - xs) + p.uv[6] * xs) * ys;
                    const double v = (p.uv[1] * (1 - xs) + p.uv[3] * xs) * (1 - ys) + (p.uv[5] * (1 - xs) + p.uv[7] * xs) * ys;
                    const double ts = mat.matrix[0], tt = mat.matrix[1], ss = mat.matrix[3], st = mat.matrix[4];
                    const double u2 = ss * tco * (u - .5) - st * tsi * (v - .5) + ts + .5;
                    const double v2 = ss * tsi * (u - .5) + st * tco * (v - .5) + tt + .5;
                    double sampled[4];
                    SampleTexture(*tex, u2, v2, mat.samplers[0].second, sampled);
                    for (int k = 0; k < 4; ++k) tx4[k] = F64(sampled[k]);
                }
                V rgba[4];
                if (mat.stages.empty()) {
                    for (int k = 0; k < 4; ++k) {
                        const V r0 = {mat.regs[0][k], true}, r1 = {mat.regs[1][k], true};
                        rgba[k] = tex ? Mul(Add(r0, Mul(tx4[k], Sub(r1, r0))), grad[k]) : Mul(r1, grad[k]);
                    }
                } else {
                    V tev[4][4];
                    for (int k = 0; k < 4; ++k) tev[0][k] = F64(0);
                    for (int r = 0; r < 3; ++r) for (int k = 0; k < 4; ++k) tev[r + 1][k] = {mat.regs[r][k], true};
                    V ras[4];
                    for (int k = 0; k < 4; ++k) ras[k] = mat.vertex ? grad[k] : V{mat.matcol[k], true};
                    auto konst = [&](int sel, bool alpha, int k) -> V {
                        if (sel < 8) return F64((8.0 - sel) / 8);
                        if (!alpha && sel >= 12 && sel < 16) return {mat.constants[sel - 12][k], true};
                        if (sel >= 16) return F64(mat.constants[(sel - 16) % 4][(sel - 16) / 4]);
                        return F64(0);
                    };
                    for (const auto& stg : mat.stages) {
                        for (int pass = 0; pass < 2; ++pass) {
                            const bool alphaPass = pass == 1;
                            const int ofs = alphaPass ? 8 : 4;
                            const uint8_t b0 = stg[ofs], b1 = stg[ofs + 1], b2 = stg[ofs + 2], b3 = stg[ofs + 3];
                            const int ksel = b3 >> 3;
                            auto inp = [&](int code, int k) -> V {
                                if (alphaPass) {
                                    if (code < 4) return tev[code][3];
                                    if (code == 4) return tx4[3];
                                    if (code == 5) return ras[3];
                                    if (code == 6) return konst(ksel, true, 3);
                                    return F64(0);
                                }
                                if (code < 8) return code % 2 == 0 ? tev[code / 2][k] : tev[code / 2][3];
                                if (code == 8) return tx4[k];
                                if (code == 9) return tx4[3];
                                if (code == 10) return ras[k];
                                if (code == 11) return ras[3];
                                if (code == 12) return F64(1);
                                if (code == 13) return F64(.5);
                                if (code == 14) return konst(ksel, false, k);
                                return F64(0);
                            };
                            const int op = b2 & 15;
                            if (op != 0 && op != 1) Fail("Unsupported TEV operation in " + request.layout);
                            const int bias = (b2 >> 4) & 3;
                            const double scale[4] = {1, 2, 4, .5};
                            const int dest = (b3 >> 1) & 3;
                            V results[3];
                            const int channels = alphaPass ? 1 : 3;
                            for (int ch = 0; ch < channels; ++ch) {
                                const int k = alphaPass ? 3 : ch;
                                const V A = inp(b0 & 15, k), B = inp(b0 >> 4, k), C = inp(b1 & 15, k), D = inp(b1 >> 4, k);
                                V r = Add(Mul(A, Scalar(C, 1, '-')), Mul(B, C));
                                r = Scalar(r, op == 0 ? 1 : -1, '*');
                                r = Add(D, r);
                                if (bias == 1) r = Scalar(r, .5, '+');
                                else if (bias == 2) r = Scalar(r, -.5, '+');
                                r = Scalar(r, scale[b2 >> 6], '*');
                                if (b3 & 1) r.v = std::min(std::max(r.v, 0.0), 1.0);
                                results[ch] = r;
                            }
                            for (int ch = 0; ch < channels; ++ch) {
                                const int k = alphaPass ? 3 : ch;
                                tev[dest][k] = dest == 0 ? F64(results[ch].v) : F32v(results[ch].v);
                            }
                        }
                    }
                    for (int k = 0; k < 4; ++k) rgba[k] = tev[0][k];
                }
                rgba[3] = Scalar(rgba[3], p.alpha, '*');
                const double av = std::min(std::max(rgba[3].v * 255, 0.0), 255.0);
                auto test = [&](int kind, int ref) {
                    switch (kind) {
                        case 0: return false;
                        case 1: return av < ref;
                        case 2: return av == ref;
                        case 3: return av <= ref;
                        case 4: return av > ref;
                        case 5: return av != ref;
                        case 6: return av >= ref;
                        default: return true;
                    }
                };
                const bool ta = test(mat.alphaCompare[0] & 15, mat.alphaCompare[2]);
                const bool tb = test(mat.alphaCompare[0] >> 4, mat.alphaCompare[3]);
                bool pass = false;
                switch (mat.alphaCompare[1]) {
                    case 0: pass = ta && tb; break;
                    case 1: pass = ta || tb; break;
                    case 2: pass = ta != tb; break;
                    default: pass = ta == tb; break;
                }
                if (!pass) continue;
                region = p.z;
                if (!p.draw) continue;
                uint8_t src[4];
                for (int k = 0; k < 4; ++k) src[k] = uint8_t(std::min(std::max(rgba[k].v * 255, 0.0), 255.0));
                AlphaComposite(out.At(int(xx - long(b[0])), int(yy - long(b[1]))), src);
            }
        }
    }
    return out;
}

} // namespace wiids::art
