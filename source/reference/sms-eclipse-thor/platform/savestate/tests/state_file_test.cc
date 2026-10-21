// platform/savestate/state_file.cpp: compression round trips, damaged data,
// and the chunk file.
#include "state_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace savestate;

static int g_failed;
#define CHECK(x)                                                        \
	do {                                                                \
		if (!(x)) {                                                     \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);         \
			g_failed++;                                                 \
		}                                                               \
	} while (0)

static void round_trip(const std::vector<uint8_t>& in, const char* what)
{
	std::vector<uint8_t> packed = lz_compress(in.data(), in.size());
	std::vector<uint8_t> out(in.size(), 0xCD);
	const bool ok = lz_decompress(packed.data(), packed.size(), out.data(), out.size());
	CHECK(ok);
	CHECK(out == in);
	printf("%-28s %9zu -> %9zu bytes\n", what, in.size(), packed.size());
}

int main()
{
	srand(1);
	round_trip(std::vector<uint8_t>(), "empty");
	round_trip(std::vector<uint8_t>(3, 7), "tiny");
	round_trip(std::vector<uint8_t>(9 << 20, 0), "zeros, three blocks");
	std::vector<uint8_t> noise(5 << 20);
	for (size_t i = 0; i < noise.size(); i++)
		noise[i] = (uint8_t)rand();
	round_trip(noise, "noise (kept raw)");
	// memory-like: repeated structures with small differences and runs
	std::vector<uint8_t> mem(6 << 20);
	for (size_t i = 0; i < mem.size(); i++)
		mem[i] = (i / 4096) % 3 == 0 ? 0 : (uint8_t)((i * 7) ^ (i >> 9)) & ((i % 64) < 40 ? 0xFF : 0x0F);
	for (size_t i = 0; i < mem.size(); i += 997)
		mem[i] = (uint8_t)rand();
	round_trip(mem, "memory-like");
	// long overlapping matches (offset 1 and 3)
	std::vector<uint8_t> runs;
	for (int k = 0; k < 2000; k++) {
		runs.insert(runs.end(), 300 + k % 50, (uint8_t)k);
		runs.push_back(1), runs.push_back(2), runs.push_back(3);
		runs.push_back(1), runs.push_back(2), runs.push_back(3);
	}
	round_trip(runs, "runs");

	// damaged data is refused, never overrun
	std::vector<uint8_t> packed = lz_compress(mem.data(), mem.size());
	std::vector<uint8_t> out(mem.size());
	CHECK(!lz_decompress(packed.data(), packed.size() - 1, out.data(), out.size()));
	CHECK(!lz_decompress(packed.data(), packed.size(), out.data(), out.size() - 1));
	for (int trial = 0; trial < 200; trial++) {
		std::vector<uint8_t> bad = packed;
		for (int k = 0; k < 4; k++)
			bad[4 + rand() % (bad.size() - 4)] ^= (uint8_t)(1 + rand() % 255);
		lz_decompress(bad.data(), bad.size(), out.data(), out.size()); // must not crash
	}

	// the file: chunks come back, unknown ones can be skipped
	const char* tmp = getenv("TMPDIR");
	std::string path = std::string(tmp ? tmp : "/tmp") + "/state_file_test.smsstate";
	std::vector<Chunk> chunks;
	const char json[] = "{\"format\":1}";
	chunks.push_back(make_chunk("JSON", json, sizeof json - 1, false));
	chunks.push_back(make_chunk("MEM1", mem.data(), mem.size(), true));
	chunks.push_back(make_chunk("XTRA", noise.data(), 1000, false));
	CHECK(write_file(path, chunks));
	std::vector<Chunk> back;
	CHECK(read_file(path, &back));
	CHECK(back.size() == 3);
	const Chunk* m = find_chunk(back, "MEM1");
	CHECK(m && m->codec == CODEC_LZ && m->rawSize == mem.size());
	std::vector<uint8_t> mem2(mem.size());
	CHECK(m && chunk_data(*m, mem2.data(), mem2.size()) && mem2 == mem);
	const char* const only[] = { "JSON" };
	CHECK(read_file(path, &back, only, 1));
	CHECK(back.size() == 3 && back[1].stored.empty() && find_chunk(back, "JSON")->stored.size() == sizeof json - 1);
	// not a state
	FILE* f = fopen(path.c_str(), "r+b");
	fputc('X', f);
	fclose(f);
	CHECK(!read_file(path, &back));
	remove(path.c_str());

	printf(g_failed ? "%d FAILED\n" : "all passed\n", g_failed);
	return g_failed != 0;
}
