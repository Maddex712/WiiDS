// Small drawing toolkit for the launcher case composers (art_cases.cpp).
#include "art_internal.h"

#include <algorithm>
#include <cmath>

namespace wiids::art::detail {
namespace {

double Cubic(double x) { // Catmull-Rom
    x = std::fabs(x);
    if (x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
    if (x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
    return 0;
}

// One separable pass on premultiplied float RGBA (horizontal when `horizontal`).
std::vector<float> ResamplePass(const std::vector<float>& in, int w, int h, int outLen, bool horizontal) {
    const int inLen = horizontal ? w : h, other = horizontal ? h : w;
    const double scale = double(outLen) / inLen;
    const double support = scale < 1 ? 2.0 / scale : 2.0;
    std::vector<float> out(size_t(outLen) * size_t(other) * 4, 0.0f);
    for (int o = 0; o < outLen; ++o) {
        const double center = (o + 0.5) / scale - 0.5;
        const int lo = int(std::floor(center - support)), hi = int(std::ceil(center + support));
        std::vector<std::pair<int, double>> taps;
        double sum = 0;
        for (int i = lo; i <= hi; ++i) {
            const double wgt = Cubic((i - center) * (scale < 1 ? scale : 1.0));
            if (wgt == 0) continue;
            taps.push_back({std::min(std::max(i, 0), inLen - 1), wgt});
            sum += wgt;
        }
        for (auto& t : taps) t.second /= sum;
        for (int k = 0; k < other; ++k) {
            double acc[4] = {0, 0, 0, 0};
            for (const auto& [i, wgt] : taps) {
                const size_t idx = horizontal ? (size_t(k) * size_t(w) + size_t(i)) * 4 : (size_t(i) * size_t(w) + size_t(k)) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += in[idx + size_t(c)] * wgt;
            }
            const size_t dst = horizontal ? (size_t(k) * size_t(outLen) + size_t(o)) * 4 : (size_t(o) * size_t(w) + size_t(k)) * 4;
            for (int c = 0; c < 4; ++c) out[dst + size_t(c)] = float(acc[c]);
        }
    }
    return out;
}

uint8_t Clamp8(double v) { return uint8_t(std::min(255.0, std::max(0.0, std::round(v)))); }

} // namespace

Image Solid(int w, int h, Rgba c) {
    Image im(w, h);
    for (size_t i = 0; i < im.rgba.size(); i += 4) { im.rgba[i] = c.r; im.rgba[i + 1] = c.g; im.rgba[i + 2] = c.b; im.rgba[i + 3] = c.a; }
    return im;
}

Image Resize(const Image& src, int w, int h) {
    if (w <= 0 || h <= 0) return Image();
    std::vector<float> pre(src.rgba.size());
    for (size_t i = 0; i < src.rgba.size(); i += 4) {
        const float a = src.rgba[i + 3] / 255.0f;
        for (int c = 0; c < 3; ++c) pre[i + size_t(c)] = src.rgba[i + size_t(c)] * a;
        pre[i + 3] = src.rgba[i + 3];
    }
    const auto horizontal = ResamplePass(pre, src.width, src.height, w, true);
    const auto both = ResamplePass(horizontal, w, src.height, h, false);
    Image out(w, h);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
        const double a = std::min(255.0, std::max(0.0, double(both[i + 3])));
        out.rgba[i + 3] = Clamp8(a);
        for (int c = 0; c < 3; ++c) out.rgba[i + size_t(c)] = a > 0.5 ? Clamp8(both[i + size_t(c)] / (a / 255.0)) : 0;
    }
    return out;
}

Image Scale(const Image& src, double factor) {
    return Resize(src, std::max(1, int(std::lround(src.width * factor))), std::max(1, int(std::lround(src.height * factor))));
}

Image FlipX(const Image& src) {
    Image out(src.width, src.height);
    for (int y = 0; y < src.height; ++y)
        for (int x = 0; x < src.width; ++x) std::copy_n(src.At(src.width - 1 - x, y), 4, out.At(x, y));
    return out;
}

void Over(Image& dst, const Image& src, int x0, int y0, double opacity) {
    for (int y = 0; y < src.height; ++y) {
        const int dy = y0 + y;
        if (dy < 0 || dy >= dst.height) continue;
        for (int x = 0; x < src.width; ++x) {
            const int dx = x0 + x;
            if (dx < 0 || dx >= dst.width) continue;
            const uint8_t* s = src.At(x, y);
            uint8_t* d = dst.At(dx, dy);
            const double sa = s[3] / 255.0 * opacity, da = d[3] / 255.0;
            const double oa = sa + da * (1 - sa);
            if (oa <= 0) continue;
            for (int c = 0; c < 3; ++c) d[c] = Clamp8((s[c] * sa + d[c] * da * (1 - sa)) / oa);
            d[3] = Clamp8(oa * 255);
        }
    }
}

void Paint(Image& dst, const Mask& mask, Rgba c) {
    Image layer(dst.width, dst.height);
    for (size_t i = 0; i < mask.size(); ++i) {
        layer.rgba[i * 4] = c.r; layer.rgba[i * 4 + 1] = c.g; layer.rgba[i * 4 + 2] = c.b;
        layer.rgba[i * 4 + 3] = Clamp8(c.a * std::min(1.0f, std::max(0.0f, mask[i])));
    }
    Over(dst, layer, 0, 0);
}

Mask EmptyMask(const Image& like) { return Mask(size_t(like.width) * size_t(like.height), 0.0f); }

// Coverage by 4x4 supersampling of an inside() test.
template <typename Inside>
void Cover(Mask& m, int w, int h, double x0, double y0, double x1, double y1, float value, Inside inside) {
    const int ix0 = std::max(0, int(std::floor(x0))), iy0 = std::max(0, int(std::floor(y0)));
    const int ix1 = std::min(w, int(std::ceil(x1)) + 1), iy1 = std::min(h, int(std::ceil(y1)) + 1);
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) hits += inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4) ? 1 : 0;
            if (!hits) continue;
            const float cover = hits / 16.0f;
            float& cell = m[size_t(y) * size_t(w) + size_t(x)];
            cell = value >= 0.5f ? std::max(cell, cover * value) : cell * (1 - cover); // value 0 erases
        }
}

void MaskEllipse(Mask& m, int w, int h, double cx, double cy, double rx, double ry, float value) {
    Cover(m, w, h, cx - rx, cy - ry, cx + rx, cy + ry, value, [&](double x, double y) {
        const double dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy <= 1;
    });
}

void MaskPolygon(Mask& m, int w, int h, const std::vector<std::array<double, 2>>& p, float value) {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const auto& q : p) { x0 = std::min(x0, q[0]); y0 = std::min(y0, q[1]); x1 = std::max(x1, q[0]); y1 = std::max(y1, q[1]); }
    Cover(m, w, h, x0, y0, x1, y1, value, [&](double x, double y) {
        bool in = false;
        for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
            if ((p[i][1] > y) != (p[j][1] > y) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) in = !in;
        return in;
    });
}

void MaskRect(Mask& m, int w, int h, double x0, double y0, double x1, double y1, float value) {
    Cover(m, w, h, x0, y0, x1, y1, value, [&](double x, double y) { return x >= x0 && x < x1 && y >= y0 && y < y1; });
}

Mask Blur(const Mask& m, int w, int h, double radius) {
    if (radius <= 0) return m;
    // Three box passes approximate a Gaussian.
    const int r = std::max(1, int(std::lround(radius * 0.87)));
    Mask a = m, b(m.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            double acc = 0;
            for (int x = -r; x <= r; ++x) acc += a[size_t(y) * size_t(w) + size_t(std::min(std::max(x, 0), w - 1))];
            for (int x = 0; x < w; ++x) {
                b[size_t(y) * size_t(w) + size_t(x)] = float(acc / (2 * r + 1));
                acc += a[size_t(y) * size_t(w) + size_t(std::min(x + r + 1, w - 1))] - a[size_t(y) * size_t(w) + size_t(std::max(x - r, 0))];
            }
        }
        for (int x = 0; x < w; ++x) {
            double acc = 0;
            for (int y = -r; y <= r; ++y) acc += b[size_t(std::min(std::max(y, 0), h - 1)) * size_t(w) + size_t(x)];
            for (int y = 0; y < h; ++y) {
                a[size_t(y) * size_t(w) + size_t(x)] = float(acc / (2 * r + 1));
                acc += b[size_t(std::min(y + r + 1, h - 1)) * size_t(w) + size_t(x)] - b[size_t(std::max(y - r, 0)) * size_t(w) + size_t(x)];
            }
        }
    }
    return a;
}

Mask AlphaOf(const Image& image) {
    Mask m(size_t(image.width) * size_t(image.height));
    for (size_t i = 0; i < m.size(); ++i) m[i] = image.rgba[i * 4 + 3] / 255.0f;
    return m;
}

void MultiplyAlpha(Image& image, const Mask& m) {
    for (size_t i = 0; i < m.size(); ++i) image.rgba[i * 4 + 3] = Clamp8(image.rgba[i * 4 + 3] * std::min(1.0f, std::max(0.0f, m[i])));
}

void VerticalGradient(Image& dst, const std::vector<std::pair<double, Rgba>>& stops) {
    for (int y = 0; y < dst.height; ++y) {
        const double t = dst.height > 1 ? double(y) / (dst.height - 1) : 0;
        size_t k = 0;
        while (k + 2 < stops.size() && t > stops[k + 1].first) ++k;
        const auto& [t0, c0] = stops[k];
        const auto& [t1, c1] = stops[std::min(k + 1, stops.size() - 1)];
        const double f = t1 > t0 ? std::min(1.0, std::max(0.0, (t - t0) / (t1 - t0))) : 0;
        const Rgba c = {Clamp8(c0.r + (c1.r - c0.r) * f), Clamp8(c0.g + (c1.g - c0.g) * f), Clamp8(c0.b + (c1.b - c0.b) * f),
                        Clamp8(c0.a + (c1.a - c0.a) * f)};
        for (int x = 0; x < dst.width; ++x) { uint8_t* p = dst.At(x, y); p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a; }
    }
}

Image Tint(const Image& src, Rgba dark, Rgba light) {
    Image out = src;
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
        const double v = (src.rgba[i] * 299 + src.rgba[i + 1] * 587 + src.rgba[i + 2] * 114) / 1000.0 / 255.0;
        out.rgba[i] = Clamp8(dark.r + (light.r - dark.r) * v);
        out.rgba[i + 1] = Clamp8(dark.g + (light.g - dark.g) * v);
        out.rgba[i + 2] = Clamp8(dark.b + (light.b - dark.b) * v);
    }
    return out;
}

void Shadowed(Image& dst, const Image& image, int x, int y, double blur, int dx, int dy, double opacity, Rgba shade) {
    const int pad = int(std::ceil(blur * 3)) + 2;
    Image padded(image.width + 2 * pad, image.height + 2 * pad);
    padded.Paste(image, pad, pad);
    Mask m = Blur(AlphaOf(padded), padded.width, padded.height, blur);
    Image shadow = Solid(padded.width, padded.height, {shade.r, shade.g, shade.b, 255});
    for (float& v : m) v *= float(opacity);
    MultiplyAlpha(shadow, m);
    Over(dst, shadow, x + dx - pad, y + dy - pad);
    Over(dst, image, x, y);
}

double TextWidth(const FontAtlas& font, const std::string& text, double height) {
    const double scale = height / std::max(1, font.cellHeight);
    double w = 0;
    for (const unsigned char ch : text) {
        const auto it = font.glyphs.find(ch);
        if (it != font.glyphs.end()) w += it->second.advance * scale;
    }
    return w;
}

void DrawText(Image& dst, const FontAtlas& font, const std::string& text, double centerX, double top, double height,
              Rgba fill, double stroke, Rgba strokeColor) {
    const double scale = height / std::max(1, font.cellHeight);
    const double width = TextWidth(font, text, height);
    const int pad = int(std::ceil(stroke)) + 2;
    Image layer(int(std::ceil(width)) + 2 * pad + int(height), int(std::ceil(height)) + 2 * pad);
    double pen = pad;
    for (const unsigned char ch : text) {
        const auto it = font.glyphs.find(ch);
        if (it == font.glyphs.end()) continue;
        const Glyph& g = it->second;
        Image glyph = font.atlas.Crop(g.x, g.y, g.x + g.w, g.y + g.h);
        glyph = Resize(glyph, std::max(1, int(std::lround(g.w * scale))), std::max(1, int(std::lround(g.h * scale))));
        // Game fonts draw white letters with a dark outline: brightness is the letter. Plain
        // text keeps only the letter; outlined text recolours the font's own outline.
        for (size_t i = 0; i < glyph.rgba.size(); i += 4) {
            const double lum = (glyph.rgba[i] * 299 + glyph.rgba[i + 1] * 587 + glyph.rgba[i + 2] * 114) / 1000.0 / 255.0;
            if (stroke > 0) {
                glyph.rgba[i] = uint8_t(strokeColor.r + (fill.r - strokeColor.r) * lum);
                glyph.rgba[i + 1] = uint8_t(strokeColor.g + (fill.g - strokeColor.g) * lum);
                glyph.rgba[i + 2] = uint8_t(strokeColor.b + (fill.b - strokeColor.b) * lum);
                glyph.rgba[i + 3] = uint8_t(glyph.rgba[i + 3] * fill.a / 255);
            } else {
                glyph.rgba[i] = fill.r; glyph.rgba[i + 1] = fill.g; glyph.rgba[i + 2] = fill.b;
                glyph.rgba[i + 3] = uint8_t(glyph.rgba[i + 3] * lum * fill.a / 255);
            }
        }
        Over(layer, glyph, int(std::lround(pen + g.left * scale)), pad);
        pen += g.advance * scale;
    }
    const int x = int(std::lround(centerX - width / 2)) - pad, y = int(std::lround(top)) - pad;
    Over(dst, layer, x, y);
}

} // namespace wiids::art::detail
