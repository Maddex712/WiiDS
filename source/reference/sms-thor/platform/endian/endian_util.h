// Internal helpers for the big-endian resource swappers (platform/endian).
// Everything here works on unaligned pointers and never reads or writes
// outside [base, base + size).
#ifndef SMS_PORT_ENDIAN_UTIL_H
#define SMS_PORT_ENDIAN_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace pe {

// Written into an unused pad byte of formats without a magic once converted.
enum { PE_NATIVE_MARK = 0x6E };

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

inline u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
inline u32 bswap32(u32 v) { return __builtin_bswap32(v); }

inline u16 ld16(const void* p) { u16 v; memcpy(&v, p, 2); return v; }
inline u32 ld32(const void* p) { u32 v; memcpy(&v, p, 4); return v; }
inline void st16(void* p, u16 v) { memcpy(p, &v, 2); }
inline void st32(void* p, u32 v) { memcpy(p, &v, 4); }

// Big-endian loads (for reading a field before it has been swapped).
inline u16 be16(const void* p) { return bswap16(ld16(p)); }
inline u32 be32(const void* p) { return bswap32(ld32(p)); }

// A bounded view of a buffer being converted.
struct Buf {
	u8* base;
	u32 size;

	Buf(void* b, u32 s) : base((u8*)b), size(s) { }
	bool has(u32 off, u32 n) const { return off <= size && n <= size - off; }
	u8* at(u32 off) const { return base + off; }

	// Swap one field in place, return its (host order) value; 0 if out of range.
	u16 sw16(u32 off) const
	{
		if (!has(off, 2))
			return 0;
		u16 v = bswap16(ld16(base + off));
		st16(base + off, v);
		return v;
	}
	u32 sw32(u32 off) const
	{
		if (!has(off, 4))
			return 0;
		u32 v = bswap32(ld32(base + off));
		st32(base + off, v);
		return v;
	}
	// Swap `count` consecutive elements of `width` (1, 2 or 4) bytes.
	void swn(u32 off, u32 count, u32 width) const
	{
		if (width == 2) {
			for (u32 i = 0; i < count && has(off + i * 2, 2); i++)
				sw16(off + i * 2);
		} else if (width == 4) {
			for (u32 i = 0; i < count && has(off + i * 4, 4); i++)
				sw32(off + i * 4);
		}
	}
	// Swap every element of `width` bytes in [off, end).
	void swrange(u32 off, u32 end, u32 width) const
	{
		if (end > size)
			end = size;
		if (off >= end || width < 2)
			return;
		swn(off, (end - off) / width, width);
	}
	Buf sub(u32 off, u32 n) const
	{
		if (off > size)
			off = size;
		if (n > size - off)
			n = size - off;
		return Buf(base + off, n);
	}
};

inline bool magic_is(const void* p, const char* m, size_t n)
{
	return memcmp(p, m, n) == 0;
}

// True if the n bytes at p are the byte-reversal of m (a multi-char magic
// that has already been converted to a host-order u32/u64).
inline bool magic_rev(const void* p, const char* m, size_t n)
{
	const u8* b = (const u8*)p;
	for (size_t i = 0; i < n; i++)
		if (b[i] != (u8)m[n - 1 - i])
			return false;
	return true;
}

// Sections of a block addressed by offsets, sized by the gap to the next
// section (the J3D writers lay sections out in order, padded to 32 bytes).
// `kind` is interpreted by the caller.
struct Section {
	u32 off;
	int kind;
	u32 end;
};

// Sort sections by offset, drop zero offsets and duplicates (first entry at an
// offset wins unless a later one is flagged preferred), fill in `end` from the
// next offset or `limit`. Returns the new count.
int layout_sections(Section* s, int n, u32 limit);

} // namespace pe

#endif
