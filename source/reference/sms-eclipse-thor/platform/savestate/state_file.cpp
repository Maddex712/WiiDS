// The save state file and its compression (state_file.h).
#include "state_file.h"

#include <stdio.h>
#include <string.h>

namespace savestate {

namespace {

const char kMagic[8] = { 'S', 'M', 'S', 'S', 'T', 'A', 'T', 'E' };
const size_t kMinMatch = 4;
const size_t kMaxOffset = 0xFFFF;
const int kHashBits = 16;

uint32_t read32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
void put32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back((uint8_t)(v >> i * 8));
}
uint32_t hash4(const uint8_t* p) { return (read32(p) * 2654435761u) >> (32 - kHashBits); }

void put_length(std::vector<uint8_t>& out, size_t n)
{
	for (; n >= 255; n -= 255)
		out.push_back(255);
	out.push_back((uint8_t)n);
}

// One block as LZ4 sequences: token (literal count, match length - 4, 4 bits
// each, 15 meaning more in following bytes), literals, u16 offset, more
// length. The block ends with literals only.
void compress_block(const uint8_t* src, size_t n, std::vector<uint8_t>& out)
{
	static thread_local uint32_t table[1 << kHashBits];
	memset(table, 0, sizeof table);
	size_t anchor = 0, i = 0;
	// the last match ends 5 bytes before the end, as LZ4 requires
	const size_t limit = n > 12 ? n - 12 : 0;
	while (i < limit) {
		const uint32_t h = hash4(src + i);
		const size_t cand = table[h];
		table[h] = (uint32_t)i;
		if (cand >= i || i - cand > kMaxOffset || read32(src + cand) != read32(src + i)) {
			i++;
			continue;
		}
		size_t len = kMinMatch;
		while (i + len < n - 5 && src[cand + len] == src[i + len])
			len++;
		const size_t lit = i - anchor, extra = len - kMinMatch;
		out.push_back((uint8_t)((lit >= 15 ? 15 : lit) << 4 | (extra >= 15 ? 15 : extra)));
		if (lit >= 15)
			put_length(out, lit - 15);
		out.insert(out.end(), src + anchor, src + i);
		const size_t off = i - cand;
		out.push_back((uint8_t)off);
		out.push_back((uint8_t)(off >> 8));
		if (extra >= 15)
			put_length(out, extra - 15);
		i += len;
		anchor = i;
	}
	const size_t lit = n - anchor;
	out.push_back((uint8_t)((lit >= 15 ? 15 : lit) << 4));
	if (lit >= 15)
		put_length(out, lit - 15);
	out.insert(out.end(), src + anchor, src + n);
}

bool decompress_block(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t n)
{
	size_t s = 0, d = 0;
	for (;;) {
		if (s >= srcLen)
			return false;
		const uint8_t token = src[s++];
		size_t lit = token >> 4;
		if (lit == 15)
			for (uint8_t b = 255; b == 255;) {
				if (s >= srcLen)
					return false;
				b = src[s++];
				lit += b;
			}
		if (lit > srcLen - s || lit > n - d)
			return false;
		memcpy(dst + d, src + s, lit);
		s += lit;
		d += lit;
		if (s == srcLen)
			return d == n; // the last sequence has literals only
		if (srcLen - s < 2)
			return false;
		const size_t off = src[s] | src[s + 1] << 8;
		s += 2;
		size_t len = (token & 15) + kMinMatch;
		if ((token & 15) == 15)
			for (uint8_t b = 255; b == 255;) {
				if (s >= srcLen)
					return false;
				b = src[s++];
				len += b;
			}
		if (off == 0 || off > d || len > n - d)
			return false;
		// byte by byte: a match may overlap what it writes
		for (size_t k = 0; k < len; k++, d++)
			dst[d] = dst[d - off];
	}
}

bool write_all(FILE* f, const void* p, size_t n) { return fwrite(p, 1, n, f) == n; }
bool read_all(FILE* f, void* p, size_t n) { return fread(p, 1, n, f) == n; }

void le32(uint8_t* p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> i * 8);
}
void le64(uint8_t* p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> i * 8);
}
uint64_t read64(const uint8_t* p) { return read32(p) | (uint64_t)read32(p + 4) << 32; }

} // namespace

std::vector<uint8_t> lz_compress(const uint8_t* src, size_t n)
{
	std::vector<uint8_t> out;
	out.reserve(n / 2 + 64);
	std::vector<uint8_t> block;
	for (size_t at = 0; at < n; at += kLzBlock) {
		const size_t len = n - at < kLzBlock ? n - at : kLzBlock;
		block.clear();
		compress_block(src + at, len, block);
		if (block.size() >= len) {
			// incompressible: kept raw
			put32(out, (uint32_t)len | 0x80000000u);
			out.insert(out.end(), src + at, src + at + len);
		} else {
			put32(out, (uint32_t)block.size());
			out.insert(out.end(), block.begin(), block.end());
		}
	}
	return out;
}

bool lz_decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t n)
{
	size_t s = 0;
	for (size_t at = 0; at < n; at += kLzBlock) {
		const size_t len = n - at < kLzBlock ? n - at : kLzBlock;
		if (srcLen - s < 4)
			return false;
		const uint32_t head = read32(src + s);
		s += 4;
		const size_t stored = head & 0x7FFFFFFFu;
		if (stored > srcLen - s)
			return false;
		if (head & 0x80000000u) {
			if (stored != len)
				return false;
			memcpy(dst + at, src + s, len);
		} else if (!decompress_block(src + s, stored, dst + at, len)) {
			return false;
		}
		s += stored;
	}
	return s == srcLen;
}

Chunk make_chunk(const char tag[4], const void* data, size_t n, bool compress)
{
	Chunk c;
	memcpy(c.tag, tag, 4);
	c.rawSize = n;
	const uint8_t* p = (const uint8_t*)data;
	if (compress) {
		c.codec = CODEC_LZ;
		c.stored = lz_compress(p, n);
	} else {
		c.codec = CODEC_RAW;
		c.stored.assign(p, p + n);
	}
	return c;
}

bool write_file(const std::string& path, const std::vector<Chunk>& chunks)
{
	const std::string temporary = path + ".partial";
	FILE* f = fopen(temporary.c_str(), "wb");
	if (!f)
		return false;
	uint8_t head[16];
	memcpy(head, kMagic, 8);
	le32(head + 8, kFileVersion);
	le32(head + 12, (uint32_t)chunks.size());
	bool ok = write_all(f, head, sizeof head);
	for (size_t i = 0; ok && i < chunks.size(); i++) {
		const Chunk& c = chunks[i];
		uint8_t h[24];
		memcpy(h, c.tag, 4);
		le32(h + 4, c.codec);
		le64(h + 8, c.stored.size());
		le64(h + 16, c.rawSize);
		ok = write_all(f, h, sizeof h) && write_all(f, c.stored.data(), c.stored.size());
	}
	ok = fclose(f) == 0 && ok;
	if (ok) {
#ifdef _WIN32
		remove(path.c_str()); // rename does not replace on Windows
#endif
		ok = rename(temporary.c_str(), path.c_str()) == 0;
	}
	if (!ok)
		remove(temporary.c_str());
	return ok;
}

bool read_file(const std::string& path, std::vector<Chunk>* chunks, const char* const* wanted, int wantedCount)
{
	chunks->clear();
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	uint8_t head[16];
	bool ok = read_all(f, head, sizeof head) && memcmp(head, kMagic, 8) == 0 && read32(head + 8) == kFileVersion;
	const uint32_t count = ok ? read32(head + 12) : 0;
	for (uint32_t i = 0; ok && i < count; i++) {
		uint8_t h[24];
		if (!read_all(f, h, sizeof h)) {
			ok = false;
			break;
		}
		Chunk c;
		memcpy(c.tag, h, 4);
		c.codec = read32(h + 4);
		const uint64_t stored = read64(h + 8);
		c.rawSize = read64(h + 16);
		bool keep = !wanted;
		for (int w = 0; w < wantedCount && !keep; w++)
			keep = memcmp(wanted[w], c.tag, 4) == 0;
		// a damaged size must not make us allocate the world
		if (stored > (uint64_t)1 << 32 || c.rawSize > (uint64_t)1 << 32) {
			ok = false;
			break;
		}
		if (keep) {
			c.stored.resize((size_t)stored);
			ok = read_all(f, c.stored.data(), (size_t)stored);
		} else {
			ok = fseek(f, (long)stored, SEEK_CUR) == 0;
		}
		chunks->push_back(c);
	}
	fclose(f);
	return ok;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks, const char tag[4])
{
	for (size_t i = 0; i < chunks.size(); i++)
		if (memcmp(chunks[i].tag, tag, 4) == 0)
			return &chunks[i];
	return nullptr;
}

bool chunk_data(const Chunk& c, uint8_t* dst, size_t n)
{
	if (n != c.rawSize)
		return false;
	if (c.codec == CODEC_RAW) {
		if (c.stored.size() != n)
			return false;
		memcpy(dst, c.stored.data(), n);
		return true;
	}
	return c.codec == CODEC_LZ && lz_decompress(c.stored.data(), c.stored.size(), dst, n);
}

} // namespace savestate
