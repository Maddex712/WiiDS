// Format recognition for resources the game loads in place, and the hook the
// archive loaders call (decomp-patches/endian-*.patch).
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "endian_util.h"
#include "port_endian.h"

using namespace pe;

bool pe_j3d_is_native(const void* d);
bool pe_j3d_is_be(const void* d);

// BAS animation-sound table (JAIAnimation.hpp): u16 count, 6 unread bytes,
// then count x 0x20-byte entries {u32 id, f32 start, f32 end, f32 pitch,
// u32 flags, bytes}. Header byte 7 (zero on disc) marks a converted file.
static int bas(const Buf& b)
{
	if (!b.has(0, 8) || b.base[7] == PE_NATIVE_MARK)
		return 0;
	u16 n = be16(b.at(0));
	if (8 + n * 0x20u > b.size)
		return 0;
	b.sw16(0);
	for (u32 i = 0; i < n; i++)
		b.swn(8 + i * 0x20, 5, 4);
	b.base[7] = PE_NATIVE_MARK;
	return 1;
}

static const char* ext_of(const char* name)
{
	if (!name)
		return "";
	const char* dot = strrchr(name, '.');
	return dot ? dot : "";
}

static bool ext_is(const char* ext, const char* want) { return strcasecmp(ext, want) == 0; }

static int resource(void* data, uint32_t size, const char* name)
{
	if (!data || size < 8)
		return PE_FMT_UNKNOWN;
	const u8* d = (const u8*)data;
	Buf b(data, size);

	if (pe_j3d_is_be(d))
		return port_endian_j3d(data, size) ? PE_FMT_J3D_MODEL : PE_FMT_UNKNOWN;
	if (pe_j3d_is_native(d))
		return PE_FMT_ALREADY_NATIVE;
	if (magic_is(d, "JEFFjpa1", 8))
		return port_endian_jpa(data, size) ? PE_FMT_JPA : PE_FMT_UNKNOWN;
	if (magic_rev(d, "JEFF", 4) && magic_rev(d + 4, "jpa1", 4))
		return PE_FMT_ALREADY_NATIVE;
	if (magic_is(d, "IBNK", 4))
		return port_endian_ibnk(data, size) ? PE_FMT_BNK : PE_FMT_UNKNOWN;
	if (magic_is(d, "WSYS", 4))
		return port_endian_wsys(data, size) ? PE_FMT_WSYS : PE_FMT_UNKNOWN;
	if (magic_rev(d, "IBNK", 4) || magic_rev(d, "WSYS", 4))
		return PE_FMT_ALREADY_NATIVE;
	// Read through big-endian-aware code (JSU typed reads / patched raw reads).
	if (magic_is(d, "SCRNblo1", 8) || magic_is(d, "MESGbmg1", 8))
		return PE_FMT_KEEP_BE;

	int g = port_endian_game(data, size, name); // SPCB, or by name
	if (g != PE_FMT_UNKNOWN)
		return g;

	// Formats without a magic: by file name.
	const char* ext = ext_of(name);
	if (ext_is(ext, ".bti")) {
		if (d[0x19] == PE_NATIVE_MARK)
			return PE_FMT_ALREADY_NATIVE;
		return port_endian_bti(data, size) ? PE_FMT_BTI : PE_FMT_UNKNOWN;
	}
	// Read through converting JSU typed reads (0013 + endian-05/08/13), or
	// byte data: nothing to do, and nothing to log.
	if (ext_is(ext, ".prm") || ext_is(ext, ".bin") || ext_is(ext, ".bmp") || ext_is(ext, ".me") ||
	    ext_is(ext, ".txt") || ext_is(ext, ".map"))
		return PE_FMT_KEEP_BE;
	if (ext_is(ext, ".bpl") || ext_is(ext, ".tlut")) {
		if (d[4] == PE_NATIVE_MARK)
			return PE_FMT_ALREADY_NATIVE;
		return port_endian_tlut(data, size) ? PE_FMT_BTI : PE_FMT_UNKNOWN;
	}
	if (ext_is(ext, ".aaf"))
		return port_endian_aaf(data, size) ? PE_FMT_AAF : PE_FMT_ALREADY_NATIVE;
	if (ext_is(ext, ".bas")) {
		if (d[7] == PE_NATIVE_MARK)
			return PE_FMT_ALREADY_NATIVE;
		return bas(b) ? PE_FMT_BAS : PE_FMT_UNKNOWN;
	}
	return PE_FMT_UNKNOWN;
}

extern "C" int port_endian_resource(void* data, uint32_t size, const char* name)
{
	static int log = -1;
	if (log < 0)
		log = getenv("SMS_ENDIAN_LOG") != NULL;
	int fmt = resource(data, size, name);
	if (log)
		fprintf(stderr, "[endian] %s: %s (%u bytes)\n", name ? name : "?", port_endian_format_name(fmt), size);
	return fmt;
}

extern "C" const char* port_endian_format_name(int fmt)
{
	switch (fmt) {
	case PE_FMT_ALREADY_NATIVE: return "already-native";
	case PE_FMT_J3D_MODEL: return "j3d";
	case PE_FMT_BTI: return "bti";
	case PE_FMT_JPA: return "jpa";
	case PE_FMT_BFN: return "bfn";
	case PE_FMT_AAF: return "aaf";
	case PE_FMT_BNK: return "ibnk";
	case PE_FMT_WSYS: return "wsys";
	case PE_FMT_BAS: return "bas";
	case PE_FMT_GAME: return "sms";
	case PE_FMT_KEEP_BE: return "kept-big-endian";
	default: return "unknown";
	}
}

#ifndef PORT_ENDIAN_STANDALONE
// Provided by platform/misc/endian.cpp (RARC, BFN, and the log of formats
// nobody converts).
extern "C" void port_res_to_native(void* data, uint32_t size);
#else
extern "C" void port_res_to_native(void*, uint32_t) { }
#endif

// Hook for archive loaders: a resource has just been materialised in memory
// (first fetch from a memory archive, or a copy decompressed/read into a
// caller's buffer). Converts it once; `name` is the archive file name.
extern "C" void port_endian_fetched(void* data, uint32_t size, const char* name)
{
	int fmt = port_endian_resource(data, size, name);
	if (fmt == PE_FMT_UNKNOWN)
		port_res_to_native(data, size);
}
