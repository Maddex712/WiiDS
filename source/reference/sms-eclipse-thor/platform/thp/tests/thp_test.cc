// Host test for the THP decoders: reads a movie from the user's disc
// (read-only), decodes video frames with THPVideoDecode into GX I8 tiles,
// untiles and converts to RGB PNGs, and decodes all audio with
// THPAudioDecode into a WAV whose length must match the header.
//
//   make -C platform/thp/tests run DISC=.../files [MOVIE=data/openingA.thp]
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;

extern "C" {
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work);
u32 THPAudioDecode(s16* audioBuffer, u8* audioFrame, s32 flag);
int THPInit(void);
void DCZeroRange(void* p, u32 n) { memset(p, 0, n); }
void DCFlushRange(void*, u32) { }
}

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

// --- PNG writer (stored deflate blocks) ------------------------------------------

static u32 crc_table[256];
static u32 crc(const u8* p, size_t n, u32 c = 0xFFFFFFFF)
{
	if (!crc_table[1])
		for (u32 i = 0; i < 256; i++) {
			u32 v = i;
			for (int k = 0; k < 8; k++)
				v = (v & 1) ? 0xEDB88320 ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	for (size_t i = 0; i < n; i++)
		c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c;
}

static void put32(std::vector<u8>& v, u32 x)
{
	v.push_back(x >> 24);
	v.push_back(x >> 16);
	v.push_back(x >> 8);
	v.push_back(x);
}

static void chunk(FILE* f, const char* type, const std::vector<u8>& data)
{
	std::vector<u8> b;
	put32(b, data.size());
	b.insert(b.end(), type, type + 4);
	b.insert(b.end(), data.begin(), data.end());
	put32(b, crc(&b[4], b.size() - 4) ^ 0xFFFFFFFF);
	fwrite(b.data(), 1, b.size(), f);
}

static void write_png(const char* path, const u8* rgb, int w, int h)
{
	std::vector<u8> raw;
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), rgb + y * w * 3, rgb + (y + 1) * w * 3);
	}
	std::vector<u8> z = { 0x78, 0x01 };
	u32 a = 1, b2 = 0;
	for (size_t i = 0; i < raw.size(); i++) {
		a  = (a + raw[i]) % 65521;
		b2 = (b2 + a) % 65521;
	}
	for (size_t off = 0; off < raw.size(); off += 65535) {
		size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
		z.push_back(off + n == raw.size());
		z.push_back(n & 0xFF);
		z.push_back(n >> 8);
		z.push_back(~n & 0xFF);
		z.push_back((~n >> 8) & 0xFF);
		z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
	}
	put32(z, (b2 << 16) | a);
	FILE* f = fopen(path, "wb");
	static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);
	std::vector<u8> ihdr;
	put32(ihdr, w);
	put32(ihdr, h);
	ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });
	chunk(f, "IHDR", ihdr);
	chunk(f, "IDAT", z);
	chunk(f, "IEND", {});
	fclose(f);
}

// GX I8: 8x4-pixel tiles, row-major tiles.
static u8 i8_at(const u8* tex, int w, int x, int y)
{
	return tex[(y / 4) * (w * 4) + (x / 8) * 32 + (y % 4) * 8 + (x % 8)];
}

static u8 clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (u8)lrintf(v); }

int main(int argc, char** argv)
{
	std::string out  = argc > 1 ? argv[1] : "out";
	if (!getenv("DISC")) {
		fprintf(stderr, "set DISC=<the disc's extracted files/ folder>\n");
		return 2;
	}
	std::string disc = getenv("DISC");
	std::string mov  = getenv("MOVIE") ? getenv("MOVIE") : "data/openingA.thp";
	FILE* f          = fopen((disc + "/" + mov).c_str(), "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", mov.c_str());
		return 1;
	}
	fseek(f, 0, SEEK_END);
	std::vector<u8> d(ftell(f));
	fseek(f, 0, SEEK_SET);
	if (fread(d.data(), 1, d.size(), f) != d.size())
		return 1;
	fclose(f);

	int fails = 0;
	if (memcmp(d.data(), "THP", 4) || be32(&d[4]) != 0x11000) {
		fprintf(stderr, "not a THP 1.1 file\n");
		return 1;
	}
	u32 frames = be32(&d[0x14]), first = be32(&d[0x18]), comp = be32(&d[0x20]), data = be32(&d[0x28]);
	float fps;
	u32 fr = be32(&d[0x10]);
	memcpy(&fps, &fr, 4);
	u32 ncomp = be32(&d[comp]);
	u32 w = 0, h = 0, channels = 0, freq = 0, samples = 0, tracks = 0, info = comp + 0x14;
	for (u32 c = 0; c < ncomp; c++) {
		if (d[comp + 4 + c] == 0) {
			w = be32(&d[info]);
			h = be32(&d[info + 4]);
			info += 12;
		} else {
			channels = be32(&d[info]);
			freq     = be32(&d[info + 4]);
			samples  = be32(&d[info + 8]);
			tracks   = be32(&d[info + 12]);
			info += 16;
		}
	}
	printf("%s: %ux%u, %u frames at %.3f fps; audio %u ch %u Hz, %u samples, %u tracks\n", mov.c_str(), w, h,
	       frames, fps, channels, freq, samples, tracks);

	THPInit();
	std::vector<u8> ty(w * h), tu(w * h / 4), tv(w * h / 4), work(0x10000);
	std::vector<u8> rgb(w * h * 3);
	std::vector<s16> pcm, abuf(0x10000);
	u32 dumps[] = { 1, frames / 4, frames / 2, frames * 3 / 4, frames - 1 };
	u32 pos = data, size = first, decoded = 0;
	for (u32 i = 0; i < frames && pos + size <= d.size(); i++) {
		std::vector<u8> fb(d.begin() + pos, d.begin() + pos + size); // like the read buffer
		u32 next = be32(&fb[0]);
		u8* p    = &fb[8 + ncomp * 4];
		for (u32 c = 0; c < ncomp; c++) {
			u32 cs = be32(&fb[8 + c * 4]);
			if (d[comp + 4 + c] == 0) {
				s32 err = THPVideoDecode(p, ty.data(), tu.data(), tv.data(), work.data());
				if (err) {
					if (fails++ < 5)
						fprintf(stderr, "frame %u: THPVideoDecode error %d\n", i, err);
					continue;
				}
				decoded++;
				bool dump = false;
				for (u32 k : dumps)
					dump |= k == i;
				if (dump) {
					long sum = 0;
					for (u32 y = 0; y < h; y++)
						for (u32 x = 0; x < w; x++) {
							float Y = i8_at(ty.data(), w, x, y);
							float U = i8_at(tu.data(), w / 2, x / 2, y / 2) - 128.0f;
							float V = i8_at(tv.data(), w / 2, x / 2, y / 2) - 128.0f;
							u8* o   = &rgb[(y * w + x) * 3];
							o[0]    = clamp8(Y + 1.402f * V);
							o[1]    = clamp8(Y - 0.344f * U - 0.714f * V);
							o[2]    = clamp8(Y + 1.772f * U);
							sum += (long)Y;
						}
					char path[512];
					snprintf(path, sizeof(path), "%s/frame%04u.png", out.c_str(), i);
					write_png(path, rgb.data(), w, h);
					printf("  frame %u -> %s (mean luma %.1f)\n", i, path, (double)sum / (w * h));
				}
			} else {
				// all tracks are the same length; the game plays track 0
				u32 n = THPAudioDecode(abuf.data(), p, 0);
				pcm.insert(pcm.end(), abuf.begin(), abuf.begin() + n * 2);
				cs *= tracks;
			}
			p += cs;
		}
		pos += size;
		size = next;
	}

	// WAV (16-bit stereo, interleaved as THPAudioDecode(flag 0) writes it)
	u32 nsamp = pcm.size() / 2;
	std::string wav = out + "/audio.wav";
	FILE* wf        = fopen(wav.c_str(), "wb");
	u32 bytes       = pcm.size() * 2;
	u32 hdr[]       = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001, freq, freq * 4, 0x00100004,
		                0x61746164, bytes };
	fwrite(hdr, 4, 11, wf);
	fwrite(pcm.data(), 2, pcm.size(), wf);
	fclose(wf);
	double peak = 0, rms = 0;
	for (s16 s : pcm) {
		peak = fabs(s) > peak ? fabs(s) : peak;
		rms += (double)s * s;
	}
	rms = pcm.empty() ? 0 : sqrt(rms / pcm.size());
	printf("video: %u/%u frames decoded; audio: %u samples (header %u) = %.2f s vs video %.2f s, peak %.0f rms %.0f -> %s\n",
	       decoded, frames, nsamp, samples, freq ? (double)nsamp / freq : 0, frames / fps, peak, rms, wav.c_str());
	if (decoded != frames)
		fails++;
	if (samples && nsamp != samples) {
		fprintf(stderr, "audio length %u != header %u\n", nsamp, samples);
		fails++;
	}
	return fails ? 1 : 0;
}
