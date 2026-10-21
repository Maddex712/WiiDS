// Host tests for platform/endian: load real files from the user's extracted
// disc (read-only), convert them and sanity-check the converted structures.
//
//   make -C platform/endian/tests run   DISC=/path/to/GMSE01/files
//
// Files are .cc so the port's platform/*.cpp glob does not pick them up.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../endian_util.h"
#include "../port_endian.h"

using namespace pe;
typedef std::vector<u8> Bytes;

static int g_fail, g_checks;
static std::string g_ctx;

static void fail(const char* fmt, ...)
{
	if (++g_fail > 60)
		return;
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "FAIL [%s] ", g_ctx.c_str());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}
#define CHECK(c, ...)          \
	do {                       \
		g_checks++;            \
		if (!(c))              \
			fail(__VA_ARGS__); \
	} while (0)

// --- Disc access: Yaz0 and RARC, read big-endian directly -----------------------

static bool read_file(const std::string& path, Bytes& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n);
	bool ok = fread(out.data(), 1, n, f) == (size_t)n;
	fclose(f);
	return ok;
}

static Bytes yaz0(const Bytes& in)
{
	if (in.size() < 16 || memcmp(in.data(), "Yaz0", 4) != 0)
		return in;
	u32 n = be32(&in[4]);
	Bytes out;
	out.reserve(n);
	size_t s = 16;
	while (out.size() < n && s < in.size()) {
		u8 code = in[s++];
		for (int bit = 0; bit < 8 && out.size() < n && s < in.size(); bit++) {
			if (code & (0x80 >> bit)) {
				out.push_back(in[s++]);
			} else {
				u8 b1 = in[s], b2 = in[s + 1];
				s += 2;
				u32 dist = (((b1 & 0xF) << 8) | b2) + 1, cnt;
				if ((b1 >> 4) == 0)
					cnt = in[s++] + 0x12;
				else
					cnt = (b1 >> 4) + 2;
				for (u32 k = 0; k < cnt; k++)
					out.push_back(out[out.size() - dist]);
			}
		}
	}
	return out;
}

struct Entry {
	std::string name;
	Bytes data;
};

static std::vector<Entry> rarc(const Bytes& d)
{
	std::vector<Entry> out;
	if (d.size() < 0x40 || memcmp(d.data(), "RARC", 4) != 0)
		return out;
	u32 info = be32(&d[8]), data = 0x20 + be32(&d[12]);
	u32 nfiles = be32(&d[info + 8]), foff = be32(&d[info + 12]), soff = be32(&d[info + 20]);
	for (u32 i = 0; i < nfiles; i++) {
		const u8* e = &d[info + foff + i * 20];
		u32 flags = be32(e + 4), doff = be32(e + 8), size = be32(e + 12);
		const char* name = (const char*)&d[info + soff + (flags & 0xFFFFFF)];
		if ((flags >> 24) & 2)
			continue; // directory
		if (data + doff + size > d.size())
			continue;
		Entry en;
		en.name = name;
		en.data.assign(d.begin() + data + doff, d.begin() + data + doff + size);
		en.data = yaz0(en.data);
		out.push_back(en);
	}
	return out;
}

static std::string g_disc;

static std::vector<Entry> archive(const char* rel)
{
	Bytes f;
	if (!read_file(g_disc + "/" + rel, f)) {
		fprintf(stderr, "missing %s\n", rel);
		g_fail++;
		return std::vector<Entry>();
	}
	return rarc(yaz0(f));
}

static bool ends_with(const std::string& s, const char* e)
{
	size_t n = strlen(e);
	return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
}

static float f32at(const u8* p)
{
	float f;
	memcpy(&f, p, 4);
	return f;
}
static bool sane(float f, float lim = 1e7f) { return isfinite(f) && fabsf(f) < lim; }

// --- Checks on converted (host-order) data ----------------------------------------

static void check_ntab(const Bytes& d, u32 off, u32 expect)
{
	if (!off)
		return;
	u16 n = ld16(&d[off]);
	if (expect != 0xFFFFFFFF)
		CHECK(n == expect, "ntab count %u != %u", n, expect);
	for (u32 i = 0; i < n; i++) {
		u16 so = ld16(&d[off + 4 + i * 4 + 2]);
		CHECK(off + so < d.size(), "ntab name offset %u out of range", so);
		const char* s = (const char*)&d[off + so];
		u16 h = 0;
		for (const char* c = s; *c; c++)
			h = (u16)(h * 3 + (signed char)*c);
		CHECK(h == ld16(&d[off + 4 + i * 4]), "ntab hash mismatch for '%s'", s);
	}
}

static void check_timg(const Bytes& d, u32 at, u32 limit)
{
	const u8* t = &d[at];
	u16 w = ld16(t + 2), h = ld16(t + 4);
	CHECK(t[0] <= 0x0E && w && h && w <= 1024 && h <= 1024, "timg fmt %u %ux%u", t[0], w, h);
	u32 img = ld32(t + 0x1C);
	CHECK(at + (img ? img : 0x20) < limit, "timg image offset %#x", img);
	if (t[8]) {
		CHECK(ld16(t + 0x0A) > 0 && ld16(t + 0x0A) <= 0x4000, "timg palette colours %u", ld16(t + 0x0A));
		CHECK(at + ld32(t + 0x0C) < limit, "timg palette offset");
	}
}

struct J3DStats {
	int models, anims, blocks, joints, shapes, materials, textures, anmtables;
} g_j3d;

static void check_j3d_model_block(const Bytes& d, u32 pos, u32 tag, u32 bsz)
{
	const u8* b = &d[pos];
	auto off = [&](u32 o) { return ld32(b + o); };
	switch (tag) {
	case 'INF1': {
		u32 h = off(0x14), depth = 0;
		bool term = false;
		for (u32 p = pos + h; p + 4 <= pos + bsz; p += 4) {
			u16 t = ld16(&d[p]);
			if (t == 0) {
				term = true;
				break;
			}
			CHECK(t == 1 || t == 2 || t == 0x10 || t == 0x11 || t == 0x12, "INF1 node type %#x", t);
			depth += (t == 1) - (t == 2);
		}
		CHECK(term, "INF1 hierarchy not terminated");
		break;
	}
	case 'VTX1': {
		u32 fmt = off(0x08), pos_arr = off(0x0C);
		bool end = false;
		for (u32 e = pos + fmt; e + 16 <= pos + bsz; e += 16) {
			u32 attr = ld32(&d[e]);
			if (attr == 0xFF) {
				end = true;
				break;
			}
			CHECK(attr >= 9 && attr <= 25 && ld32(&d[e + 8]) <= 5, "VTX1 attr %u type %u", attr, ld32(&d[e + 8]));
		}
		CHECK(end, "VTX1 format list not terminated");
		if (pos_arr && ld32(&d[pos + fmt + 8]) == 4) // first attribute POS as F32
			for (int i = 0; i < 3; i++)
				CHECK(sane(f32at(&d[pos + pos_arr + i * 4])), "VTX1 position %g", f32at(&d[pos + pos_arr + i * 4]));
		break;
	}
	case 'JNT1': {
		u16 n = ld16(b + 8);
		g_j3d.joints += n;
		u32 init = off(0x0C), idx = off(0x10);
		for (u16 i = 0; i < n; i++) {
			u16 k = ld16(&d[pos + idx + i * 2]);
			CHECK(k < n, "JNT1 index %u >= %u", k, n);
			const u8* j = &d[pos + init + k * 0x40];
			CHECK(sane(f32at(j + 4), 1e4f) && sane(f32at(j + 0x18)) && sane(f32at(j + 0x24)), "JNT1 transform");
		}
		check_ntab(d, off(0x14) ? pos + off(0x14) : 0, n);
		break;
	}
	case 'SHP1': {
		u16 n = ld16(b + 8);
		g_j3d.shapes += n;
		u32 init = off(0x0C), idx = off(0x10), mtxinit = off(0x24), drawinit = off(0x28);
		for (u16 i = 0; i < n; i++) {
			u16 k = ld16(&d[pos + idx + i * 2]);
			CHECK(k < n, "SHP1 index %u", k);
			const u8* s = &d[pos + init + k * 0x28];
			CHECK(s[0] <= 3 && ld16(s + 2) > 0 && ld16(s + 2) < 256, "SHP1 mtx type %u groups %u", s[0], ld16(s + 2));
			CHECK(sane(f32at(s + 0x0C)), "SHP1 radius");
			u16 di = ld16(s + 8);
			for (u16 g = 0; g < ld16(s + 2); g++) {
				const u8* dr = &d[pos + drawinit + (di + g) * 8];
				CHECK(ld32(dr) > 0 && pos + off(0x20) + ld32(dr + 4) + ld32(dr) <= pos + bsz,
				      "SHP1 display list %#x+%#x", ld32(dr + 4), ld32(dr));
			}
			(void)mtxinit;
		}
		if (off(0x14))
			check_ntab(d, pos + off(0x14), n);
		break;
	}
	case 'MAT3':
	case 'MAT2': {
		u16 n = ld16(b + 8);
		g_j3d.materials += n;
		u32 init = off(0x0C), ids = off(0x10);
		u16 maxid = 0;
		for (u16 i = 0; i < n; i++) {
			u16 k = ld16(&d[pos + ids + i * 2]);
			CHECK(k < n, "MAT material id %u >= %u", k, n);
			if (k > maxid)
				maxid = k;
		}
		u32 stride = tag == 'MAT3' ? 0x14C : 0x138;
		for (u16 k = 0; k <= maxid; k++) {
			const u8* m = &d[pos + init + k * stride];
			// texture numbers and TEV order indices: 0xFFFF or small
			u32 texno = tag == 'MAT3' ? 0x84 : 0x70;
			for (int t = 0; t < 8; t++) {
				u16 v = ld16(m + texno + t * 2);
				CHECK(v == 0xFFFF || v < 0x400, "MAT texNo index %#x", v);
			}
			u16 blend = ld16(m + stride - 4);
			CHECK(blend == 0xFFFF || blend < 0x400, "MAT blend index %#x", blend);
		}
		check_ntab(d, off(0x14) ? pos + off(0x14) : 0, n);
		break;
	}
	case 'TEX1': {
		u16 n = ld16(b + 8);
		g_j3d.textures += n;
		for (u16 i = 0; i < n; i++)
			check_timg(d, pos + off(0x0C) + i * 0x20, pos + bsz);
		check_ntab(d, off(0x10) ? pos + off(0x10) : 0, n);
		break;
	}
	case 'EVP1': {
		u16 n = ld16(b + 8);
		if (n && off(0x14))
			CHECK(sane(f32at(&d[pos + off(0x14)]), 2.0f), "EVP1 weight %g", f32at(&d[pos + off(0x14)]));
		break;
	}
	}
}

// Keyframe tables {u16 count, u16 index, u16 tangent mode} must stay inside
// the value array they index.
struct J3DAnmKeyTableBase_ {
	u16 count, index, type;
};
static void check_key(const J3DAnmKeyTableBase_* t, u32 nvalues)
{
	u32 need = t->count <= 1 ? t->index + 1 : t->index + t->count * (t->type ? 4 : 3);
	CHECK(t->type <= 1 && need <= nvalues, "key table count %u index %u type %u > %u values", t->count,
	      t->index, t->type, nvalues);
	g_j3d.anmtables++;
}

static void check_j3d_anim_block(const Bytes& d, u32 pos, u32 tag)
{
	const u8* b = &d[pos];
	auto keys = [&](u32 tab, u32 ntab, const u32* nvals, int ncomp) {
		for (u32 i = 0; i < ntab; i++)
			for (int c = 0; c < ncomp; c++) {
				J3DAnmKeyTableBase_ k;
				memcpy(&k, &d[pos + tab + (i * ncomp + c) * 6], 6);
				check_key(&k, nvals[c]);
			}
	};
	switch (tag) {
	case 'ANK1': {
		u16 nj = ld16(b + 0x0C);
		u32 nv[3] = { ld16(b + 0x0E), ld16(b + 0x10), ld16(b + 0x12) };
		u32 per[9] = { nv[0], nv[1], nv[2], nv[0], nv[1], nv[2], nv[0], nv[1], nv[2] };
		keys(ld32(b + 0x14), nj, per, 9);
		for (u32 i = 0; i < nv[0] && i < 64; i++)
			CHECK(sane(f32at(&d[pos + ld32(b + 0x18) + i * 4]), 1e5f), "ANK1 scale value");
		for (u32 i = 0; i < nv[2] && i < 64; i++)
			CHECK(sane(f32at(&d[pos + ld32(b + 0x20) + i * 4])), "ANK1 translation value");
		break;
	}
	case 'TTK1': {
		u16 n = ld16(b + 0x0C) / 3;
		u32 nv[3] = { ld16(b + 0x0E), ld16(b + 0x10), ld16(b + 0x12) };
		u32 per[9] = { nv[0], nv[1], nv[2], nv[0], nv[1], nv[2], nv[0], nv[1], nv[2] };
		keys(ld32(b + 0x14), n, per, 9);
		check_ntab(d, pos + ld32(b + 0x1C), n);
		break;
	}
	case 'TRK1': {
		u32 nc[4] = { ld16(b + 0x10), ld16(b + 0x12), ld16(b + 0x14), ld16(b + 0x16) };
		u32 nk[4] = { ld16(b + 0x18), ld16(b + 0x1A), ld16(b + 0x1C), ld16(b + 0x1E) };
		for (u32 i = 0; i < ld16(b + 0x0C); i++)
			for (int c = 0; c < 4; c++) {
				J3DAnmKeyTableBase_ k;
				memcpy(&k, &d[pos + ld32(b + 0x20) + i * 0x1C + c * 6], 6);
				check_key(&k, nc[c]);
			}
		for (u32 i = 0; i < ld16(b + 0x0E); i++)
			for (int c = 0; c < 4; c++) {
				J3DAnmKeyTableBase_ k;
				memcpy(&k, &d[pos + ld32(b + 0x24) + i * 0x1C + c * 6], 6);
				check_key(&k, nk[c]);
			}
		check_ntab(d, pos + ld32(b + 0x30), ld16(b + 0x0C));
		check_ntab(d, pos + ld32(b + 0x34), ld16(b + 0x0E));
		break;
	}
	case 'PAK1': {
		u32 nc[4] = { ld16(b + 0x10), ld16(b + 0x12), ld16(b + 0x14), ld16(b + 0x16) };
		keys(ld32(b + 0x18), ld16(b + 0x0E), nc, 4);
		check_ntab(d, pos + ld32(b + 0x20), ld16(b + 0x0E));
		break;
	}
	case 'TPT1': {
		u16 n = ld16(b + 0x0C);
		for (u16 i = 0; i < n; i++) {
			const u8* t = &d[pos + ld32(b + 0x10) + i * 8];
			CHECK(ld16(t) > 0 && ld16(t) < 0x1000, "TPT1 frames %u", ld16(t));
		}
		check_ntab(d, pos + ld32(b + 0x1C), n);
		break;
	}
	case 'CLK1':
	case 'CLF1':
	case 'VAF1':
	case 'ANF1':
	case 'PAF1':
		break;
	}
}

static void check_j3d(const std::string& name, Bytes d)
{
	g_ctx = name;
	u32 size = d.size();
	int r = port_endian_j3d(d.data(), size);
	CHECK(r == 1, "not converted");
	if (!r)
		return;
	CHECK(port_endian_j3d(d.data(), size) == 0, "converted twice");
	CHECK(port_endian_resource(d.data(), size, name.c_str()) == PE_FMT_ALREADY_NATIVE, "not recognised as native");
	CHECK(ld32(&d[0]) == 'J3D1' || ld32(&d[0]) == 'J3D2', "magic");
	bool model = ld32(&d[0]) == 'J3D2';
	if (model)
		g_j3d.models++;
	else
		g_j3d.anims++;
	u32 fsize = ld32(&d[8]), n = ld32(&d[12]), pos = 0x20;
	CHECK(fsize <= size && fsize + 0x20 >= size - 0x20, "file size %u vs %u", fsize, size);
	for (u32 i = 0; i < n; i++) {
		// Some .bmt headers count more blocks than the file holds (the game's
		// loader then reads past the end); stop at the end of the file.
		if (pos + 8 > size)
			return;
		u32 tag = ld32(&d[pos]), bsz = ld32(&d[pos + 4]);
		CHECK(bsz >= 8 && pos + bsz <= size, "block %.4s size %#x", (char*)&d[pos], bsz);
		if (bsz < 8 || pos + bsz > size)
			return;
		g_j3d.blocks++;
		if (model)
			check_j3d_model_block(d, pos, tag, bsz);
		else
			check_j3d_anim_block(d, pos, tag);
		pos += bsz;
	}
}

// --- JPA -----------------------------------------------------------------------------

static int g_jpa, g_jpa_tex;
static void check_jpa(const std::string& name, Bytes d)
{
	g_ctx = name;
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_JPA, "not converted");
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_ALREADY_NATIVE, "not idempotent");
	g_jpa++;
	u32 n = ld32(&d[0x0C]), pos = 0x20;
	for (u32 i = 0; i < n && pos + 8 <= d.size(); i++) {
		u32 tag = ld32(&d[pos]), sz = ld32(&d[pos + 4]);
		CHECK(sz >= 8 && pos + sz <= d.size(), "block size");
		const u8* b = &d[pos];
		switch (tag) {
		case 'BSP1':
			CHECK(sane(f32at(b + 0x18), 1e4f) && sane(f32at(b + 0x1C), 1e4f), "BSP1 base size");
			if (b[0x62])
				CHECK((u16)ld16(b + 0x14) < sz, "BSP1 colour table offset");
			break;
		case 'SSP1':
			CHECK(sane(f32at(b + 0x4C), 1e4f), "SSP1 scale");
			break;
		case 'KFA1':
			for (u32 k = 0; k < b[0x10]; k++)
				CHECK(sane(f32at(b + 0x20 + k * 16)), "KFA1 key time");
			break;
		case 'TEX1':
			check_timg(d, pos + 0x20, pos + sz);
			g_jpa_tex++;
			break;
		case 'BEM1': // stays big-endian: scale read by stream
		{
			u32 be = be32(b + 0x0C);
			float s;
			memcpy(&s, &be, 4);
			CHECK(sane(s, 1e4f), "BEM1 body should stay big-endian");
			break;
		}
		case 'FLD1':
		case 'ESP1':
		case 'ETX1':
			break;
		default:
			fail("unknown JPA block %.4s", (const char*)b);
		}
		pos += sz;
	}
}

// --- BTI / BAS ------------------------------------------------------------------------

static int g_bti, g_bas;
static void check_bti(const std::string& name, Bytes d)
{
	g_ctx = name;
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_BTI, "not converted");
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_ALREADY_NATIVE, "not idempotent");
	check_timg(d, 0, d.size());
	g_bti++;
}

static void check_bas(const std::string& name, Bytes d)
{
	g_ctx = name;
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_BAS, "not converted");
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_ALREADY_NATIVE, "not idempotent");
	u16 n = ld16(&d[0]);
	for (u16 i = 0; i < n; i++) {
		const u8* e = &d[8 + i * 0x20];
		CHECK(sane(f32at(e + 4), 1e5f) && sane(f32at(e + 8), 1e5f) && sane(f32at(e + 12), 100), "BAS entry %u", i);
	}
	g_bas++;
}

// --- AAF (JAudio init data) --------------------------------------------------------------

static void check_aaf(const std::string& name, Bytes d)
{
	g_ctx = name;
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_AAF, "not converted");
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_ALREADY_NATIVE, "not idempotent");
	const u32* w = (const u32*)d.data();
	u32 i = 0, banks = 0, wsys = 0, sounds = 0, waves = 0, insts = 0;
	for (;;) {
		u32 cmd = w[i++];
		if (cmd == 0)
			break;
		CHECK(cmd <= 8, "command %u", cmd);
		if (cmd > 8)
			return;
		if (cmd == 1) {
			u32 off = w[i], sz = w[i + 1];
			i += (w[i + 2] != 0) ? 6 : 3;
			const u8* t = &d[off];
			u32 total = ld16(t + 4), sum = 0;
			for (int c = 0; c < 18; c++) {
				u16 cnt = ld16(t + 6 + c * 4), first = ld16(t + 8 + c * 4);
				if (cnt)
					CHECK(first + cnt <= (sz - 0x50) / 16, "sound table category %d", c);
				sum += cnt;
			}
			CHECK(total == (sz - 0x50) / 16, "sound table total %u vs %u", total, (sz - 0x50) / 16);
			for (u32 e = 0; e < total; e++) {
				float pitch = f32at(t + 0x50 + e * 16 + 8);
				CHECK(pitch > 0.01f && pitch < 16.0f, "sound %u pitch %g", e, pitch);
			}
			sounds += total;
		} else if (cmd == 2 || cmd == 3) {
			while (w[i]) {
				u32 off = w[i];
				i += 3;
				const u8* p = &d[off];
				if (cmd == 2) {
					banks++;
					CHECK(magic_rev(p, "IBNK", 4), "IBNK magic");
					for (int s = 0; s < 0x80; s++) {
						u32 io = ld32(p + 0x24 + s * 4);
						if (!io)
							continue;
						insts++;
						const u8* in = p + io;
						CHECK(sane(f32at(in + 8), 100) && sane(f32at(in + 12), 100), "inst %d volume/pitch", s);
						u32 nk = ld32(in + 0x28);
						CHECK(nk > 0 && nk <= 128, "inst %d key regions %u", s, nk);
						for (u32 k = 0; k < nk && k < 128; k++) {
							const u8* km = p + ld32(in + 0x2C + k * 4);
							u32 nv = ld32(km + 4);
							CHECK(nv > 0 && nv <= 128, "keymap velocity count %u", nv);
							const u8* vm = p + ld32(km + 8);
							CHECK(sane(f32at(vm + 8), 100) && sane(f32at(vm + 12), 100), "vmap vol/pitch");
						}
						for (int o = 0; o < 2; o++) {
							u32 oo = ld32(in + 0x10 + o * 4);
							if (oo)
								CHECK(sane(f32at(p + oo + 4), 1e6f) && ld32(p + oo + 8) < ld32(p + 4) + 1,
								      "osc rate/table");
						}
					}
				} else {
					wsys++;
					CHECK(magic_rev(p, "WSYS", 4), "WSYS magic");
					u32 wbct = ld32(p + 0x14), winf = ld32(p + 0x10);
					u32 groups = ld32(p + wbct + 8);
					CHECK(groups > 0 && groups < 256, "WSYS groups %u", groups);
					for (u32 g = 0; g < groups; g++) {
						u32 arc = ld32(p + winf + 8 + g * 4);
						u32 scene = ld32(p + wbct + 0x0C + g * 4);
						u32 cdf = ld32(p + scene + 0x0C);
						u32 nw = ld32(p + cdf + 4);
						for (u32 k = 0; k < nw; k++) {
							const u8* wv = p + ld32(p + arc + 0x74 + k * 4);
							float rate = f32at(wv + 4);
							CHECK(rate >= 4000 && rate <= 48001, "wave sample rate %g", rate);
							waves++;
						}
					}
				}
			}
			i++;
		} else {
			if (cmd == 7) {
				const u8* t = &d[w[i]];
				CHECK(ld32(t) > 0 && ld32(t) < 64, "fx scene count %u", ld32(t));
			}
			if (cmd == 4) {
				const u8* t = &d[w[i]];
				CHECK(ld32(t + 12) > 0 && 0x20 * (ld32(t + 12) + 1) <= w[i + 1], "seq archive entries %u", ld32(t + 12));
			}
			i += 3;
		}
	}
	printf("  aaf: %u sounds, %u banks (%u instruments), %u wave systems (%u waves)\n", sounds, banks, insts,
	       wsys, waves);
}

// --- SMS formats ----------------------------------------------------------------------

static int g_game[6];
static void check_game(const std::string& name, Bytes d)
{
	g_ctx = name;
	int fmt = port_endian_resource(d.data(), d.size(), name.c_str());
	CHECK(fmt == PE_FMT_GAME, "not converted (%d)", fmt);
	if (fmt != PE_FMT_GAME)
		return;
	CHECK(port_endian_resource(d.data(), d.size(), name.c_str()) == PE_FMT_ALREADY_NATIVE, "not idempotent");
	const u8* p = d.data();
	u32 size = d.size();
	if (ends_with(name, ".col")) {
		g_game[0]++;
		u32 nv = ld32(p), vo = ld32(p + 4), ng = ld32(p + 8), go = ld32(p + 12);
		CHECK(vo + nv * 12 <= size && go + ng * 0x18 <= size, "col header");
		for (u32 i = 0; i < nv && i < 256; i++)
			CHECK(sane(f32at(p + vo + i * 12), 1e6f), "col vertex");
		for (u32 g = 0; g < ng; g++) {
			const u8* e = p + go + g * 0x18;
			int16_t tris = (int16_t)ld16(e + 2);
			CHECK(tris >= 0 && ld32(e + 8) + tris * 6u <= size, "col group %u tris %d", g, tris);
			for (int t = 0; t < tris * 3 && t < 300; t++)
				CHECK((u16)ld16(p + ld32(e + 8) + t * 2) < nv, "col index");
		}
	} else if (ends_with(name, ".ral")) {
		g_game[1]++;
		for (u32 e = 0; ld32(p + e); e += 12) {
			u32 n = ld32(p + e), nodes = ld32(p + e + 8);
			CHECK(n < 1000 && ld32(p + e + 4) < size && nodes + n * 0x44 <= size, "ral graph");
			for (u32 i = 0; i < n && nodes + i * 0x44 + 0x44 <= size; i++) {
				const u8* nd = p + nodes + i * 0x44;
				int16_t nc = (int16_t)ld16(nd + 6);
				CHECK(nc >= 0 && nc <= 8, "ral connections %d", nc);
				for (int c = 0; c < nc; c++)
					CHECK(ld16(nd + 0x14 + c * 2) < n, "ral connection index");
			}
		}
	} else if (ends_with(name, ".ymp")) {
		g_game[2]++;
		u16 n = ld16(p);
		u32 lo = ld32(p + 4);
		CHECK(n > 0 && n < 64 && lo + n * 0x2Cu <= size, "ymp header");
		for (u16 i = 0; i < n; i++) {
			const u8* l = p + lo + i * 0x2C;
			CHECK(sane(f32at(l + 0x0C), 1e5f) && sane(f32at(l + 0x10), 1e6f) && ld16(l + 0x20) < 16 &&
			          ld32(l + 0x28) < size,
			      "ymp layer %u", i);
		}
	} else if (ends_with(name, ".pad")) {
		g_game[3]++;
		CHECK((int32_t)ld32(p + 0x10) > 0 && (int32_t)ld32(p + 0x10) < 1000000, "pad length");
		for (int i = 0; i < 10; i++)
			CHECK(ld32(p + 0x14 + i * 4) < size, "pad offset");
		CHECK(sane(f32at(p + ld32(p + 0x18)), 1e3f), "pad first magnitude");
	} else if (ends_with(name, ".sb")) {
		g_game[4]++;
		u32 sym = ld32(p + 0x10), ns = ld32(p + 0x14);
		CHECK(sym + ns * 0x14 <= size && ld32(p + 4) < size, "sb header");
		for (u32 i = 0; i < ns; i++)
			CHECK(ld32(p + sym + i * 0x14) < 8 && ld32(p + sym + i * 0x14 + 4) < size, "sb symbol");
	} else if (ends_with(name, ".bcr")) {
		g_game[5]++;
		CHECK(ld32(p + 4) < 64 && ld32(p + 8) < size, "bcr header");
	}
}

// --- Sequence oscillator tables, TLUT, THP frame layout ---------------------------------

static void check_misc()
{
	g_ctx = "seq-osc";
	// {mode, time, value} triplets, terminated by mode > 10 (here 0x000F).
	static const u8 be[] = { 0, 0, 0, 10, 0x7F, 0xFF, 0, 1, 0, 20, 0x40, 0, 0, 0x0F, 0, 0, 0, 0, 0xAA, 0xBB };
	int16_t* t = port_seq_s16_osc_table(be);
	CHECK(t[0] == 0 && t[1] == 10 && t[2] == 0x7FFF && t[3] == 1 && t[5] == 0x4000 && t[6] == 15, "osc table");
	CHECK(port_seq_s16_osc_table(be) == t, "osc table not cached by content");
	u8 be2[sizeof(be)];
	memcpy(be2, be, sizeof(be));
	CHECK(port_seq_s16_osc_table(be2) == t, "same content, other address");
	be2[3] = 11;
	CHECK(port_seq_s16_osc_table(be2) != t && port_seq_s16_osc_table(be2)[1] == 11, "different content");

	g_ctx = "tlut";
	u8 tl[0x40] = { 1, 0, 0x00, 0x10 };
	CHECK(port_endian_tlut(tl, sizeof(tl)) == 1 && ld16(tl + 2) == 16, "tlut header");
	CHECK(port_endian_tlut(tl, sizeof(tl)) == 0, "tlut twice");

	// THP: the frame header layout decomp-patches/endian-12 converts
	// (next size, previous size, one size per component).
	g_ctx = "thp";
	Bytes f;
	if (!read_file(g_disc + "/data/openingA.thp", f) && !read_file(g_disc + "/data/Entrance.thp", f)) {
		fail("no THP file");
		return;
	}
	CHECK(memcmp(f.data(), "THP", 4) == 0 && be32(&f[4]) == 0x11000, "THP header");
	u32 frames = be32(&f[0x14]), first = be32(&f[0x18]), comp = be32(&f[0x20]), data = be32(&f[0x28]);
	u32 ncomp = be32(&f[comp]);
	CHECK(ncomp >= 1 && ncomp <= 2, "THP components %u", ncomp);
	u32 pos = data, size = first, checked = 0;
	for (u32 i = 0; i < frames && i < 50 && pos + size <= f.size(); i++, checked++) {
		u32 next = be32(&f[pos]), sum = 8 + ncomp * 4;
		// component info: types at comp+4; video info 3 words, audio info 4
		// (channels, frequency, samples, tracks); audio data is per track.
		u32 info = comp + 0x14;
		for (u32 c = 0; c < ncomp; c++) {
			u32 cs = be32(&f[pos + 8 + c * 4]);
			if (f[comp + 4 + c] == 1) {
				sum += cs * be32(&f[info + 12]);
				info += 16;
			} else {
				sum += cs;
				info += 12;
			}
		}
		CHECK(sum <= size && size - sum < 64, "THP frame %u: components %u vs frame %u", i, sum, size);
		pos += size;
		size = next;
	}
	printf("  thp: %u frames x %u components, first %u frames walked\n", frames, ncomp, checked);
}

// --- Driver ---------------------------------------------------------------------------------

static void run_archive(const char* rel)
{
	std::vector<Entry> es = archive(rel);
	int before            = g_fail;
	for (size_t i = 0; i < es.size(); i++) {
		const Entry& e = es[i];
		std::string nm = std::string(rel) + ":" + e.name;
		if (e.data.size() < 8)
			continue;
		const u8* d = e.data.data();
		if (memcmp(d, "J3D", 3) == 0)
			check_j3d(nm, e.data);
		else if (memcmp(d, "JEFFjpa1", 8) == 0)
			check_jpa(nm, e.data);
		else if (ends_with(e.name, ".bti"))
			check_bti(nm, e.data);
		else if (ends_with(e.name, ".bas"))
			check_bas(nm, e.data);
		else if (ends_with(e.name, ".aaf"))
			check_aaf(nm, e.data);
		else if (ends_with(e.name, ".col") || ends_with(e.name, ".ral") || ends_with(e.name, ".ymp") ||
		         ends_with(e.name, ".pad") || ends_with(e.name, ".bcr") || memcmp(d, "SPCB", 4) == 0)
			check_game(nm, e.data);
	}
	printf("%-40s %4zu files %s\n", rel, es.size(), g_fail == before ? "ok" : "FAILED");
}

int main(int argc, char** argv)
{
	if (!getenv("DISC")) {
		fprintf(stderr, "set DISC=<the disc's extracted files/ folder>\n");
		return 2;
	}
	g_disc = getenv("DISC");
	std::vector<const char*> arcs;
	for (int i = 1; i < argc; i++)
		arcs.push_back(argv[i]);
	if (arcs.empty()) {
		static const char* def[] = { "data/nintendo.szs", "data/title.szs",  "data/option.szs",
			                         "data/common.szs",   "data/mario.szs",  "data/particle.szs",
			                         "data/scene/dolpic0.szs", "data/scene/ricco3.szs",
			                         "data/scene/bianco0.szs", "data/scene/mamma5.szs",
			                         "data/subtitle.szs" };
		arcs.assign(def, def + sizeof(def) / sizeof(def[0]));
	}
	for (size_t i = 0; i < arcs.size(); i++)
		run_archive(arcs[i]);
	check_misc();
	printf("j3d: %d models, %d anims, %d blocks, %d joints, %d shapes, %d materials, %d textures, %d key tables\n",
	       g_j3d.models, g_j3d.anims, g_j3d.blocks, g_j3d.joints, g_j3d.shapes, g_j3d.materials, g_j3d.textures,
	       g_j3d.anmtables);
	printf("jpa: %d files (%d textures); bti: %d; bas: %d\n", g_jpa, g_jpa_tex, g_bti, g_bas);
	printf("sms: %d col, %d ral, %d ymp, %d pad, %d sb, %d bcr\n", g_game[0], g_game[1], g_game[2], g_game[3],
	       g_game[4], g_game[5]);
	printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
