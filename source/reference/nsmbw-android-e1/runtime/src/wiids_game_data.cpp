// WiiDS: rebuilds the game executable's loaded image from the player's own disc (see header).
// Every transformation here mirrors a build-time tool exactly:
//   DOL sections      - the translator's DOL loader (section bytes at their load address)
//   StaticRel         - translator/src/Translator.Core/Parsing/Rel/RelFile.cs BuildImage
//   NsmbwLinkedRels   - projects/nsmbw/tools/unpack_game.py lz11 + prelink_rels.py, then BuildImage
//   Patch::Msmbwii    - experiments/nsmbw-thor/prepare_msmbwii.py
// The pinned SHA-256 of each result is the final word: any difference refuses to start.
#include "wiids_game_data.h"

#include "isa/ppc_isa_context.h"
#include "memory.h"
#include "runtime_config.h"
#include "system_bridge.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace WiiDSGameData {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

// ------------------------------------------------------------------ SHA-256 (FIPS 180-4)
class Sha256 {
public:
    void Update(const uint8_t* data, size_t size) {
        total_ += size;
        while (size) {
            const size_t take = std::min(size, sizeof(block_) - used_);
            std::memcpy(block_ + used_, data, take);
            used_ += take; data += take; size -= take;
            if (used_ == sizeof(block_)) { Compress(block_); used_ = 0; }
        }
    }
    std::string Hex() {
        const uint64_t bits = total_ * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        const uint8_t zero = 0;
        while (used_ != 56) Update(&zero, 1);
        uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = uint8_t(bits >> (56 - 8 * i));
        Update(length, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (uint32_t word : state_)
            for (int shift = 28; shift >= 0; shift -= 4) out += digits[(word >> shift) & 0xF];
        return out;
    }

private:
    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Compress(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }
    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64] = {};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

std::string HashOf(const Bytes& data) {
    Sha256 sha;
    sha.Update(data.data(), data.size());
    return sha.Hex();
}

// ------------------------------------------------------------------ helpers
const Game* g_game = nullptr;

[[noreturn]] void Fail(const std::string& problem) {
    std::string details = problem;
    if (g_game) {
        details += "\n\n";
        details += g_game->title;
        details += " needs your own disc: ";
        details += g_game->disc;
        details += ".\nImport that disc again from the WiiDS library.";
    }
    std::cerr << "[wiids] " << details << std::endl;
    RuntimeCrash::WriteCrashArtifacts("game_data", details);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("This disc can't be used", details);
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

Bytes ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) Fail("A game file is missing: " + path.filename().string());
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || size > (512 << 20)) Fail("A game file has an unexpected size: " + path.filename().string());
    in.seekg(0, std::ios::beg);
    Bytes data(static_cast<size_t>(size));
    if (size && !in.read(reinterpret_cast<char*>(data.data()), size))
        Fail("A game file could not be read: " + path.filename().string());
    return data;
}

uint32_t Be32(const Bytes& b, size_t at) {
    if (at + 4 > b.size()) Fail("A game file is shorter than expected.");
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}
uint16_t Be16(const Bytes& b, size_t at) {
    if (at + 2 > b.size()) Fail("A game file is shorter than expected.");
    return uint16_t(b[at] << 8 | b[at + 1]);
}
void Put32(Bytes& b, size_t at, uint32_t v) {
    if (at + 4 > b.size()) Fail("A relocation points outside its module.");
    b[at] = uint8_t(v >> 24); b[at + 1] = uint8_t(v >> 16); b[at + 2] = uint8_t(v >> 8); b[at + 3] = uint8_t(v);
}
void Put16(Bytes& b, size_t at, uint16_t v) {
    if (at + 2 > b.size()) Fail("A relocation points outside its module.");
    b[at] = uint8_t(v >> 8); b[at + 1] = uint8_t(v);
}
uint32_t AlignUp(uint32_t v, uint32_t a) { return a ? (v + a - 1) & ~(a - 1) : v; }

// ------------------------------------------------------------------ DOL
Bytes DolSection(const Bytes& dol, uint32_t address, uint32_t size) {
    for (int i = 0; i < 18; ++i) {
        const uint32_t offset = Be32(dol, 4 * i), load = Be32(dol, 0x48 + 4 * i), length = Be32(dol, 0x90 + 4 * i);
        if (length && load == address) {
            if (length != size || size_t(offset) + length > dol.size()) break;
            return Bytes(dol.begin() + offset, dol.begin() + offset + length);
        }
    }
    Fail("The game's main executable (main.dol) is not the expected version.");
}

// ------------------------------------------------------------------ REL (RelFile.BuildImage)
Bytes BuildRelImage(const Bytes& raw, uint32_t base, uint32_t imageSize) {
    const uint32_t count = Be32(raw, 0x0C), table = Be32(raw, 0x10), bssSize = Be32(raw, 0x20);
    const uint32_t importOffset = Be32(raw, 0x28), importSize = Be32(raw, 0x2C);
    const uint8_t bssIndex = raw.size() > 0x33 ? raw[0x33] : 0;
    uint32_t bssAlign = Be32(raw, 0x44);
    if (!bssAlign) bssAlign = 0x20;
    struct Sec { uint32_t offset, size; };
    std::vector<Sec> sections;
    for (uint32_t i = 0; i < count; ++i) sections.push_back({Be32(raw, table + 8 * i) & ~1u, Be32(raw, table + 8 * i + 4)});
    const uint32_t bssOffset = AlignUp(AlignUp(uint32_t(raw.size()) + 0x20, 0x20), bssAlign);
    uint32_t cursor = bssOffset;
    for (uint32_t i = 0; i < count; ++i)
        if (sections[i].offset == 0 && (sections[i].size != 0 || i == bssIndex)) { sections[i].offset = cursor; cursor += sections[i].size; }
    const uint32_t total = bssOffset + bssSize;
    if (total != imageSize) Fail("The game's module (StaticR.rel) is not the expected version.");
    Bytes memory(total, 0);
    std::memcpy(memory.data(), raw.data(), raw.size());
    for (uint32_t at = importOffset; at < importOffset + importSize; at += 8) {
        const uint32_t module = Be32(raw, at);
        uint32_t p = Be32(raw, at + 4), offset = 0, section = 0;
        for (;;) {
            const uint16_t delta = Be16(raw, p);
            const uint8_t type = raw.at(p + 2), symbolSection = raw.at(p + 3);
            const uint32_t addend = Be32(raw, p + 4);
            p += 8;
            if (type == 203) break;                                  // R_RVL_STOP
            if (type == 202) { section = symbolSection; offset = 0; continue; } // R_RVL_SECT
            offset += delta;
            if (section >= sections.size()) Fail("The game's module has an invalid relocation.");
            const uint32_t dst = base + sections[section].offset + offset;
            uint32_t target;
            if (module == 0) {
                target = addend;
                if (target < 0x80000000u) target += 0x80000000u;
            } else {
                if (symbolSection >= sections.size()) Fail("The game's module has an invalid relocation.");
                target = base + sections[symbolSection].offset + addend;
            }
            const size_t index = dst - base;
            switch (type) {
                case 201: break;                                                   // R_RVL_NONE
                case 1: Put32(memory, index, target); break;                        // ADDR32
                case 4: Put16(memory, index, uint16_t(target)); break;              // ADDR16_LO
                case 6: Put16(memory, index, uint16_t((target + 0x8000) >> 16)); break; // ADDR16_HA
                case 10: Put32(memory, index, (Be32(memory, index) & 0xFC000003u) | ((target - dst) & 0x03FFFFFCu)); break;
                default: Fail("The game's module uses an unexpected relocation.");
            }
        }
    }
    return memory;
}

// ------------------------------------------------------------------ NSMB Wii boot RELs
Bytes Lz11(const Bytes& data) {
    if (data.size() < 4 || data[0] != 0x11) Fail("A compressed game module is not in the expected format.");
    size_t size = data[1] | data[2] << 8 | data[3] << 16, pos = 4;
    if (size == 0) { size = data.at(4) | data.at(5) << 8 | data.at(6) << 16 | size_t(data.at(7)) << 24; pos = 8; }
    Bytes out;
    out.reserve(size);
    while (out.size() < size) {
        const uint8_t flags = data.at(pos++);
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
            if (!(flags & (0x80 >> bit))) { out.push_back(data.at(pos++)); continue; }
            const uint8_t b0 = data.at(pos);
            size_t length, disp;
            if ((b0 >> 4) == 0) {
                const uint8_t b1 = data.at(pos + 1), b2 = data.at(pos + 2);
                length = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11; disp = (((b1 & 0xF) << 8) | b2) + 1; pos += 3;
            } else if ((b0 >> 4) == 1) {
                const uint8_t b1 = data.at(pos + 1), b2 = data.at(pos + 2), b3 = data.at(pos + 3);
                length = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111; disp = (((b2 & 0xF) << 8) | b3) + 1; pos += 4;
            } else {
                const uint8_t b1 = data.at(pos + 1);
                length = (b0 >> 4) + 1; disp = (((b0 & 0xF) << 8) | b1) + 1; pos += 2;
            }
            if (disp > out.size()) Fail("A compressed game module is damaged.");
            const size_t start = out.size() - disp;
            for (size_t i = 0; i < length; ++i) out.push_back(out[start + i]); // may overlap
        }
    }
    out.resize(size);
    return out;
}

struct NsmbRel {
    uint32_t id = 0, image = 0, fixSize = 0, bssAddr = 0, bssSize = 0, end = 0;
    Bytes raw;
    struct Sec { uint32_t offset, size; bool exec; };
    std::vector<Sec> sections;
    std::vector<std::pair<uint32_t, uint32_t>> imports;
    uint32_t SectionAddr(size_t index) const {
        if (sections.at(index).offset == 0 && index != 0) return bssAddr;
        return image + sections.at(index).offset;
    }
};

Bytes NsmbwLinkedImage(const fs::path& root, uint32_t imageSize) {
    // prelink_rels.py: SMNE01 rev 1 file-image addresses, chained heap layout.
    static const struct { uint32_t id; uint32_t image; const char* name; } kModules[] = {
        {1, 0x807684C0u, "d_profileNP"}, {2, 0x8076D680u, "d_basesNP"},
        {3, 0x809A2CC0u, "d_enemiesNP"}, {4, 0x80B1C940u, "d_en_bossNP"}};
    std::map<uint32_t, NsmbRel> mods;
    for (const auto& m : kModules) {
        NsmbRel rel;
        rel.id = m.id; rel.image = m.image;
        rel.raw = Lz11(ReadFile(root / "files" / "rels" / (std::string(m.name) + ".rel.LZ")));
        const Bytes& b = rel.raw;
        if (Be32(b, 0) != m.id || Be32(b, 0x1C) != 3) Fail("A game module is not the expected version.");
        const uint32_t count = Be32(b, 0x0C), table = Be32(b, 0x10);
        rel.bssSize = Be32(b, 0x20);
        const uint32_t importOffset = Be32(b, 0x28), importSize = Be32(b, 0x2C);
        rel.fixSize = Be32(b, 0x48);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t off = Be32(b, table + 8 * i), size = Be32(b, table + 8 * i + 4);
            rel.sections.push_back({off & ~1u, size, (off & 1) != 0});
        }
        for (uint32_t i = 0; i < importSize / 8; ++i)
            rel.imports.emplace_back(Be32(b, importOffset + 8 * i), Be32(b, importOffset + 8 * i + 4));
        rel.bssAddr = rel.image + AlignUp(rel.fixSize, 0x20);
        rel.end = rel.bssAddr + rel.bssSize;
        mods[m.id] = std::move(rel);
    }
    for (uint32_t id = 1; id < 4; ++id)
        if (AlignUp(mods[id].end + 0x10, 0x20) != mods[id + 1].image) Fail("The game's modules do not have the expected layout.");
    const uint32_t base = mods[1].image, end = AlignUp(mods[4].end, 0x20);
    Bytes mem(end - base, 0);
    for (auto& [id, m] : mods) {
        if (m.fixSize > m.raw.size()) Fail("A game module is damaged.");
        std::memcpy(mem.data() + (m.image - base), m.raw.data(), m.fixSize);
        std::fill(mem.begin() + (m.bssAddr - base), mem.begin() + (m.bssAddr - base) + m.bssSize, 0);
    }
    auto rd32 = [&](uint32_t a) { return Be32(mem, a - base); };
    auto wr32 = [&](uint32_t a, uint32_t v) { Put32(mem, a - base, v); };
    auto wr16 = [&](uint32_t a, uint32_t v) { Put16(mem, a - base, uint16_t(v)); };
    for (auto& [id, m] : mods) {
        for (const auto& [importModule, relOffset] : m.imports) {
            uint32_t cursor = relOffset, sectionAddr = 0;
            for (;;) {
                const uint16_t delta = Be16(m.raw, cursor);
                const uint8_t type = m.raw.at(cursor + 2), symbolSection = m.raw.at(cursor + 3);
                const uint32_t addend = Be32(m.raw, cursor + 4);
                cursor += 8;
                if (type == 203) break;
                if (type == 202) { sectionAddr = m.SectionAddr(symbolSection); continue; }
                sectionAddr += delta;
                const uint32_t dst = sectionAddr;
                uint32_t target = addend;
                if (importModule != 0) {
                    auto other = mods.find(importModule);
                    if (other == mods.end()) Fail("A game module imports an unknown module.");
                    target = other->second.SectionAddr(symbolSection) + addend;
                }
                switch (type) {
                    case 0: case 201: break;
                    case 1: wr32(dst, target); break;
                    case 2: wr32(dst, (rd32(dst) & ~0x03FFFFFCu) | (target & 0x03FFFFFCu)); break;
                    case 3: case 4: wr16(dst, target); break;
                    case 5: wr16(dst, target >> 16); break;
                    case 6: wr16(dst, (target >> 16) + ((target & 0x8000) ? 1 : 0)); break;
                    case 7: case 8: case 9: wr32(dst, (rd32(dst) & ~0xFFFCu) | (target & 0xFFFCu)); break;
                    case 10: wr32(dst, (rd32(dst) & ~0x03FFFFFCu) | ((target - dst) & 0x03FFFFFCu)); break;
                    case 11: case 12: case 13: wr32(dst, (rd32(dst) & ~0xFFFCu) | ((target - dst) & 0xFFFCu)); break;
                    default: Fail("A game module uses an unexpected relocation.");
                }
            }
        }
    }
    // The synthetic pre-linked REL header (prelink_rels.py), then the translator's BuildImage
    // padding: the header advertises no BSS, so the image is the file rounded to 0x20 + 0x20.
    struct SynSec { uint32_t offset; bool exec; uint32_t size; };
    std::vector<SynSec> syn{{0, false, 0}};
    for (auto& [id, m] : mods) {
        syn.push_back({m.image + m.sections.at(1).offset - base, true, m.sections.at(1).size});
        const uint32_t dataStart = m.image + m.sections.at(2).offset;
        const uint32_t dataEnd = m.image + m.sections.at(5).offset + m.sections.at(5).size;
        syn.push_back({dataStart - base, false, dataEnd - dataStart});
        syn.push_back({m.bssAddr - base, false, m.bssSize});
    }
    const uint32_t size = uint32_t(mem.size());
    Bytes header(0x4C + 8 * syn.size(), 0);
    const uint32_t fields[] = {0x7E, 0, 0, uint32_t(syn.size()), 0x4C, 0, 0, 3, 0, size, size, 0};
    for (size_t i = 0; i < 12; ++i) Put32(header, 4 * i, fields[i]);
    // bytes 0x30..0x33 sections, 0x34..0x3F offsets: all zero
    Put32(header, 0x40, 0x20); Put32(header, 0x44, 0x20); Put32(header, 0x48, size);
    for (size_t i = 0; i < syn.size(); ++i) {
        Put32(header, 0x4C + 8 * i, syn[i].offset | (syn[i].exec ? 1u : 0u));
        Put32(header, 0x4C + 8 * i + 4, syn[i].size);
    }
    std::memcpy(mem.data(), header.data(), header.size());
    if (AlignUp(size + 0x20, 0x20) != imageSize) Fail("The game's modules are not the expected version.");
    mem.resize(imageSize, 0);
    return mem;
}

// ------------------------------------------------------------------ MSMBWii (prepare_msmbwii.py)
struct GuestView {
    std::vector<std::pair<uint32_t, Bytes*>> parts;
    Bytes* Find(uint32_t address, size_t& index) {
        for (auto& [base, bytes] : parts)
            if (address >= base && address + 4 <= base + bytes->size()) { index = address - base; return bytes; }
        return nullptr;
    }
    uint32_t Read(uint32_t address) {
        size_t i; Bytes* b = Find(address, i);
        if (!b) Fail("The MSMBWii mod writes outside the game.");
        return Be32(*b, i);
    }
    void Write(uint32_t address, uint32_t word) {
        size_t i; Bytes* b = Find(address, i);
        if (!b) Fail("The MSMBWii mod writes outside the game.");
        Put32(*b, i, word);
    }
};

void ApplyMsmbwii(const fs::path& root, GuestView& view) {
    const fs::path table = root.parent_path() / "mod" / "msmbwiijala" / "AnotherRes" / "SystemUS_1.bin";
    std::error_code ec;
    if (!fs::is_regular_file(table, ec))
        Fail("The MSMBWii level pack isn't installed. Download it from the WiiDS library first.");
    const Bytes data = ReadFile(table);
    constexpr uint32_t kCaveHook = 0x807EBC5Cu, kCave = 0x80A02610u;
    for (size_t i = 0; i + 8 <= data.size();) {
        const uint32_t a = Be32(data, i), v = Be32(data, i + 4);
        if (a == 0xFFFFFFFFu) break;
        uint32_t address; std::vector<uint32_t> words;
        if (a < 0x80000000u) {
            address = v;
            for (uint32_t k = 0; k < a; ++k) words.push_back(Be32(data, i + 8 + 4 * k));
            i += 8 + 4 * size_t(a);
        } else {
            address = a; words.push_back(v); i += 8;
        }
        if (address == kCaveHook || address == kCave) continue;
        for (size_t k = 0; k < words.size(); ++k) view.Write(address + 4 * uint32_t(k), words[k]);
    }
    // continueFromFlagObjCheck with the cave's profile test inlined (our own rewrite).
    static const uint32_t kOriginal[18] = {0xC0210008, 0xC00300AC, 0xFC010040, 0x4C401382, 0x40820038, 0xFC00F840,
                                           0x4C401382, 0x4082002C, 0xC021000C, 0xC00300B0, 0xFC010040, 0x4C411382,
                                           0x40820018, 0xFC00F040, 0x4C411382, 0x4082000C, 0x38600000, 0x4800001C};
    const uint32_t base = 0x807EBC1Cu, skip = 0x807EBC64u, ret = 0x807EBC7Cu;
    uint32_t words[18] = {0xC0210008, 0xC00300AC, 0xFC010040, 0, 0xFC00F840, 0, 0xC021000C, 0xC00300B0,
                          0xFC010040, 0, 0xFC00F040, 0, 0xA0030008, 0x28000193, 0, 0x38600000, 0, 0x60000000};
    const std::pair<int, uint32_t> branches[] = {{3, 0x41810000}, {5, 0x41810000}, {9, 0x41800000}, {11, 0x41800000}, {14, 0x40820000}};
    for (const auto& [k, op] : branches) words[k] = op | ((skip - (base + 4 * k)) & 0xFFFC);
    words[16] = 0x48000000u | ((ret - (base + 64)) & 0x03FFFFFC);
    for (int k = 0; k < 18; ++k)
        if (view.Read(base + 4 * k) != kOriginal[k]) Fail("This NSMB Wii disc doesn't match the MSMBWii patch.");
    for (int k = 0; k < 18; ++k) view.Write(base + 4 * k, words[k]);
    // The mod's save name ("wiimj2d." -> "another.") where the original bytes are present.
    for (uint32_t address : {0x802F148Cu, 0x802F118Cu})
        if (view.Read(address) == 0x7769696Du && view.Read(address + 4) == 0x6A32642Eu) {
            view.Write(address, 0x616E6F74u);
            view.Write(address + 4, 0x6865722Eu);
        }
}

} // namespace

void Load(const Game& game, const Section* sections, size_t count) {
    g_game = &game;
    const fs::path root = RuntimeConfigFile::ResolvedDvdRoot();
    std::error_code ec;
    if (root.empty() || !fs::is_regular_file(root / "sys" / "main.dol", ec))
        Fail("The game files haven't been imported yet.");
    std::cout << "[wiids] Rebuilding " << count << " game sections from your disc..." << std::endl;

    const Bytes dol = ReadFile(root / "sys" / "main.dol");
    std::vector<Bytes> built(count);
    for (size_t i = 0; i < count; ++i) {
        const Section& s = sections[i];
        switch (s.source) {
            case Source::DolSection: built[i] = DolSection(dol, s.address, s.size); break;
            case Source::StaticRel: built[i] = BuildRelImage(ReadFile(root / "files" / "rel" / "StaticR.rel"), s.address, s.size); break;
            case Source::NsmbwLinkedRels: built[i] = NsmbwLinkedImage(root, s.size); break;
        }
    }
    if (game.patch == Patch::Msmbwii) {
        GuestView view;
        for (size_t i = 0; i < count; ++i) view.parts.emplace_back(sections[i].address, &built[i]);
        ApplyMsmbwii(root, view);
    }
    for (size_t i = 0; i < count; ++i) {
        const Section& s = sections[i];
        if (built[i].size() != s.size || HashOf(built[i]) != s.sha256)
            Fail(std::string("Your disc's game code doesn't match this build (section ") + s.name + ").");
        if (!Memory::Contains(s.address, s.size)) Fail("Guest memory is not ready for the game.");
        std::memcpy(Memory::GetPointer(s.address, s.size), built[i].data(), s.size);
    }
    std::cout << "[wiids] All game sections verified and loaded." << std::endl;
}

} // namespace WiiDSGameData
