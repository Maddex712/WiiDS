// JParticle resources (JEFFjpa1). Layout: JPAEmitterLoader.hpp/.cpp,
// JPABaseShape.cpp, JPAExtraShape.cpp, JPASweepShape.cpp, JPAExTexShape.cpp,
// JPAEmitter.cpp (BEM1), JPAField.cpp (FLD1), JPATexture.hpp, JPAMath.hpp.
//
// BEM1 and FLD1 bodies are read through JSUMemoryInputStream typed reads,
// which convert from big-endian (decomp-patches/0013), so they stay
// big-endian here; decomp-patches/endian-01 makes BEM1's raw vector reads
// convert too. Every block header (magic, size) is converted, since the
// loader and JPADataBlock read those in place.
#include "endian_util.h"
#include "port_endian.h"

using namespace pe;

static void bsp1(const Buf& b)
{
	b.swn(0x12, 3, 2); // table offsets
	b.swn(0x18, 2, 4); // base size y/x
	b.sw16(0x20);      // loop offset
	b.sw16(0x5C);      // colour anim max frame
	b.swn(0x80, 11, 2);
	// colour key tables: {s16 frame, GXColor} (6 bytes)
	u32 prm = (u32)(int16_t)ld16(b.at(0x14)), env = (u32)(int16_t)ld16(b.at(0x16));
	u8 nprm = b.has(0x62, 2) ? b.base[0x62] : 0, nenv = b.has(0x62, 2) ? b.base[0x63] : 0;
	for (u32 i = 0; prm && i < nprm; i++)
		b.sw16(prm + i * 6);
	for (u32 i = 0; env && env != prm && i < nenv; i++)
		b.sw16(env + i * 6);
}

static void esp1(const Buf& b)
{
	static const u8 s16s[] = { 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x20, 0x22, 0x24, 0x26,
		                       0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x42, 0x44, 0x46,
		                       0x48, 0x4C, 0x5A, 0x5C, 0x5E, 0x60, 0x62 };
	for (size_t i = 0; i < sizeof(s16s); i++)
		b.sw16(s16s[i]);
}

static void ssp1(const Buf& b)
{
	static const u8 s16s[] = { 0x14, 0x16, 0x18, 0x30, 0x32, 0x34, 0x48, 0x4A, 0x54, 0x60 };
	for (size_t i = 0; i < sizeof(s16s); i++)
		b.sw16(s16s[i]);
	b.swn(0x28, 2, 4);
	b.swn(0x4C, 2, 4);
}

static void etx1(const Buf& b) { b.swn(0x12, 6, 2); }

static void kfa1(const Buf& b) { b.swrange(0x20, b.size, 4); }

extern "C" int port_endian_jpa(void* data, uint32_t size)
{
	Buf f(data, size);
	if (!f.has(0, 0x20) || !magic_is(data, "JEFFjpa1", 8))
		return 0;
	f.swn(0, 4, 4);
	u32 n   = ld32(f.at(0x0C));
	u32 pos = 0x20;
	for (u32 i = 0; i < n && f.has(pos, 8); i++) {
		u32 tag = f.sw32(pos);
		u32 sz  = f.sw32(pos + 4);
		if (sz < 8 || !f.has(pos, sz))
			sz = f.size - pos;
		Buf b = f.sub(pos, sz);
		switch (tag) {
		case 'BSP1': bsp1(b); break;
		case 'ESP1': esp1(b); break;
		case 'SSP1': ssp1(b); break;
		case 'ETX1': etx1(b); break;
		case 'KFA1': kfa1(b); break;
		case 'TEX1':
			if (b.has(0x20, 0x20))
				port_endian_timg_header(b.at(0x20));
			break;
		case 'BEM1': // stream-read, stays big-endian
		case 'FLD1':
		default:
			break;
		}
		pos += sz;
	}
	return 1;
}
