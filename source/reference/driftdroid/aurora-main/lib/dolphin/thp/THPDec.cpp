#include "dolphin/thp.h"

#include "../../internal.hpp"

// Upstream builds these inside a `using namespace aurora` context; this tree does not, so pull
// in just the two helpers the decoder calls unqualified.
using aurora::ByteReader;
using aurora::read_bits;

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
// Error codes
constexpr s32 kBadSyntax = 3;
constexpr s32 kBadPrecision = 10;
constexpr s32 kUnsupportedMarker = 11;
constexpr s32 kBadComponentCount = 12;
constexpr s32 kMissingHuffmanTable = 15;
constexpr s32 kBadSampling = 19;
constexpr s32 kNoInput = 25;
constexpr s32 kNoOutput = 27;

constexpr std::array<u8, 64> kNaturalOrder{0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                           12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                           35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                           58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr std::array<double, 8> kAanScale{1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
                                          1.0f, 0.785694958f, 0.541196100f, 0.275899379f};

// AAN butterfly constants
constexpr float kSqrt2 = 1.414213562f;
constexpr float kC2 = 1.847759065f;        // 2 cos(pi/8)
constexpr float kC2MinusC6 = 1.082392200f; // 2 cos(pi/8) - 2 cos(3pi/8)
constexpr float kC2PlusC6 = 2.613125930f;  // 2 cos(pi/8) + 2 cos(3pi/8)
constexpr float kC6 = kC2 - kC2MinusC6;

constexpr float kOutputBias = 128.0f * 8.0f;

// Whether to reproduce a bug from SDK THP that swaps columns 3 and 4 in _quarterIDCT
constexpr bool kBuggyQuarterIdct = false;

bool read_segment(aurora::ByteReader& reader, const u8*& data, size_t& size) noexcept {
  u16 encodedSize = 0;
  if (!reader.try_read(encodedSize) || encodedSize < 2) {
    return false;
  }

  std::span<const u8> bytes;
  if (!reader.try_take(encodedSize - 2, bytes)) {
    return false;
  }
  data = bytes.data();
  size = bytes.size();
  return true;
}

struct QuantizationTable {
  std::array<float, 64> values{};
  bool valid = false;
};

struct HuffmanTable {
  std::array<u8, 17> counts{};
  std::array<u16, 17> firstCodes{};
  std::array<u16, 17> symbolOffsets{};
  std::array<u8, 256> symbols{};
  // Codes of up to kHuffmanLookupBits bits, indexed by the next kHuffmanLookupBits of the stream:
  // (length << 8) | symbol, or 0 when the code is longer (the slow path then walks lengths 10-16).
  std::array<u16, 1u << 9> lookup{};
  bool valid = false;
};
constexpr u32 kHuffmanLookupBits = 9;

struct Component {
  u8 quantizationTable = 0;
  u8 dcTable = 0;
  u8 acTable = 0;
  s32 predictedDc = 0;
};

struct DecodeContext {
  std::array<QuantizationTable, 3> quantizationTables{};
  std::array<HuffmanTable, 4> huffmanTables{};
  std::array<Component, 3> components{};
  u16 width = 0;
  u16 height = 0;
  u16 restartInterval = 0;
  size_t scanOffset = 0;
};

s32 parse_quantization_tables(const u8* data, size_t size, DecodeContext& context) noexcept {
  size_t position = 0;
  while (position < size) {
    if (size - position < 65) {
      return kBadSyntax;
    }
    const u8 descriptor = data[position++];
    const u8 precision = descriptor >> 4;
    const u8 id = descriptor & 15;
    if (precision != 0 || id >= context.quantizationTables.size()) {
      return kBadSyntax;
    }

    std::array<float, 64> natural{};
    for (size_t i = 0; i < 64; ++i) {
      natural[kNaturalOrder[i]] = static_cast<float>(data[position++]);
    }
    QuantizationTable& table = context.quantizationTables[id];
    for (size_t row = 0; row < 8; ++row) {
      for (size_t column = 0; column < 8; ++column) {
        const size_t index = row * 8 + column;
        table.values[index] =
            static_cast<float>(static_cast<double>(natural[index]) * kAanScale[row] * kAanScale[column]);
      }
    }
    table.valid = true;
  }
  return 0;
}

s32 parse_frame_header(const u8* data, size_t size, DecodeContext& context) noexcept {
  if (size < 6) {
    return kBadSyntax;
  }
  if (data[0] != 8) {
    return kBadPrecision;
  }
  context.height = static_cast<u16>((static_cast<u16>(data[1]) << 8) | data[2]);
  context.width = static_cast<u16>((static_cast<u16>(data[3]) << 8) | data[4]);
  if (data[5] != 3) {
    return kBadComponentCount;
  }
  if (size < 15 || context.width == 0 || context.height == 0) {
    return kBadSyntax;
  }

  for (size_t i = 0; i < 3; ++i) {
    const u8 sampling = data[7 + i * 3];
    if ((i == 0 && sampling != 0x22) || (i != 0 && sampling != 0x11)) {
      return kBadSampling;
    }
    const u8 table = data[8 + i * 3];
    if (table >= context.quantizationTables.size()) {
      return kBadSyntax;
    }
    context.components[i].quantizationTable = table;
  }
  return 0;
}

s32 parse_huffman_tables(const u8* data, size_t size, DecodeContext& context) noexcept {
  size_t position = 0;
  while (position < size) {
    if (size - position < 17) {
      return kBadSyntax;
    }
    const u8 descriptor = data[position++];
    const u8 tableClass = descriptor >> 4;
    const u8 id = descriptor & 15;
    if (tableClass > 1 || id > 1) {
      return kBadSyntax;
    }

    HuffmanTable table{};
    size_t symbolCount = 0;
    for (size_t length = 1; length <= 16; ++length) {
      table.counts[length] = data[position++];
      symbolCount += table.counts[length];
    }
    if (symbolCount > table.symbols.size() || symbolCount > size - position) {
      return kBadSyntax;
    }
    std::copy_n(data + position, symbolCount, table.symbols.begin());
    position += symbolCount;

    u32 code = 0;
    u16 symbolOffset = 0;
    for (size_t length = 1; length <= 16; ++length) {
      const u32 count = table.counts[length];
      if (code + count > (u32{1} << length)) {
        return kBadSyntax;
      }
      table.firstCodes[length] = static_cast<u16>(code);
      table.symbolOffsets[length] = symbolOffset;
      code = (code + count) << 1;
      symbolOffset = static_cast<u16>(symbolOffset + count);
    }
    // Longest first, so where ranges could overlap the shorter code wins, exactly as the
    // bit-at-a-time search (which tries length 1 upward) would decide.
    for (u32 length = kHuffmanLookupBits; length >= 1; --length) {
      const u32 shift = kHuffmanLookupBits - length;
      for (u32 index = 0; index < table.counts[length]; ++index) {
        const u32 prefix = table.firstCodes[length] + index;
        const u16 entry = static_cast<u16>((length << 8) | table.symbols[table.symbolOffsets[length] + index]);
        for (u32 suffix = 0; suffix < (u32{1} << shift); ++suffix) {
          table.lookup[(prefix << shift) | suffix] = entry;
        }
      }
    }
    table.valid = true;
    context.huffmanTables[id * 2 + tableClass] = table;
  }
  return 0;
}

s32 parse_scan_header(const u8* data, size_t size, DecodeContext& context) noexcept {
  if (size < 1) {
    return kBadSyntax;
  }
  if (data[0] != 3) {
    return kBadComponentCount;
  }
  if (size < 10) {
    return kBadSyntax;
  }

  for (size_t i = 0; i < 3; ++i) {
    const u8 selectors = data[2 + i * 2];
    const u8 dcTable = selectors >> 4;
    const u8 acTable = selectors & 15;
    if (dcTable > 1 || acTable > 1 || !context.huffmanTables[dcTable * 2].valid ||
        !context.huffmanTables[acTable * 2 + 1].valid) {
      return kMissingHuffmanTable;
    }
    context.components[i].dcTable = dcTable;
    context.components[i].acTable = acTable;
    context.components[i].predictedDc = 0;
  }
  if (data[7] != 0 || data[8] != 63 || data[9] != 0) {
    return kBadSyntax;
  }
  return 0;
}

s32 parse_headers(const void* file, DecodeContext& context) noexcept {
  auto reader = aurora::ByteReader::unbounded(file);
  while (true) {
    u8 prefix = 0;
    if (!reader.try_read(prefix) || prefix != 0xFF) {
      return kBadSyntax;
    }

    u8 marker = 0;
    do {
      if (!reader.try_read(marker)) {
        return kBadSyntax;
      }
    } while (marker == 0xFF);

    if (marker == 0xD8) {
      continue;
    }
    if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
      const u8* ignored = nullptr;
      size_t ignoredSize = 0;
      if (!read_segment(reader, ignored, ignoredSize)) {
        return kBadSyntax;
      }
      continue;
    }

    const u8* segment = nullptr;
    size_t segmentSize = 0;
    switch (marker) {
    case 0xC0: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_frame_header(segment, segmentSize, context); result != 0) {
        return result;
      }
      break;
    }
    case 0xC4: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_huffman_tables(segment, segmentSize, context); result != 0) {
        return result;
      }
      break;
    }
    case 0xDA: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      if (const s32 result = parse_scan_header(segment, segmentSize, context); result != 0) {
        return result;
      }
      context.scanOffset = reader.offset();
      return 0;
    }
    case 0xDB: {
      if (!read_segment(reader, segment, segmentSize)) {
        return kBadSyntax;
      }
      const s32 result = parse_quantization_tables(segment, segmentSize, context);
      if (result != 0) {
        return result;
      }
      break;
    }
    case 0xDD: {
      if (!read_segment(reader, segment, segmentSize) || segmentSize != 2) {
        return kBadSyntax;
      }
      context.restartInterval = read_bits<u16>(segment);
      break;
    }
    default:
      return kUnsupportedMarker;
    }
  }
}

// MSB-first bit reader with a 64-bit buffer held in registers. After refill() at least 56 bits are
// buffered, enough for one Huffman code and its value bits (at most 16 + 16) without another load.
// Refilling loads 8 bytes at the read pointer, so it may look up to 8 bytes past the last bit
// consumed - always inside the frame buffer the guest allocated, which is padded well beyond the
// compressed data.
class BitReader {
public:
  BitReader(const u8* data, size_t byteOffset) noexcept : mNext{data + byteOffset} {}

  void refill() noexcept {
    u64 raw;
    std::memcpy(&raw, mNext, sizeof(raw));
    mBuffer |= __builtin_bswap64(raw) >> mCount;
    mNext += (63 - mCount) >> 3;
    mCount |= 56;
  }

  // The next 16 bits. Needs a refill() since at most 40 bits were consumed.
  u32 peek16() const noexcept { return static_cast<u32>(mBuffer >> 48); }

  // `count` (1-16) bits starting `offset` bits ahead, without consuming anything.
  u32 peek_at(u32 offset, u32 count) const noexcept { return static_cast<u32>((mBuffer << offset) >> (64 - count)); }

  void skip(u32 count) noexcept {
    mBuffer <<= count;
    mCount -= count;
  }

  void byte_align() noexcept { skip(mCount & 7); }

private:
  const u8* mNext;
  u64 mBuffer = 0;
  u32 mCount = 0;
};

// Decodes the Huffman code at the top of `bits` (the next 16 stream bits). Returns the code's
// length, or 0 when no code matches. Same answer as trying lengths 1..16 one bit at a time:
// short codes resolve in one table lookup, longer ones by walking lengths 10-16.
inline u32 decode_huffman(u32 bits, const HuffmanTable& table, u8& symbol) noexcept {
  if (const u16 entry = table.lookup[bits >> (16 - kHuffmanLookupBits)]; entry != 0) [[likely]] {
    symbol = static_cast<u8>(entry);
    return entry >> 8;
  }
  for (u32 length = kHuffmanLookupBits + 1; length <= 16; ++length) {
    const u32 code = bits >> (16 - length);
    const u32 firstCode = table.firstCodes[length];
    if (code >= firstCode && code - firstCode < table.counts[length]) {
      symbol = table.symbols[table.symbolOffsets[length] + code - firstCode];
      return length;
    }
  }
  return 0;
}

// JPEG's EXTEND for a value of `bitCount` (1-16) bits: values below 2^(bitCount-1) are negative.
// Branch-free, because the sign of a coefficient is as good as random to a branch predictor.
inline s32 extend_value(u32 value, u32 bitCount) noexcept {
  const s32 isNegative = static_cast<s32>(value - (u32{1} << (bitCount - 1))) >> 31;  // all ones or zero
  return static_cast<s32>(value) + (isNegative & (1 - static_cast<s32>(u32{1} << bitCount)));
}

// Entropy-decodes one block into `block` (natural order) and sets a bit in `rowMask` for every
// row that received a coefficient.
bool decode_block(BitReader& sharedReader, DecodeContext& context, size_t componentIndex, s16* block,
                  u32& rowMask) noexcept {
  std::memset(block, 0, 64 * sizeof(s16));
  Component& component = context.components[componentIndex];
  const HuffmanTable& dcTable = context.huffmanTables[component.dcTable * 2];
  const HuffmanTable& acTable = context.huffmanTables[component.acTable * 2 + 1];

  BitReader reader = sharedReader;  // a local copy the compiler can keep in registers
  reader.refill();
  u8 symbol = 0;
  u32 length = decode_huffman(reader.peek16(), dcTable, symbol);
  u32 bitCount = symbol;
  if (length == 0 || bitCount > 16) {
    return false;
  }
  const u32 encodedDifference = bitCount == 0 ? 0 : reader.peek_at(length, bitCount);
  reader.skip(length + bitCount);
  component.predictedDc += bitCount == 0 ? 0 : extend_value(encodedDifference, bitCount);
  block[0] = static_cast<s16>(component.predictedDc);
  u32 rows = 1;

  size_t coefficient = 1;
  while (coefficient < 64) {
    reader.refill();
    length = decode_huffman(reader.peek16(), acTable, symbol);
    if (length == 0) {
      return false;
    }
    const u32 run = symbol >> 4;
    bitCount = symbol & 15;
    if (bitCount == 0) {
      reader.skip(length);
      if (run == 15) {
        coefficient += 16;
        continue;
      }
      break;
    }

    coefficient += run;
    if (coefficient >= 64) {
      return false;
    }
    const u32 encodedValue = reader.peek_at(length, bitCount);
    reader.skip(length + bitCount);
    const u32 position = kNaturalOrder[coefficient];
    block[position] = static_cast<s16>(extend_value(encodedValue, bitCount));
    rows |= u32{1} << (position >> 3);
    ++coefficient;
  }
  sharedReader = reader;
  rowMask = rows;
  return true;
}

using FloatRow = std::array<float, 8>;

struct EvenHalf {
  float out0, out1, out2, out3;
};

struct OddHalf {
  float out7, out6, out5, out4;
};

EvenHalf aan_even(float sum04, float dif04, float sum26, float dif26) noexcept {
  const float rotated = std::fma(dif26, kSqrt2, -sum26);
  return {sum04 + sum26, dif04 + rotated, dif04 - rotated, sum04 - sum26};
}

OddHalf aan_odd(float z10, float z11, float z12, float z13) noexcept {
  const float out7 = z11 + z13;
  const float z5 = (z10 + z12) * kC2;
  const float out6 = std::fma(-z10, kC2PlusC6, z5) - out7;
  const float out5 = std::fma(z11 - z13, kSqrt2, -out6);
  const float out4 = std::fma(-z12, kC2MinusC6, z5) - out5;
  return {out7, out6, out5, out4};
}

FloatRow combine(const EvenHalf& even, const OddHalf& odd) noexcept {
  return {
      even.out0 + odd.out7, even.out1 + odd.out6, even.out2 + odd.out5, even.out3 + odd.out4,
      even.out3 - odd.out4, even.out2 - odd.out5, even.out1 - odd.out6, even.out0 - odd.out7,
  };
}

FloatRow transform_row(const s16* coefficients, const float* quant) noexcept {
  const float x0 = static_cast<float>(coefficients[0]) * quant[0];
  const float x1 = static_cast<float>(coefficients[1]) * quant[1];
  const float x2 = static_cast<float>(coefficients[2]) * quant[2];
  const float x3 = static_cast<float>(coefficients[3]) * quant[3];
  const auto c4 = static_cast<float>(coefficients[4]);
  const auto c5 = static_cast<float>(coefficients[5]);
  const auto c6 = static_cast<float>(coefficients[6]);
  const auto c7 = static_cast<float>(coefficients[7]);
  const EvenHalf even = aan_even(std::fma(c4, quant[4], x0), std::fma(-c4, quant[4], x0), std::fma(c6, quant[6], x2),
                                 std::fma(-c6, quant[6], x2));
  const OddHalf odd = aan_odd(std::fma(c5, quant[5], -x3), std::fma(c7, quant[7], x1), std::fma(-c7, quant[7], x1),
                              std::fma(c5, quant[5], x3));
  return combine(even, odd);
}

FloatRow transform_row_low4(const FloatRow& x) noexcept {
  const float sum02 = x[0] + x[2];
  const float dif02 = x[0] - x[2];
  const EvenHalf even{sum02, std::fma(x[2], kSqrt2, dif02), std::fma(-x[2], kSqrt2, sum02), dif02};
  const OddHalf odd = aan_odd(-x[3], x[1], x[1], x[3]);
  return combine(even, odd);
}

FloatRow transform_row_dc_ac(float dc, float ac) noexcept {
  const float a1 = std::fma(ac, kC2, -ac);
  const float a2 = std::fma(ac, kSqrt2, -a1);
  const float a3 = std::fma(-ac, kC6, a2);
  if constexpr (kBuggyQuarterIdct) {
    return {dc + ac, dc + a1, dc + a2, dc + a3, dc - a3, dc - a2, dc - a1, dc - ac};
  }
  return {dc + ac, dc + a1, dc + a2, dc - a3, dc + a3, dc - a2, dc - a1, dc - ac};
}

FloatRow transform_column(const FloatRow& x) noexcept {
  const EvenHalf even = aan_even((x[0] + x[4]) + kOutputBias, (x[0] - x[4]) + kOutputBias, x[2] + x[6], x[2] - x[6]);
  const OddHalf odd = aan_odd(x[5] - x[3], x[1] + x[7], x[1] - x[7], x[5] + x[3]);
  return combine(even, odd);
}

u8 to_pixel(float value) noexcept {
  const float scaled = value * 0.125f;
  return scaled <= 0.0f ? 0 : scaled >= 255.0f ? 255 : static_cast<u8>(scaled);
}

// Four columns at a time. Lane-wise +, - and * are the same IEEE operations as the scalar code,
// and fma4 is one fused multiply-add per lane, so every pixel is computed by exactly the
// operation sequence of transform_column above - just four (NEON) lanes per instruction.
using Float4 = float __attribute__((vector_size(16)));
using Int4 = s32 __attribute__((vector_size(16)));

inline Float4 splat4(float value) noexcept { return Float4{value, value, value, value}; }

inline Float4 fma4(Float4 a, Float4 b, Float4 c) noexcept {
  Float4 result;
  for (int lane = 0; lane < 4; ++lane) {
    result[lane] = __builtin_fmaf(a[lane], b[lane], c[lane]);
  }
  return result;
}

inline void store_pixels4(u8* out, Float4 value) noexcept {
  const Float4 scaled = value * splat4(0.125f);
  // Same result as to_pixel for every finite input: clamp to [0, 255], then truncate.
  const Float4 zero = splat4(0.0f);
  const Float4 top = splat4(255.0f);
  Float4 clamped = scaled > zero ? scaled : zero;
  clamped = clamped < top ? clamped : top;
  const Int4 integer = __builtin_convertvector(clamped, Int4);
  for (int lane = 0; lane < 4; ++lane) {
    out[lane] = static_cast<u8>(integer[lane]);
  }
}

inline void transform_columns4(const float* workspace, u8* output) noexcept {
  Float4 x[8];
  for (int row = 0; row < 8; ++row) {
    std::memcpy(&x[row], workspace + row * 8, sizeof(Float4));
  }
  const Float4 bias = splat4(kOutputBias);
  const Float4 sqrt2 = splat4(kSqrt2);
  // aan_even
  const Float4 sum04 = (x[0] + x[4]) + bias;
  const Float4 dif04 = (x[0] - x[4]) + bias;
  const Float4 sum26 = x[2] + x[6];
  const Float4 dif26 = x[2] - x[6];
  const Float4 rotated = fma4(dif26, sqrt2, -sum26);
  const Float4 even0 = sum04 + sum26;
  const Float4 even1 = dif04 + rotated;
  const Float4 even2 = dif04 - rotated;
  const Float4 even3 = sum04 - sum26;
  // aan_odd
  const Float4 z10 = x[5] - x[3];
  const Float4 z11 = x[1] + x[7];
  const Float4 z12 = x[1] - x[7];
  const Float4 z13 = x[5] + x[3];
  const Float4 out7 = z11 + z13;
  const Float4 z5 = (z10 + z12) * splat4(kC2);
  const Float4 out6 = fma4(-z10, splat4(kC2PlusC6), z5) - out7;
  const Float4 out5 = fma4(z11 - z13, sqrt2, -out6);
  const Float4 out4 = fma4(-z12, splat4(kC2MinusC6), z5) - out5;
  // combine
  store_pixels4(output + 0 * 8, even0 + out7);
  store_pixels4(output + 1 * 8, even1 + out6);
  store_pixels4(output + 2 * 8, even2 + out5);
  store_pixels4(output + 3 * 8, even3 + out4);
  store_pixels4(output + 4 * 8, even3 - out4);
  store_pixels4(output + 5 * 8, even2 - out5);
  store_pixels4(output + 6 * 8, even1 - out6);
  store_pixels4(output + 7 * 8, even0 - out7);
}

size_t row_extent(const s16* coefficients) noexcept {
  size_t extent = 8;
  while (extent > 0 && coefficients[extent - 1] == 0) {
    --extent;
  }
  return extent;
}

// `rowMask` has a bit for every row that may hold a non-zero coefficient (row 0 always).
void inverse_dct(const s16* coefficients, u32 rowMask, const QuantizationTable& quantization, u8* output) noexcept {
  if (rowMask == 1 && coefficients[1] == 0 && row_extent(coefficients) <= 1) {
    // Only the DC term: every column sees the same input, so one column gives all eight.
    FloatRow column{};
    column[0] = static_cast<float>(coefficients[0]) * quantization.values[0];
    const FloatRow transformed = transform_column(column);
    for (size_t row = 0; row < 8; ++row) {
      std::memset(output + row * 8, to_pixel(transformed[row]), 8);
    }
    return;
  }

  alignas(16) float workspace[64];
  for (size_t row = 0; row < 8; ++row) {
    const s16* rowCoefficients = &coefficients[row * 8];
    const float* rowQuant = &quantization.values[row * 8];
    FloatRow transformed{};
    // A row with no coefficients transforms to all zeros (0 * quant).
    switch ((rowMask >> row) & 1 ? row_extent(rowCoefficients) : 0) {
    case 0:
      break;
    case 1:
      transformed.fill(static_cast<float>(rowCoefficients[0]) * rowQuant[0]);
      break;
    case 2:
      transformed = transform_row_dc_ac(static_cast<float>(rowCoefficients[0]) * rowQuant[0],
                                        static_cast<float>(rowCoefficients[1]) * rowQuant[1]);
      break;
    case 3:
    case 4: {
      FloatRow dequantized{};
      for (size_t column = 0; column < 4; ++column) {
        dequantized[column] = static_cast<float>(rowCoefficients[column]) * rowQuant[column];
      }
      transformed = transform_row_low4(dequantized);
      break;
    }
    default:
      transformed = transform_row(rowCoefficients, rowQuant);
      break;
    }
    std::memcpy(workspace + row * 8, transformed.data(), sizeof(transformed));
  }

  transform_columns4(workspace, output);
  transform_columns4(workspace + 4, output + 4);
}

void write_block(u8* output, u16 width, u16 height, u16 blockX, u16 blockY, const std::array<u8, 64>& pixels) noexcept {
  const size_t tilesPerRow = (width + 7) / 8;
  if (blockX + 8 <= width && blockY + 8 <= height) {
    // Whole block (blockX is always a multiple of 8): each 8-pixel row is one contiguous row of an
    // 8x4 I8 tile, and the block spans exactly two tiles vertically.
    const size_t tileColumn = blockX / 8;
    for (u32 row = 0; row < 8; ++row) {
      const u32 y = blockY + row;
      std::memcpy(output + ((y / 4) * tilesPerRow + tileColumn) * 32 + (y & 3) * 8, &pixels[row * 8], 8);
    }
    return;
  }
  for (u16 row = 0; row < 8 && blockY + row < height; ++row) {
    for (u16 column = 0; column < 8 && blockX + column < width; ++column) {
      const u16 x = blockX + column;
      const u16 y = blockY + row;
      const size_t tile = static_cast<size_t>(y / 4) * tilesPerRow + x / 8;
      const size_t offset = tile * 32 + static_cast<size_t>(y & 3) * 8 + (x & 7);
      output[offset] = pixels[row * 8 + column];
    }
  }
}

// One entropy-decoded block waiting for its inverse DCT.
struct BlockJob {
  s16 coefficients[64];
  u32 rowMask;
};

// Everything the inverse-DCT stage needs for one frame. Blocks are stored MCU row by MCU row, six
// per MCU (four luma, U, V) in stream order; blockCounts[row] is how many of that row are valid
// (all of them unless the stream broke off inside the row).
struct FrameJob {
  const DecodeContext* context = nullptr;
  const BlockJob* blocks = nullptr;
  const u32* blockCounts = nullptr;
  u8* tileY = nullptr;
  u8* tileU = nullptr;
  u8* tileV = nullptr;
  u16 chromaWidth = 0;
  u16 chromaHeight = 0;
  u16 mcuColumns = 0;
};

void transform_mcu_row(const FrameJob& job, u32 mcuY) noexcept {
  const DecodeContext& context = *job.context;
  const u32 blocksPerRow = u32{job.mcuColumns} * 6;
  const BlockJob* blocks = job.blocks + static_cast<size_t>(mcuY) * blocksPerRow;
  const u32 count = job.blockCounts[mcuY];
  const QuantizationTable* quantization[3];
  for (size_t component = 0; component < 3; ++component) {
    quantization[component] = &context.quantizationTables[context.components[component].quantizationTable];
  }
  std::array<u8, 64> pixels;
  for (u32 index = 0; index < count; ++index) {
    const u32 mcuX = index / 6;
    const u32 part = index % 6;
    const BlockJob& block = blocks[index];
    if (part < 4) {
      inverse_dct(block.coefficients, block.rowMask, *quantization[0], pixels.data());
      write_block(job.tileY, context.width, context.height, static_cast<u16>(mcuX * 16 + (part & 1) * 8),
                  static_cast<u16>(mcuY * 16 + (part >> 1) * 8), pixels);
    } else {
      inverse_dct(block.coefficients, block.rowMask, *quantization[part - 3], pixels.data());
      write_block(part == 4 ? job.tileU : job.tileV, job.chromaWidth, job.chromaHeight, static_cast<u16>(mcuX * 8),
                  static_cast<u16>(mcuY * 8), pixels);
    }
  }
}

// Runs the inverse DCT of finished MCU rows on helper threads while the calling thread is still
// entropy-decoding the rest of the frame (that part is serial by nature: each code's position
// depends on every code before it). Rows touch disjoint pixels and each is computed exactly as in
// a single-threaded decode, so the picture is identical whichever thread does it.
class TransformPool {
public:
  static TransformPool& instance() noexcept {
    static TransformPool* pool = new TransformPool();  // never destroyed: the helpers outlive main()
    return *pool;
  }

  std::vector<BlockJob>& block_storage() noexcept { return mBlocks; }
  std::vector<u32>& count_storage() noexcept { return mCounts; }

  void begin(const FrameJob* job) noexcept {
    std::lock_guard lock{mMutex};
    mJob = job;
    mPublished = mClaimed = mDone = 0;
  }

  // Rows [0, rows) are ready to transform.
  void publish(u32 rows) noexcept {
    if (mThreads.empty()) {
      mPublished = rows;
      return;
    }
    {
      std::lock_guard lock{mMutex};
      mPublished = rows;
    }
    mWork.notify_one();
  }

  // Transforms whatever is left on the calling thread too, then waits for the helpers.
  void finish() noexcept {
    std::unique_lock lock{mMutex};
    while (mClaimed < mPublished) {
      const u32 row = mClaimed++;
      lock.unlock();
      transform_mcu_row(*mJob, row);
      lock.lock();
      ++mDone;
    }
    mIdle.wait(lock, [this] { return mDone == mPublished; });
    mJob = nullptr;
  }

private:
  TransformPool() noexcept {
#if defined(__SWITCH__)
    u32 helpers = 2;  // three application cores; std::thread::hardware_concurrency is not reliable here
#else
    const u32 cores = std::thread::hardware_concurrency();
    u32 helpers = cores >= 4 ? 2 : cores >= 2 ? 1 : 0;
#endif
    for (u32 index = 0; index < helpers; ++index) {
#if defined(__cpp_exceptions)
      try {
        mThreads.emplace_back([this] { run(); });
      } catch (...) {
        break;  // no threads available: the caller transforms everything itself
      }
#else
      mThreads.emplace_back([this] { run(); });
#endif
    }
  }

  void run() noexcept {
    std::unique_lock lock{mMutex};
    for (;;) {
      mWork.wait(lock, [this] { return mClaimed < mPublished; });
      const u32 row = mClaimed++;
      const FrameJob* job = mJob;
      lock.unlock();
      transform_mcu_row(*job, row);
      lock.lock();
      if (++mDone == mPublished) {
        mIdle.notify_one();
      }
    }
  }

  std::mutex mMutex;
  std::condition_variable mWork;
  std::condition_variable mIdle;
  const FrameJob* mJob = nullptr;
  u32 mPublished = 0;
  u32 mClaimed = 0;
  u32 mDone = 0;
  std::vector<std::thread> mThreads;
  std::vector<BlockJob> mBlocks;
  std::vector<u32> mCounts;
};

// THPVideoDecode is called from one thread at a time in practice; this makes that a guarantee.
std::mutex g_decodeMutex;
} // namespace

extern "C" {
BOOL THPInit(void) { return TRUE; }

BOOL THPVideoFrameSize(const void* file, u16* width, u16* height) {
  if (file == nullptr) {
    return FALSE;
  }
  DecodeContext context{};
  if (parse_headers(file, context) != 0) {
    return FALSE;
  }
  if (width != nullptr) {
    *width = context.width;
  }
  if (height != nullptr) {
    *height = context.height;
  }
  return TRUE;
}

s32 THPVideoDecode(const void* file, void* tileY, void* tileU, void* tileV, void*) {
  if (file == nullptr) {
    return kNoInput;
  }
  if (tileY == nullptr || tileU == nullptr || tileV == nullptr) {
    return kNoOutput;
  }

  DecodeContext context{};
  const s32 headerResult = parse_headers(file, context);
  if (headerResult != 0) {
    return headerResult;
  }

  const u16 chromaWidth = static_cast<u16>((context.width + 1) / 2);
  const u16 chromaHeight = static_cast<u16>((context.height + 1) / 2);

  BitReader bits{static_cast<const uint8_t*>(file), context.scanOffset};
  const u16 mcuColumns = static_cast<u16>((context.width + 15) / 16);
  const u16 mcuRows = static_cast<u16>((context.height + 15) / 16);
  const u32 blocksPerRow = u32{mcuColumns} * 6;

  std::lock_guard decodeLock{g_decodeMutex};
  TransformPool& pool = TransformPool::instance();
  std::vector<BlockJob>& blocks = pool.block_storage();
  std::vector<u32>& blockCounts = pool.count_storage();
  if (blocks.size() < static_cast<size_t>(blocksPerRow) * mcuRows) {
    blocks.resize(static_cast<size_t>(blocksPerRow) * mcuRows);
  }
  if (blockCounts.size() < mcuRows) {
    blockCounts.resize(mcuRows);
  }

  FrameJob job;
  job.context = &context;
  job.blocks = blocks.data();
  job.blockCounts = blockCounts.data();
  job.tileY = static_cast<u8*>(tileY);
  job.tileU = static_cast<u8*>(tileU);
  job.tileV = static_cast<u8*>(tileV);
  job.chromaWidth = chromaWidth;
  job.chromaHeight = chromaHeight;
  job.mcuColumns = mcuColumns;
  pool.begin(&job);

  // Entropy decode on this thread, one MCU row at a time; each finished row is handed to the pool.
  // A block that fails to decode ends the frame there: the blocks before it are still drawn, as
  // they were when decode and transform were interleaved.
  static constexpr u8 kBlockComponent[6]{0, 0, 0, 0, 1, 2};
  bool ok = true;
  u32 restartCount = 0;
  u32 publishedRows = 0;
  for (u16 mcuY = 0; ok && mcuY < mcuRows; ++mcuY) {
    BlockJob* rowBlocks = blocks.data() + static_cast<size_t>(mcuY) * blocksPerRow;
    u32 count = 0;
    for (u16 mcuX = 0; ok && mcuX < mcuColumns; ++mcuX) {
      for (u32 part = 0; part < 6; ++part) {
        const size_t component = kBlockComponent[part];
        BlockJob& block = rowBlocks[count];
        block.rowMask = 1;
        if (!decode_block(bits, context, component, block.coefficients, block.rowMask) ||
            !context.quantizationTables[context.components[component].quantizationTable].valid) {
          ok = false;
          break;
        }
        ++count;
      }
      if (ok && context.restartInterval != 0 && ++restartCount == context.restartInterval) {
        bits.byte_align();
        restartCount = 0;
        for (Component& component : context.components) {
          component.predictedDc = 0;
        }
      }
    }
    blockCounts[mcuY] = count;
    pool.publish(++publishedRows);
  }
  pool.finish();
  if (!ok) {
    return kBadSyntax;
  }
  return 0;
}
}
