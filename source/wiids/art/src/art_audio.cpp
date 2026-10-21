// Launcher music and UI sounds from the player's Mario Kart Wii disc (sound/revo_kart.brsar),
// ported from scripts/Extract-ThorLauncherAudio.py: the title theme (W_BGM_TITLE, looping
// DSP-ADPCM) and six SE_UI_* sequences rendered with the same small sequence player. numpy's
// float32/float64 steps are kept so the rendered PCM matches the PC tool.
#include "wiids_art_profiles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace wiids::art {
namespace {

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
constexpr int kRate = 32000;

struct Reader {
    const Bytes& b;
    uint8_t U8(size_t o) const { if (o >= b.size()) Fail("Damaged sound archive"); return b[o]; }
    uint16_t U16(size_t o) const { return uint16_t(U8(o) << 8 | U8(o + 1)); }
    uint32_t U32(size_t o) const { return uint32_t(U16(o)) << 16 | U16(o + 2); }
    int32_t I32(size_t o) const { return int32_t(U32(o)); }
    float F32(size_t o) const { const uint32_t v = U32(o); float f; std::memcpy(&f, &v, 4); return f; }
};

// A decoded wave: float32 samples per channel.
struct Wave {
    std::vector<std::vector<float>> pcm;
    int rate = 0;
    bool loop = false;
    long loopStart = 0, loopEnd = 0;
    long Frames() const { return pcm.empty() ? 0 : long(pcm[0].size()); }
};

long NibbleSample(long n) { return (n / 16) * 14 + std::max(0L, n % 16 - 2); }

std::vector<float> DspDecode(const Bytes& data, size_t start, long endNibble, const int16_t coefs[16]) {
    const long samples = NibbleSample(endNibble) + 1;
    const long frames = (samples + 13) / 14;
    if (start + size_t(frames) * 8 > data.size()) Fail("Damaged sound data");
    std::vector<float> out(size_t(frames) * 14);
    int64_t h1 = 0, h2 = 0;
    for (long f = 0; f < frames; ++f) {
        const int header = data[start + size_t(f) * 8];
        const int scale = 1 << (header & 15);
        const int c1 = coefs[(header >> 4) * 2], c2 = coefs[(header >> 4) * 2 + 1];
        for (int k = 0; k < 14; ++k) {
            const int byte = data[start + size_t(f) * 8 + 1 + size_t(k / 2)];
            int nib = k % 2 == 0 ? byte >> 4 : byte & 15;
            if (nib >= 8) nib -= 16;
            int64_t v = (int64_t(nib) * scale * 2048) + 1024 + c1 * h1 + c2 * h2;
            v = std::max<int64_t>(-32768, std::min<int64_t>(32767, v >> 11));
            out[size_t(f) * 14 + size_t(k)] = float(v) / 32768.0f;
            h2 = h1;
            h1 = v;
        }
    }
    out.resize(size_t(samples));
    return out;
}

Wave Rwav(const Bytes& data, size_t base) {
    const Reader r{data};
    if (std::memcmp(&data.at(base), "RWAV", 4) != 0) Fail("Expected a wave");
    const size_t info = base + r.U32(base + 0x10) + 8, body = base + r.U32(base + 0x18) + 8;
    const int fmt = r.U8(info), loop = r.U8(info + 1), channels = r.U8(info + 2);
    Wave w;
    w.rate = (r.U8(info + 3) << 16) | r.U16(info + 4);
    long loopStart = long(r.U32(info + 8)), loopEnd = long(r.U32(info + 12));
    const size_t table = info + r.U32(info + 16);
    for (int c = 0; c < channels; ++c) {
        const size_t ch = info + r.U32(table + 4 * size_t(c));
        const size_t start = body + r.U32(ch);
        if (fmt == 2) {
            const size_t adpcm = info + r.U32(ch + 4);
            int16_t coefs[16];
            for (int k = 0; k < 16; ++k) coefs[k] = int16_t(r.U16(adpcm + 2 * size_t(k)));
            w.pcm.push_back(DspDecode(data, start, loopEnd, coefs));
        } else if (fmt == 1) {
            std::vector<float> v(size_t(loopEnd + 1));
            for (size_t i = 0; i < v.size(); ++i) v[i] = float(int16_t(r.U16(start + 2 * i))) / 32768.0f;
            w.pcm.push_back(std::move(v));
        } else {
            std::vector<float> v(size_t(loopEnd + 1));
            for (size_t i = 0; i < v.size(); ++i) v[i] = float(int8_t(r.U8(start + i))) / 128.0f;
            w.pcm.push_back(std::move(v));
        }
    }
    if (fmt == 2) { loopStart = NibbleSample(loopStart); loopEnd = NibbleSample(loopEnd); }
    size_t n = SIZE_MAX;
    for (const auto& x : w.pcm) n = std::min(n, x.size());
    for (auto& x : w.pcm) x.resize(n);
    w.loop = loop != 0;
    w.loopStart = loopStart;
    w.loopEnd = std::min(loopEnd, long(n) - 1);
    return w;
}

std::vector<Wave> RwarWaves(const Bytes& data) {
    const Reader r{data};
    if (data.size() < 0x20 || std::memcmp(data.data(), "RWAR", 4) != 0) Fail("Expected a wave archive");
    const uint32_t tabl = r.U32(0x10), body = r.U32(0x18);
    std::vector<Wave> waves;
    for (uint32_t k = 0; k < r.U32(tabl + 8); ++k) waves.push_back(Rwav(data, body + r.U32(tabl + 16 + 12 * k)));
    return waves;
}

class Rsar {
public:
    explicit Rsar(const Bytes& data) : b_(data), r_{data} {
        if (data.size() < 0x20 || std::memcmp(data.data(), "RSAR", 4) != 0) Fail("Not a sound archive");
        const size_t symb = r_.U32(0x10), info = r_.U32(0x18);
        const size_t s = symb + 8, strtab = s + r_.U32(s);
        for (uint32_t i = 0; i < r_.U32(strtab); ++i) {
            const size_t o = s + r_.U32(strtab + 4 + 4 * i);
            size_t e = o;
            while (e < b_.size() && b_[e]) ++e;
            names_.emplace_back(b_.begin() + std::ptrdiff_t(o), b_.begin() + std::ptrdiff_t(e));
        }
        i_ = info + 8;
        sounds_ = Table(Ref(i_));
        banks_ = Table(Ref(i_ + 8));
        files_ = Table(Ref(i_ + 24));
        groups_ = Table(Ref(i_ + 32));
        for (size_t k = 0; k < sounds_.size(); ++k) {
            const uint32_t sid = r_.U32(i_ + sounds_[k]);
            if (sid < names_.size()) byName_[names_[sid]] = k;
        }
    }
    const Reader& R() const { return r_; }
    size_t I() const { return i_; }
    uint32_t Bank(uint32_t index) const { return banks_.at(index); }

    std::pair<Bytes, Bytes> File(uint32_t fileId) const {
        const size_t fo = i_ + files_.at(fileId);
        const auto positions = Table(Ref(fo + 20));
        if (positions.empty()) Fail("Sound file is not in any group");
        const uint32_t gid = r_.U32(i_ + positions[0]), index = r_.U32(i_ + positions[0] + 4);
        const size_t go = i_ + groups_.at(gid);
        const size_t goff = r_.U32(go + 16), gwoff = r_.U32(go + 24);
        const size_t it = i_ + Table(Ref(go + 32)).at(index);
        const size_t off = r_.U32(it + 4), size = r_.U32(it + 8), woff = r_.U32(it + 12), wsize = r_.U32(it + 16);
        if (goff + off + size > b_.size() || gwoff + woff + wsize > b_.size()) Fail("Damaged sound archive");
        return {Bytes(b_.begin() + std::ptrdiff_t(goff + off), b_.begin() + std::ptrdiff_t(goff + off + size)),
                Bytes(b_.begin() + std::ptrdiff_t(gwoff + woff), b_.begin() + std::ptrdiff_t(gwoff + woff + wsize))};
    }
    struct Sound { uint32_t fileId; int kind; size_t detail; };
    Sound Find(const std::string& name) const {
        const auto it = byName_.find(name);
        if (it == byName_.end()) Fail("The disc's sound archive has no " + name);
        const size_t o = i_ + sounds_[it->second];
        return {r_.U32(o + 4), r_.U8(o + 22), i_ + Ref(o + 24)};
    }

private:
    uint32_t Ref(size_t o) const { return r_.U32(o + 4); }
    std::vector<uint32_t> Table(size_t offset) const {
        const size_t o = i_ + offset;
        std::vector<uint32_t> out;
        for (uint32_t k = 0; k < r_.U32(o); ++k) out.push_back(r_.U32(o + 4 + 8 * k + 4));
        return out;
    }
    const Bytes& b_;
    Reader r_;
    size_t i_ = 0;
    std::vector<std::string> names_;
    std::vector<uint32_t> sounds_, banks_, files_, groups_;
    std::map<std::string, size_t> byName_;
};

struct Instrument { int32_t wave; int a, d, s, r, key, vol, pan; double tune; };

class BankFile {
public:
    BankFile(Bytes data, std::vector<Wave> waves) : b_(std::move(data)), r_{b_}, waves(std::move(waves)) {
        if (b_.size() < 0x20 || std::memcmp(b_.data(), "RBNK", 4) != 0) Fail("Expected a bank");
        d_ = r_.U32(16) + 8;
    }
    std::optional<Instrument> Lookup(int prg, int key, int vel) const {
        if (prg < 0 || uint32_t(prg) >= r_.U32(d_)) return std::nullopt;
        int kind = r_.U8(d_ + 4 + 8 * size_t(prg) + 1);
        uint32_t off = r_.U32(d_ + 4 + 8 * size_t(prg) + 4);
        for (int depth = 0; depth < 4; ++depth) {
            const size_t p = d_ + off;
            if (kind == 1)
                return Instrument{r_.I32(p), r_.U8(p + 4), r_.U8(p + 5), r_.U8(p + 6), r_.U8(p + 7),
                                  r_.U8(p + 12), r_.U8(p + 13), r_.U8(p + 14), double(r_.F32(p + 16))};
            const int probe = depth == 0 ? key : vel;
            if (kind == 2) {
                const int n = r_.U8(p);
                const size_t refs = p + ((1 + size_t(n) + 3) & ~size_t(3));
                int i = n - 1;
                for (int k = 0; k < n; ++k) if (probe <= r_.U8(p + 1 + size_t(k))) { i = k; break; }
                kind = r_.U8(refs + 8 * size_t(i) + 1);
                off = r_.U32(refs + 8 * size_t(i) + 4);
            } else if (kind == 3) {
                const int lo = r_.U8(p), hi = r_.U8(p + 1);
                const int k = std::min(std::max(probe, lo), hi) - lo;
                kind = r_.U8(p + 4 + 8 * size_t(k) + 1);
                off = r_.U32(p + 4 + 8 * size_t(k) + 4);
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

private:
    Bytes b_;
    Reader r_;
    size_t d_ = 0;

public:
    std::vector<Wave> waves;
};

double EnvSeconds(int v, double longest) { return v >= 127 ? 0.0 : std::pow((127 - v) / 127.0, 2) * longest; }

struct Note { double t, dur; int key, vel, vol, pan, prg; std::array<int, 4> adsr; };

// Interleaved-by-channel float32 mix, 2 channels.
using Mix = std::array<std::vector<float>, 2>;

Mix RenderSequence(const Rsar& rsar, const std::string& name) {
    const auto sound = rsar.Find(name);
    if (sound.kind != 1) Fail(name + " is not a sequence");
    const auto [seq, unused] = rsar.File(sound.fileId);
    (void)unused;
    if (seq.size() < 0x20 || std::memcmp(seq.data(), "RSEQ", 4) != 0) Fail(name + ": unexpected sequence file");
    const Reader r = rsar.R();
    const uint32_t start = r.U32(sound.detail);
    const size_t bankInfo = rsar.I() + rsar.Bank(r.U32(sound.detail + 4));
    auto [bankBytes, bankWaves] = rsar.File(r.U32(bankInfo + 4));
    const BankFile bank(std::move(bankBytes), RwarWaves(bankWaves));
    const Reader s{seq};
    const size_t data = s.U32(16);
    const size_t codeAt = data + s.U32(data + 8);
    const Bytes code(seq.begin() + std::ptrdiff_t(codeAt), seq.end());
    auto byte = [&](size_t pc) -> int { if (pc >= code.size()) Fail(name + ": sequence ends early"); return code[pc]; };
    auto varlen = [&](size_t& pc) { uint32_t v = 0; while (true) { const int c = byte(pc++); v = (v << 7) | (c & 0x7F); if (c < 0x80) return v; } };
    auto u24 = [&](size_t pc) { return size_t(byte(pc)) << 16 | size_t(byte(pc + 1)) << 8 | size_t(byte(pc + 2)); };

    struct Track {
        size_t pc; double t; int prg, vol, pan, trans; bool wait;
        std::array<int, 4> adsr; // -1 = unset
        bool hasAdsr;
        std::vector<size_t> stack;
        std::vector<std::pair<size_t, int>> loops;
    };
    int tempo = 120;
    auto ticks = [&](double n) { return n * 60.0 / (tempo * 48); };
    std::deque<Track> tracks;
    tracks.push_back({start, 0.0, 0, 127, 64, 0, true, {-1, -1, -1, -1}, false, {}, {}});
    std::vector<Note> notes;
    int guard = 0;
    while (!tracks.empty()) {
        Track tr = tracks.front();
        tracks.pop_front();
        while (guard < 20000 && tr.t < 4.0) {
            ++guard;
            size_t pc = tr.pc;
            const int c = byte(pc++);
            bool stop = false;
            if (c < 0x80) {
                const int vel = byte(pc);
                size_t p2 = pc + 1;
                const uint32_t length = varlen(p2);
                pc = p2;
                notes.push_back({tr.t, ticks(length), c + tr.trans, vel, tr.vol, tr.pan, tr.prg, tr.adsr});
                if (tr.wait) tr.t += ticks(length);
            } else if (c == 0x80) { tr.t += ticks(varlen(pc)); }
            else if (c == 0x81) { tr.prg = int(varlen(pc)); }
            else if (c == 0x88) { Track sub = tr; sub.pc = u24(pc + 1); sub.stack.clear(); sub.loops.clear(); pc += 4; tracks.push_back(sub); }
            else if (c == 0x89) { pc = u24(pc); }
            else if (c == 0x8a) { tr.stack.push_back(pc + 3); pc = u24(pc); }
            else if (c == 0xfd) { if (tr.stack.empty()) stop = true; else { pc = tr.stack.back(); tr.stack.pop_back(); } }
            else if (c == 0xff) { stop = true; }
            else if (c == 0xfe) { pc += 2; }
            else if (c == 0xc1) { tr.vol = byte(pc++); }
            else if (c == 0xc0) { tr.pan = byte(pc++); }
            else if (c == 0xc3) { tr.trans = int8_t(byte(pc++)); }
            else if (c == 0xc7) { tr.wait = byte(pc++) != 0; }
            else if (c >= 0xd0 && c <= 0xd3) { tr.adsr[size_t(c - 0xd0)] = byte(pc++); }
            else if (c == 0xd4) { tr.loops.push_back({pc + 1, byte(pc)}); pc += 1; }
            else if (c == 0xfc) {
                if (!tr.loops.empty()) {
                    auto& loop = tr.loops.back();
                    if (loop.second == 0 || loop.second > 1) { if (loop.second > 1) --loop.second; pc = loop.first; }
                    else tr.loops.pop_back();
                }
            }
            else if (c == 0xe1) { const int v = byte(pc) << 8 | byte(pc + 1); tempo = v ? v : 120; pc += 2; }
            else if (c == 0xe0 || c == 0xe3) { pc += 2; }
            else if (c >= 0xc2 && c <= 0xdf) { pc += 1; }
            else if (c >= 0xb0 && c <= 0xbf) { pc += 3; }
            else if (c == 0xa0) { pc += byte(pc) == 0xb0 ? 5 : 4; }
            else if (c == 0xa1 || c == 0xa2 || c == 0xa3) {}
            else if (c == 0xf0) { const int sub = byte(pc++); pc += (sub >= 0x80 && sub <= 0x9f) ? 3 : 2; }
            else Fail(name + ": unsupported sequence command");
            if (stop) break;
            tr.pc = pc;
        }
    }
    if (notes.empty()) Fail(name + ": sequence produced no notes");
    double end = 0;
    for (const Note& n : notes) end = std::max(end, n.t + n.dur);
    end += 1.5;
    const size_t total = size_t(end * kRate) + 1;
    Mix mix = {std::vector<float>(total, 0.0f), std::vector<float>(total, 0.0f)};
    for (const Note& note : notes) {
        const auto inst = bank.Lookup(note.prg, note.key, note.vel);
        if (!inst || inst->wave < 0 || size_t(inst->wave) >= bank.waves.size()) continue;
        const Wave& w = bank.waves[size_t(inst->wave)];
        const int a = note.adsr[0] >= 0 ? note.adsr[0] : inst->a, d = note.adsr[1] >= 0 ? note.adsr[1] : inst->d;
        const int sv = note.adsr[2] >= 0 ? note.adsr[2] : inst->s, rv = note.adsr[3] >= 0 ? note.adsr[3] : inst->r;
        const double step = std::pow(2.0, (note.key - inst->key) / 12.0) * inst->tune * w.rate / kRate;
        const double held = note.dur + EnvSeconds(rv, 2.5) + 0.02;
        long n = long(held * kRate);
        const long frames = w.Frames();
        const bool looping = w.loop && w.loopEnd > w.loopStart;
        if (!looping) n = std::min(n, long((frames - 1) / step));
        if (n <= 0) continue;
        const double at = EnvSeconds(a, 1.2), dt = EnvSeconds(d, 3.0) + 0.005, rt = EnvSeconds(rv, 2.5) + 0.005;
        const double sus = std::pow(sv / 127.0, 2);
        auto envAt = [&](double tt) {
            double env = tt < at ? tt / std::max(at, 1e-4) : 1.0;
            if (tt >= at) env = sus + (1 - sus) * std::exp(-(tt - at) / dt * 3);
            return env;
        };
        const double offEnv = envAt(double(std::min(long(note.dur * kRate), n - 1)) / kRate);
        const double gain = std::pow(note.vel / 127.0, 2) * std::pow(note.vol / 127.0, 2) * std::pow(inst->vol / 127.0, 2);
        double p = ((note.pan - 64) + (inst->pan - 64)) / 64.0;
        p = std::max(-1.0, std::min(1.0, p));
        // float32 array times a Python float: the multiply happens in float32.
        const float root2 = float(std::sqrt(2.0));
        const float lr[2] = {float(std::cos((p + 1) * 3.141592653589793 / 4)) * root2,
                             float(std::sin((p + 1) * 3.141592653589793 / 4)) * root2};
        const long s0 = long(note.t * kRate);
        for (long i = 0; i < n; ++i) {
            double pos = double(i) * step;
            if (looping && pos > w.loopEnd) pos = w.loopStart + std::fmod(pos - w.loopStart, double(w.loopEnd - w.loopStart));
            const long i0 = long(pos);
            const float frac = float(pos - double(i0));
            const long i1 = std::min(i0 + 1, frames - 1);
            const double tt = double(i) / kRate;
            double env = envAt(tt);
            if (tt >= note.dur) env = offEnv * std::exp(-std::min((tt - note.dur) / rt * 4, 60.0));
            const size_t at2 = size_t(s0 + i);
            if (at2 >= total) break;
            for (int ch = 0; ch < 2; ++ch) {
                const auto& src = w.pcm[w.pcm.size() == 1 ? 0 : size_t(ch)];
                const float voice = src[size_t(i0)] * (1.0f - frac) + src[size_t(i1)] * frac;
                const double v = double(voice) * env * gain * double(lr[ch]);
                mix[size_t(ch)][at2] = float(double(mix[size_t(ch)][at2]) + v);
            }
        }
    }
    float peak = 0;
    for (const auto& ch : mix) for (float v : ch) peak = std::max(peak, std::fabs(v));
    if (peak <= 1e-4f) Fail(name + ": rendered silence");
    const float norm = float(0.7 / double(peak));
    for (auto& ch : mix) for (float& v : ch) v *= norm;
    long last = -1;
    for (size_t i = 0; i < total; ++i) if (std::max(std::fabs(mix[0][i]), std::fabs(mix[1][i])) > 4e-3f) last = long(i);
    if (last >= 0) for (auto& ch : mix) ch.resize(std::min(ch.size(), size_t(last + kRate / 50)));
    const size_t cap = size_t(1.6 * kRate);
    if (mix[0].size() > cap) {
        const size_t fade = size_t(0.15 * kRate);
        for (auto& ch : mix) {
            ch.resize(cap);
            const double stepF = -1.0 / double(fade - 1);
            for (size_t i = 0; i < fade; ++i) {
                const float f = i + 1 == fade ? 0.0f : float(double(i) * stepF + 1.0);
                ch[cap - fade + i] *= f;
            }
        }
    }
    return mix;
}

Bytes Wav(const std::vector<std::vector<float>>& pcm, int rate, size_t frames) {
    const uint16_t channels = uint16_t(pcm.size());
    const uint32_t dataSize = uint32_t(frames * channels * 2);
    Bytes out;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
    out.insert(out.end(), {'R', 'I', 'F', 'F'}); u32(36 + dataSize);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); u32(16); u16(1); u16(channels);
    u32(uint32_t(rate)); u32(uint32_t(rate) * channels * 2); u16(uint16_t(channels * 2)); u16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'}); u32(dataSize);
    for (size_t i = 0; i < frames; ++i)
        for (const auto& ch : pcm) {
            const float v = std::min(std::max(ch[i], -1.0f), 1.0f) * 32767.0f;
            u16(uint16_t(int16_t(v)));
        }
    return out;
}

} // namespace

std::string BuildMarioKartAudio(const std::string& discRoot, const std::string& outDir) {
    const Bytes brsar = ReadFile(discRoot + "/files/sound/revo_kart.brsar");
    const Rsar rsar(brsar);
    std::filesystem::create_directories(std::filesystem::u8path(outDir));
    Json sounds = Json::Object();
    const auto music = rsar.Find("W_BGM_TITLE");
    if (music.kind != 3) Fail("Title music is not a wave sound");
    Wave title = RwarWaves(rsar.File(music.fileId).second).at(0);
    for (auto& ch : title.pcm) ch.resize(size_t(title.loopEnd + 1));
    const Bytes musicWav = Wav(title.pcm, title.rate, size_t(title.loopEnd + 1));
    WriteFile(outDir + "/music.wav", musicWav.data(), musicWav.size());
    Json musicRecord = Json::Object().Set("file", Json::Str("music.wav")).Set("sha256", Json::Str(Sha256Hex(musicWav)))
        .Set("source", Json::Str("W_BGM_TITLE")).Set("rate", Json::Num(title.rate))
        .Set("channels", Json::Num(long(title.pcm.size()))).Set("frames", Json::Num(title.loopEnd + 1))
        .Set("loopStart", Json::Num(title.loop ? title.loopStart : 0)).Set("loopEnd", Json::Num(title.loopEnd));
    const std::pair<const char*, const char*> roles[] = {
        {"focus", "SE_UI_BTN_IN"}, {"open", "SE_UI_CTRL_WIN_POPUP"}, {"close", "SE_UI_CTRL_WIN_POPDOWN"},
        {"select", "SE_UI_BTN_OK"}, {"launch", "SE_UI_TITLE_OUT"}, {"reject", "SE_UI_CTRL_REJECT"}};
    for (const auto& [role, name] : roles) {
        const Mix mix = RenderSequence(rsar, name);
        const std::vector<std::vector<float>> pcm = {mix[0], mix[1]};
        const Bytes wav = Wav(pcm, kRate, mix[0].size());
        WriteFile(outDir + "/" + role + ".wav", wav.data(), wav.size());
        sounds.Set(role, Json::Object().Set("file", Json::Str(std::string(role) + ".wav")).Set("sha256", Json::Str(Sha256Hex(wav)))
            .Set("source", Json::Str(name)).Set("frames", Json::Num(long(mix[0].size()))));
    }
    const std::string manifest = Json::Object().Set("schema", Json::Num(1)).Set("kind", Json::Str("private-launcher-audio"))
        .Set("source", Json::Str("player's Mario Kart Wii disc: sound/revo_kart.brsar")).Set("sounds", std::move(sounds))
        .Set("music", std::move(musicRecord)).Dump() + "\n";
    WriteFile(outDir + "/audio.json", manifest.data(), manifest.size());
    return "Mario Kart launcher audio built";
}

} // namespace wiids::art
