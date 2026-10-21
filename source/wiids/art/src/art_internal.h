#pragma once
// Shared internals of wiids_art: disc helpers used by more than one profile and the small
// drawing toolkit the launcher case composers use.
#include "wiids_art.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace wiids::art::detail {

// ---- disc helpers
Bytes GameCubeFile(const std::string& isoPath, const std::string& path); // one file from a 1:1 GameCube ISO
std::map<std::string, Bytes> Rarc(const Bytes& data);
Image SunshineBti(const Bytes& data); // the Sunshine tool's own BTI decoding rules
std::map<std::string, Image> BannerImages(const std::string& bannerPath); // "banner_x" / "icon_x" -> picture

// ---- drawing (straight RGBA images, float coverage masks)
struct Rgba { uint8_t r, g, b, a; };
using Mask = std::vector<float>; // width*height coverage 0..1

Image Solid(int w, int h, Rgba c);
Image Resize(const Image& src, int w, int h);
Image Scale(const Image& src, double factor);
Image FlipX(const Image& src);
// Source-over with an extra opacity, PIL alpha_composite semantics (straight alpha).
void Over(Image& dst, const Image& src, int x, int y, double opacity = 1.0);
// Paints colour c through a coverage mask the size of dst.
void Paint(Image& dst, const Mask& mask, Rgba c);
Mask EmptyMask(const Image& like);
void MaskEllipse(Mask& m, int w, int h, double cx, double cy, double rx, double ry, float value = 1);
void MaskPolygon(Mask& m, int w, int h, const std::vector<std::array<double, 2>>& points, float value = 1);
void MaskRect(Mask& m, int w, int h, double x0, double y0, double x1, double y1, float value = 1);
Mask Blur(const Mask& m, int w, int h, double radius);
Mask AlphaOf(const Image& image);
void MultiplyAlpha(Image& image, const Mask& m);
void VerticalGradient(Image& dst, const std::vector<std::pair<double, Rgba>>& stops);
Image Tint(const Image& src, Rgba dark, Rgba light); // greyscale ramp, alpha kept
// Draws `image` with a soft drop shadow (blur radius, offset, shadow opacity 0..1).
void Shadowed(Image& dst, const Image& image, int x, int y, double blur, int dx, int dy, double opacity, Rgba shade = {10, 30, 70, 255});
// Text with a game font: letters in `fill`; stroke > 0 keeps the font outline in strokeColor.
double TextWidth(const FontAtlas& font, const std::string& latin1, double height);
void DrawText(Image& dst, const FontAtlas& font, const std::string& latin1, double centerX, double top, double height,
              Rgba fill, double stroke = 0, Rgba strokeColor = {0, 0, 0, 255});

} // namespace wiids::art::detail
