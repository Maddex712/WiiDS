// Window icon: the game's memory-card icon (card/mario_icon.bti in
// /data/common.szs, a 32x32 Mario head, C8 with an RGB5A3 palette, two
// animation frames stacked), read from the game source at startup and scaled
// up without smoothing. tools/extract_icon.py makes the SMS.app and .exe icons
// from the same image at build time; nothing of it is kept in the repository.
#include "port_platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vector>

extern "C" __attribute__((weak)) void GXPC_SetWindowIcon(const uint8_t* rgba, int w, int h);
#ifdef __APPLE__
extern "C" int gcdisc_self_path(char* buf, uint32_t bufsize);
#endif

namespace {

typedef std::vector<uint8_t> Bytes;

const int kFrame = 32;
const int kSize  = 256;
#ifdef __APPLE__
// A Dock icon leaves a margin around the artwork, like the SMS.app icon.
const int kArt = 192;
#else
const int kArt = 256;
#endif

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool yaz0(const Bytes& src, Bytes& out)
{
	if (src.size() < 16 || memcmp(src.data(), "Yaz0", 4) != 0) {
		out = src;
		return true;
	}
	uint32_t size = be32(&src[4]);
	out.clear();
	out.reserve(size);
	size_t s = 16;
	while (out.size() < size) {
		if (s >= src.size())
			return false;
		uint8_t code = src[s++];
		for (int bit = 0; bit < 8 && out.size() < size; bit++) {
			if (code & (0x80 >> bit)) {
				if (s >= src.size())
					return false;
				out.push_back(src[s++]);
				continue;
			}
			if (s + 1 >= src.size())
				return false;
			uint8_t b1 = src[s], b2 = src[s + 1];
			s += 2;
			size_t dist = ((size_t)(b1 & 0xF) << 8 | b2) + 1;
			size_t n    = b1 >> 4;
			if (n == 0) {
				if (s >= src.size())
					return false;
				n = src[s++] + 0x12;
			} else {
				n += 2;
			}
			if (dist > out.size())
				return false;
			for (size_t i = 0; i < n; i++)
				out.push_back(out[out.size() - dist]);
		}
	}
	return true;
}

// File `name` in RARC directory `dir` (names compared case-insensitively).
bool rarc_file(const Bytes& arc, const char* dir, const char* name, Bytes& out)
{
	if (arc.size() < 0x40 || memcmp(arc.data(), "RARC", 4) != 0)
		return false;
	const uint8_t* a = arc.data();
	const size_t info = 0x20;
	uint32_t nodes = be32(a + info), nodes_off = be32(a + info + 4) + info;
	uint32_t ents_off = be32(a + info + 12) + info, str_off = be32(a + info + 20) + info;
	uint32_t data = be32(a + 0xC) + info;
	auto str = [&](uint32_t o) -> const char* {
		o += str_off;
		return o < arc.size() && memchr(a + o, 0, arc.size() - o) ? (const char*)a + o : "";
	};
	for (uint32_t n = 0; n < nodes; n++) {
		uint32_t node = nodes_off + n * 16;
		if (node + 16 > arc.size() || strcasecmp(str(be32(a + node + 4)), dir) != 0)
			continue;
		uint32_t first = be32(a + node + 12), count = be16(a + node + 10);
		for (uint32_t e = first; e < first + count; e++) {
			uint32_t ent = ents_off + e * 20;
			if (ent + 20 > arc.size() || strcasecmp(str(be32(a + ent + 4) & 0xFFFFFF), name) != 0)
				continue;
			uint32_t off = data + be32(a + ent + 8), size = be32(a + ent + 12);
			if (off > arc.size() || size > arc.size() - off)
				return false;
			out.assign(a + off, a + off + size);
			return true;
		}
	}
	return false;
}

void rgb5a3(uint16_t v, uint8_t* px)
{
	if (v & 0x8000) {
		int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
		px[0] = (uint8_t)(r << 3 | r >> 2);
		px[1] = (uint8_t)(g << 3 | g >> 2);
		px[2] = (uint8_t)(b << 3 | b >> 2);
		px[3] = 255;
	} else {
		int a = (v >> 12) & 7;
		px[0] = (uint8_t)(((v >> 8) & 15) * 17);
		px[1] = (uint8_t)(((v >> 4) & 15) * 17);
		px[2] = (uint8_t)((v & 15) * 17);
		px[3] = (uint8_t)(a << 5 | a << 2 | a >> 1);
	}
}

// Frame 0 (the top 32x32) of a C8 / RGB5A3 BTI, as RGBA8.
bool decode_frame(const Bytes& bti, uint8_t* rgba)
{
	if (bti.size() < 0x20 || bti[0] != 9 || bti[9] != 2)
		return false;
	const uint8_t* b = bti.data();
	int w = be16(b + 2), h = be16(b + 4), npal = be16(b + 10);
	uint32_t pal = be32(b + 12), img = be32(b + 0x1C);
	if (w != kFrame || h < kFrame || img + (size_t)w * h > bti.size() || pal + (size_t)npal * 2 > bti.size())
		return false;
	size_t o = img;
	for (int ty = 0; ty < kFrame; ty += 4)
		for (int tx = 0; tx < w; tx += 8)
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 8; x++) {
					uint8_t i = b[o++];
					uint8_t* px = rgba + ((ty + y) * kFrame + tx + x) * 4;
					if (i < npal)
						rgb5a3(be16(b + pal + i * 2), px);
					else
						memset(px, 0, 4);
				}
	return true;
}

#ifdef __APPLE__
// SMS.app's own icon (Contents/Resources/SMS.icns) is sharper in the Dock.
bool in_app_bundle()
{
	char self[4096];
	return gcdisc_self_path(self, sizeof self) && strstr(self, ".app/Contents/MacOS/");
}
#endif

} // namespace

extern "C" void port_window_icon_init(void)
{
	if (!GXPC_SetWindowIcon)
		return;
#ifdef __APPLE__
	if (in_app_bundle())
		return;
#endif
	size_t n  = 0;
	void* szs = port_dvd_read_file("/data/common.szs", &n);
	if (!szs)
		return;
	Bytes packed((uint8_t*)szs, (uint8_t*)szs + n), arc, bti;
	free(szs);
	uint8_t frame[kFrame * kFrame * 4];
	if (!yaz0(packed, arc) || !rarc_file(arc, "card", "mario_icon.bti", bti) || !decode_frame(bti, frame)) {
		port_log("[port] no window icon: card/mario_icon.bti not usable\n");
		return;
	}
	std::vector<uint8_t> icon((size_t)kSize * kSize * 4, 0);
	int pad = (kSize - kArt) / 2;
	for (int y = 0; y < kArt; y++)
		for (int x = 0; x < kArt; x++)
			memcpy(&icon[((y + pad) * kSize + x + pad) * 4], frame + ((y * kFrame / kArt) * kFrame + x * kFrame / kArt) * 4, 4);
	GXPC_SetWindowIcon(icon.data(), kSize, kSize);
	port_log("[port] window icon: card/mario_icon.bti\n");
}
