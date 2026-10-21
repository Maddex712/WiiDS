// Super Mario Sunshine's own in-place formats (no magic, recognised by file
// name): map collision .col, rails .ral, pollution map .ymp, Shadow Mario
// replays .pad, movie rumble tables .bcr, and SPC script binaries .sb.
// Layouts: Map/MapCollisionEntry.cpp + MapMakeData.cpp, Enemy/graph.cpp +
// include/Enemy/Graph.hpp, Map/PollutionManager.cpp + PollutionLayer.cpp,
// Player/MarioRecord.cpp, MarioUtil/ToolData.cpp, Strategic/spcinterp.hpp.
//
// Idempotence: none of these has a magic we can flip, so each converter
// checks a header offset: a big-endian offset lies inside the file, the same
// offset once converted (read big-endian) does not.
#include <strings.h>

#include "endian_util.h"
#include "port_endian.h"

using namespace pe;

namespace {

// True if the u32 offset at `at` is a valid big-endian offset (not converted).
bool be_offset(const Buf& b, u32 at)
{
	if (!b.has(at, 4))
		return false;
	u32 v = be32(b.at(at));
	return v != 0 && v < b.size && !(ld32(b.at(at)) != 0 && ld32(b.at(at)) < b.size && v > ld32(b.at(at)));
}

// .col: {u32 vtxNum, vtxOff, grpNum, grpOff}; vertices f32[3]; groups 0x18
// {u16 bgType, s16 triNum, u16 flags, pad, u32 idx, attrA, attrB, addData}.
int col(const Buf& b)
{
	if (!be_offset(b, 4) && !(be32(b.at(0)) == 0 && be_offset(b, 0x0C)))
		return 0;
	b.swn(0, 4, 4);
	u32 nv = ld32(b.at(0)), vo = ld32(b.at(4)), ng = ld32(b.at(8)), go = ld32(b.at(0x0C));
	b.swn(vo, nv * 3, 4);
	for (u32 g = 0; g < ng && b.has(go + g * 0x18, 0x18); g++) {
		u32 e = go + g * 0x18;
		b.swn(e, 3, 2);
		b.swn(e + 8, 4, 4);
		int16_t tris = (int16_t)ld16(b.at(e + 2));
		u16 flags    = ld16(b.at(e + 4));
		if (tris <= 0)
			continue;
		b.swn(ld32(b.at(e + 8)), tris * 3u, 2);
		u32 add = ld32(b.at(e + 0x14));
		if ((flags & 1) && add)
			b.swn(add, (u32)tris, 2);
	}
	return 1;
}

// .ral: 12-byte graph entries {s32 nodeNum, u32 nameOff, u32 nodesOff} up to
// nodeNum == 0; TRailNode 0x44: s16[4], u32 flags, u16[4], u16 conn[8], f32[8].
int ral(const Buf& b)
{
	if (!be_offset(b, 8))
		return 0;
	for (u32 e = 0; b.has(e, 12); e += 12) {
		u32 n = b.sw32(e);
		if (n == 0)
			break;
		b.sw32(e + 4);
		u32 nodes = b.sw32(e + 8);
		for (u32 i = 0; i < n && b.has(nodes + i * 0x44, 0x44); i++) {
			u32 o = nodes + i * 0x44;
			b.swn(o, 4, 2);
			b.sw32(o + 8);
			b.swn(o + 0x0C, 12, 2);
			b.swn(o + 0x24, 8, 4);
		}
	}
	return 1;
}

// .ymp: {u16 layerCount, pad, u32 layerOff}; TPollutionLayerInfo 0x2C:
// u16[3], pad, f32[6], u16[2], u32, u32 heightMapOff. Height maps are I8.
int ymp(const Buf& b)
{
	if (!be_offset(b, 4))
		return 0;
	u16 n  = b.sw16(0);
	u32 lo = b.sw32(4);
	for (u32 i = 0; i < n && b.has(lo + i * 0x2C, 0x2C); i++) {
		u32 o = lo + i * 0x2C;
		b.swn(o, 3, 2);
		b.swn(o + 8, 6, 4);
		b.swn(o + 0x20, 2, 2);
		b.swn(o + 0x24, 2, 4);
	}
	return 1;
}

// .pad: s32 length at 0x10, then 10 offsets (0x14..0x3C) to five
// {u32 durations[], values[]} pairs: f32, s16, u16, u8, u8. Array lengths are
// not stored; each runs to the next offset (or the end of the file).
int pad(const Buf& b)
{
	if (!b.has(0, 0x3C) || !be_offset(b, 0x14))
		return 0;
	b.sw32(0x10);
	static const u8 width[10] = { 4, 4, 4, 2, 4, 2, 4, 1, 4, 1 };
	Section s[10];
	for (int i = 0; i < 10; i++)
		s[i] = { b.sw32(0x14 + i * 4), i, 0 };
	int n = layout_sections(s, 10, b.size);
	for (int i = 0; i < n; i++)
		b.swrange(s[i].off, s[i].end, width[s[i].kind]);
	return 1;
}

// .bcr (JMap-style table): {s32 entries, s32 fields, s32 dataOff, u32 entrySize};
// 12-byte field items {u32 hash, u32 mask, u16 offset, u8 shift, u8 type};
// row fields: type 0/2/3/6 are 4 bytes, 4 is 2 bytes, 1/5 are bytes.
int bcr(const Buf& b)
{
	if (!b.has(0, 0x10) || !be_offset(b, 8))
		return 0;
	b.swn(0, 4, 4);
	u32 ne = ld32(b.at(0)), nf = ld32(b.at(4)), data = ld32(b.at(8)), es = ld32(b.at(12));
	for (u32 f = 0; f < nf && b.has(0x10 + f * 12, 12); f++) {
		u32 it = 0x10 + f * 12;
		b.swn(it, 2, 4);
		u16 off = b.sw16(it + 8);
		u8 type = b.base[it + 11];
		u32 w   = (type == 4) ? 2 : (type == 1 || type == 5) ? 0 : 4;
		for (u32 r = 0; w && r < ne; r++)
			b.swn(data + r * es + off, 1, w);
	}
	return 1;
}

// .sb (SPCB): header {char[4], u32 text, data, dataNum, sym, symNum, s32
// storage}; data table u32[dataNum]; symbols 0x14 {u32 x5}. The byte code
// stays big-endian (decomp-patches/endian-06 makes TSpcInterp's immediates
// big-endian loads).
int spcb(const Buf& b)
{
	if (!b.has(0, 0x1C) || !magic_is(b.base, "SPCB", 4) || !be_offset(b, 4))
		return 0;
	b.swn(4, 6, 4);
	u32 data = ld32(b.at(8)), nd = ld32(b.at(0x0C)), sym = ld32(b.at(0x10)), ns = ld32(b.at(0x14));
	b.swn(data, nd, 4);
	b.swn(sym, ns * 5, 4);
	return 1;
}

bool ext_is(const char* name, const char* ext)
{
	if (!name)
		return false;
	size_t n = strlen(name), e = strlen(ext);
	return n >= e && strcasecmp(name + n - e, ext) == 0;
}

} // namespace

// Returns a PortEndianFormat for the SMS formats, or PE_FMT_UNKNOWN.
extern "C" int port_endian_game(void* data, uint32_t size, const char* name)
{
	Buf b(data, size);
	if (size < 0x10)
		return PE_FMT_UNKNOWN;
	if (magic_is(data, "SPCB", 4))
		return spcb(b) ? PE_FMT_GAME : PE_FMT_ALREADY_NATIVE;
	int r = -1;
	if (ext_is(name, ".col"))
		r = col(b);
	else if (ext_is(name, ".ral"))
		r = ral(b);
	else if (ext_is(name, ".ymp"))
		r = ymp(b);
	else if (ext_is(name, ".pad"))
		r = pad(b);
	else if (ext_is(name, ".bcr"))
		r = bcr(b);
	if (r < 0)
		return PE_FMT_UNKNOWN;
	return r ? PE_FMT_GAME : PE_FMT_ALREADY_NATIVE;
}
