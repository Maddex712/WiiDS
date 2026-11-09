// Makes the key list a game ships (gcn_add_game's SHADER_KEYS; see render_set_shader_keys
// in runtime/gx/render_gl.h) from shader caches that runs have left behind, and from key
// lists made before:
//
//   shader_keys OUT.bin IN.bin...
//
// Every key is made canonical by the renderer's own canonicalize_shader_key and kept once,
// in the order first seen -- the order the game first asked for them, so a load that
// stops early has the earliest -- and the binaries are left behind: they belong to one
// driver. Built by the `shader_keys` target of a desktop build.
#include "gx/shadergen.h"
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

using namespace gx;

static constexpr uint32_t kCacheMagic = 0x43535257;  // render_gl.cpp's

static bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const bool ok = out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s OUT.bin IN.bin...\n", argv[0]);
        return 2;
    }
    std::vector<ShaderKey> keys;
    std::unordered_set<ShaderKey, ShaderKeyHash> seen;
    auto add = [&](ShaderKey k) {
        canonicalize_shader_key(k);
        if (seen.insert(k).second) keys.push_back(k);
    };
    for (int i = 2; i < argc; i++) {
        std::vector<uint8_t> d;
        if (!read_file(argv[i], d)) {
            fprintf(stderr, "%s: cannot read\n", argv[i]);
            return 1;
        }
        uint32_t magic = 0, ver = 0;
        if (d.size() >= 8) {
            memcpy(&magic, d.data(), 4);
            memcpy(&ver, d.data() + 4, 4);
        }
        if ((magic != kCacheMagic && magic != kShaderKeysMagic) || ver != kShaderKeyFormat) {
            fprintf(stderr, "%s: neither a shader cache nor a key list in format %08x\n", argv[i], kShaderKeyFormat);
            return 1;
        }
        const size_t before = keys.size();
        size_t records = 0;
        if (magic == kShaderKeysMagic) {
            for (size_t o = 8; o + sizeof(ShaderKey) <= d.size(); o += sizeof(ShaderKey), records++) {
                ShaderKey k;
                memcpy(&k, d.data() + o, sizeof(k));
                add(k);
            }
        } else {
            // The cache's records: the key, the source's hash (8 bytes), the binary's format
            // and length (4 each), the binary. A truncated last record is dropped.
            size_t o = 16;
            while (o + sizeof(ShaderKey) + 16 <= d.size()) {
                ShaderKey k;
                uint32_t len;
                memcpy(&k, d.data() + o, sizeof(k));
                memcpy(&len, d.data() + o + sizeof(k) + 12, 4);
                const size_t next = o + sizeof(k) + 16 + len;
                if (next > d.size()) break;
                add(k);
                records++;
                o = next;
            }
        }
        printf("%s: %zu records, %zu new keys\n", argv[i], records, keys.size() - before);
    }
    FILE* f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "%s: cannot write\n", argv[1]);
        return 1;
    }
    const uint32_t hdr[2] = {kShaderKeysMagic, kShaderKeyFormat};
    fwrite(hdr, sizeof(hdr), 1, f);
    if (!keys.empty()) fwrite(keys.data(), sizeof(ShaderKey), keys.size(), f);
    fclose(f);
    printf("%s: %zu keys, %zu bytes\n", argv[1], keys.size(), 8 + keys.size() * sizeof(ShaderKey));
    return 0;
}
