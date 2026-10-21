// Wii disc channel banner textures (files/opening.bnr), ported from
// experiments/nsmbw-thor/extract_banner_art.py: IMET + U8 {meta/banner.bin, meta/icon.bin};
// each .bin is IMD5 + LZ77 (0x10) + U8 {arc/timg/*.tpl}. Used to compose the launcher cases.
#include "wiids_art_profiles.h"
#include "art_internal.h"

#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace wiids::art {
namespace {

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }

Bytes Lz77(const Bytes& data) {
    if (data.empty() || data[0] != 0x10) Fail("Expected LZ77 type 0x10");
    const size_t size = data.at(1) | size_t(data.at(2)) << 8 | size_t(data.at(3)) << 16;
    Bytes out;
    out.reserve(size);
    size_t at = 4;
    while (out.size() < size) {
        const uint8_t flags = data.at(at++);
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if (flags & (0x80 >> bit)) {
                const uint8_t b1 = data.at(at), b2 = data.at(at + 1);
                at += 2;
                const size_t length = (b1 >> 4) + 3, back = (size_t(b1 & 0xF) << 8 | b2) + 1;
                if (back > out.size()) Fail("Damaged banner");
                for (size_t i = 0; i < length; ++i) out.push_back(out[out.size() - back]);
            } else {
                out.push_back(data.at(at++));
            }
        }
    }
    return out;
}

std::map<std::string, Bytes> Inner(Bytes blob) {
    if (blob.size() >= 4 && std::memcmp(blob.data(), "IMD5", 4) == 0) blob.erase(blob.begin(), blob.begin() + 0x20);
    if (blob.size() >= 4 && std::memcmp(blob.data(), "LZ77", 4) == 0) blob.erase(blob.begin(), blob.begin() + 4);
    if (!blob.empty() && blob[0] == 0x10) blob = Lz77(blob);
    return U8Files(blob);
}

std::string Stem(const std::string& path) {
    const size_t slash = path.rfind('/');
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    return name.substr(0, name.rfind('.'));
}

} // namespace

namespace detail {
std::map<std::string, Image> BannerImages(const std::string& bannerPath) {
    const Bytes data = ReadFile(bannerPath);
    size_t at = std::string::npos;
    for (size_t i = 0; i + 4 <= data.size() && i < 0x1000; ++i)
        if (std::memcmp(&data[i], "IMET", 4) == 0) { at = i; break; }
    if (at == std::string::npos || at < 0x40) Fail("The disc banner has no IMET header.");
    auto outer = U8Files(Bytes(data.begin() + std::ptrdiff_t(at - 0x40 + 0x600), data.end()));
    std::map<std::string, Image> images;
    for (const std::string part : {"banner", "icon"}) {
        std::string key;
        for (const auto& [name, unused] : outer)
            if (name.size() >= part.size() + 4 && name.compare(name.size() - part.size() - 4, std::string::npos, part + ".bin") == 0) key = name;
        if (key.empty()) continue;
        for (const auto& [name, payload] : Inner(outer[key])) {
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".tpl") != 0) continue;
            try { images[part + "_" + Stem(name)] = DecodeTpl(payload); } catch (const std::exception&) {}
        }
    }
    if (images.empty()) Fail("The disc banner has no pictures.");
    return images;
}
} // namespace detail

std::string BuildBannerArt(const std::string& bannerPath, const std::string& outDir) {
    const Bytes data = ReadFile(bannerPath);
    size_t at = std::string::npos;
    for (size_t i = 0; i + 4 <= data.size() && i < 0x1000; ++i)
        if (std::memcmp(&data[i], "IMET", 4) == 0) { at = i; break; }
    if (at == std::string::npos || at < 0x40) Fail("The disc banner has no IMET header.");
    const Bytes outerBytes(data.begin() + std::ptrdiff_t(at - 0x40 + 0x600), data.end());
    auto outer = U8Files(outerBytes);
    std::filesystem::create_directories(std::filesystem::u8path(outDir));
    int count = 0;
    for (const char* part : {"banner", "icon"}) {
        std::string key;
        for (const auto& [name, unused] : outer)
            if (name.size() >= std::strlen(part) + 4 && name.compare(name.size() - std::strlen(part) - 4, std::string::npos, std::string(part) + ".bin") == 0) key = name;
        if (key.empty()) continue;
        for (const auto& [name, payload] : Inner(outer[key])) {
            if (name.size() < 4 || name.compare(name.size() - 4, 4, ".tpl") != 0) continue;
            Image image;
            try { image = DecodeTpl(payload); } catch (const std::exception&) { continue; }
            const Bytes png = EncodePng(image);
            WriteFile(outDir + "/" + part + "_" + Stem(name) + ".png", png.data(), png.size());
            ++count;
        }
    }
    if (!count) Fail("The disc banner has no pictures.");
    return std::to_string(count) + " banner pictures";
}

} // namespace wiids::art
