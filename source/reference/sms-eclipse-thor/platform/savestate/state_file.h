// A save state's file (*.smsstate): a list of tagged chunks, each stored raw
// or compressed (an LZ4-style block format, below). Plain C++ with no game
// code, so platform/savestate/tests can check it on its own.
//
//   "SMSSTATE"  u32 version (1)  u32 chunk count
//   then per chunk: char tag[4], u32 codec (0 raw, 1 lz), u64 stored bytes,
//   u64 raw bytes, the stored bytes
//
// Little-endian throughout. Readers skip tags they do not know, so later
// versions can add chunks.
#ifndef PORT_SAVESTATE_STATE_FILE_H
#define PORT_SAVESTATE_STATE_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace savestate {

const uint32_t kFileVersion = 1;

enum Codec : uint32_t { CODEC_RAW = 0, CODEC_LZ = 1 };

struct Chunk {
	char tag[4];
	uint32_t codec;
	uint64_t rawSize;
	std::vector<uint8_t> stored;
};

// LZ: independent blocks of up to kLzBlock input bytes, each a u32 stored
// size (top bit set: kept raw) and the block. Matches reach back 64 KiB.
const size_t kLzBlock = 1u << 22;
std::vector<uint8_t> lz_compress(const uint8_t* src, size_t n);
// Fills dst (exactly n bytes); false if the data is damaged.
bool lz_decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t n);

Chunk make_chunk(const char tag[4], const void* data, size_t n, bool compress);

// Writes to `path` through a temporary file, so a crash never leaves half a
// state under the real name.
bool write_file(const std::string& path, const std::vector<Chunk>& chunks);

// Reads the chunk list; with `wanted`, keeps the data of those tags only (the
// rest are skipped on disk). False if the file is not a state.
bool read_file(const std::string& path, std::vector<Chunk>* chunks, const char* const* wanted = nullptr,
               int wantedCount = 0);
const Chunk* find_chunk(const std::vector<Chunk>& chunks, const char tag[4]);
// A chunk's data, decompressed into dst (rawSize bytes).
bool chunk_data(const Chunk& c, uint8_t* dst, size_t n);

} // namespace savestate

#endif
