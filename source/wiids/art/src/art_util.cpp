// SHA-256 (FIPS 180-4) and a small ordered JSON writer for wiids_art.
#include "wiids_art.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace wiids::art {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Block(uint32_t h[8], const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = k + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

void Escape(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
                else out += char(c);
        }
    }
    out += '"';
}

} // namespace

std::string Sha256Hex(const void* data, size_t size) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto* p = static_cast<const uint8_t*>(data);
    size_t full = size / 64;
    for (size_t i = 0; i < full; ++i) Block(h, p + 64 * i);
    uint8_t tail[128] = {};
    const size_t rest = size - full * 64;
    if (rest) std::memcpy(tail, p + full * 64, rest);
    tail[rest] = 0x80;
    const size_t tailBlocks = rest + 9 > 64 ? 2 : 1;
    const uint64_t bits = uint64_t(size) * 8;
    for (int i = 0; i < 8; ++i) tail[tailBlocks * 64 - 1 - i] = uint8_t(bits >> (8 * i));
    for (size_t i = 0; i < tailBlocks; ++i) Block(h, tail + 64 * i);
    char hex[65];
    for (int i = 0; i < 8; ++i) std::snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return std::string(hex, 64);
}

Json Json::Object() { Json j; j.kind_ = Kind::Object; return j; }
Json Json::Array() { Json j; j.kind_ = Kind::Array; return j; }
Json Json::Str(const std::string& value) { Json j; j.kind_ = Kind::String; j.text_ = value; return j; }
Json Json::Num(long long value) { Json j; j.kind_ = Kind::Number; j.number_ = value; return j; }
Json Json::Real(double value) { Json j; j.kind_ = Kind::Real; j.real_ = value; return j; }
Json Json::Bool(bool value) { Json j; j.kind_ = Kind::Bool; j.boolean_ = value; return j; }
Json Json::Null() { return Json(); }

Json& Json::Set(const std::string& key, Json value) {
    for (auto& member : members_)
        if (member.first == key) { member.second = std::move(value); return *this; }
    members_.emplace_back(key, std::move(value));
    return *this;
}

Json& Json::Push(Json value) { items_.push_back(std::move(value)); return *this; }

std::string Json::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    return out;
}

void Json::DumpTo(std::string& out, int indent, int depth) const {
    const std::string pad(size_t(indent) * size_t(depth + 1), ' '), close(size_t(indent) * size_t(depth), ' ');
    switch (kind_) {
        case Kind::Null: out += "null"; break;
        case Kind::Bool: out += boolean_ ? "true" : "false"; break;
        case Kind::Number: out += std::to_string(number_); break;
        case Kind::Real: {
            if (std::isfinite(real_) && real_ == std::floor(real_) && std::fabs(real_) < 1e15) {
                out += std::to_string((long long)real_) + ".0";
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", real_);
                out += buf;
            }
            break;
        }
        case Kind::String: Escape(out, text_); break;
        case Kind::Array:
            if (items_.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < items_.size(); ++i) {
                out += pad;
                items_[i].DumpTo(out, indent, depth + 1);
                out += i + 1 < items_.size() ? ",\n" : "\n";
            }
            out += close + "]";
            break;
        case Kind::Object:
            if (members_.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < members_.size(); ++i) {
                out += pad;
                Escape(out, members_[i].first);
                out += ": ";
                members_[i].second.DumpTo(out, indent, depth + 1);
                out += i + 1 < members_.size() ? ",\n" : "\n";
            }
            out += close + "}";
            break;
    }
}

} // namespace wiids::art
