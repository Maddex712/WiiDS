// JAudio sequence (.bms) data stays big-endian: the parser assembles its
// operands byte by byte. Opcodes 0xD7 (cmdSimpleEnv) and 0xF2 (cmdOscFull)
// point the track's oscillator at an s16 {mode, time, value} table inside the
// sequence, which JASOscillator reads in place; decomp-patches/endian-10 asks
// for a host-order copy here instead. Copies are cached by content, so a
// table is converted once however often (and from wherever) it is used, and
// a sequence reloaded at the same address can never see a stale copy.
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "endian_util.h"

using namespace pe;

namespace {

// JASBNKParser getOscTableEndPtr: triplets up to and including the first
// whose mode is > 10 (signed compare). Capped for a table that points at
// something else (e.g. a zero offset = the sequence base).
const u32 kMaxTriplets = 64;

std::mutex s_lock;
std::map<std::string, std::vector<int16_t> >* s_tables;

} // namespace

extern "C" int16_t* port_seq_s16_osc_table(const void* be_table)
{
	const u8* p = (const u8*)be_table;
	u32 n       = 0;
	while (n < kMaxTriplets) {
		int16_t mode = (int16_t)be16(p + n * 6);
		n++;
		if (mode > 10)
			break;
	}
	std::string key((const char*)p, n * 6);
	std::lock_guard<std::mutex> g(s_lock);
	if (!s_tables)
		s_tables = new std::map<std::string, std::vector<int16_t> >();
	std::vector<int16_t>& t = (*s_tables)[key];
	if (t.empty()) {
		t.resize(n * 3);
		for (u32 i = 0; i < n * 3; i++)
			t[i] = (int16_t)be16(p + i * 2);
	}
	return t.data();
}
