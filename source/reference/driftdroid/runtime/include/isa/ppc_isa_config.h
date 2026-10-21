#pragma once

#include <atomic>
#include <cstdint>

#define MKW_RESTRICT __restrict

#if defined(__aarch64__)
// The ISA headers (ppc_isa_float.h, ppc_isa_quantized.h) are written against the SSE/SSE2
// intrinsic surface directly - sse2neon.h supplies that same __m128/__m128i/__m128d API backed
// by NEON, so those files need NO changes at all on arm64 (verified compiling both unchanged
// through this header for arm64-v8a). Vendored at runtime/third_party/sse2neon (MIT, upstream
// github.com/DLTcollab/sse2neon) rather than hand-porting every intrinsic call site individually
// - a sibling ARM64 static-recompilation port of this same upstream project (for Apple platforms)
// took the identical approach for the identical reason.
#include "../../third_party/sse2neon/sse2neon.h"

// sse2neon deliberately stops at SSE4.2 (see its own header comment); the translated PPC helpers
// use these two FMA3 intrinsics (introduced with AVX2/FMA, i.e. after SSE4.2) to get a single
// rounding step for paired-single arithmetic. Both map to one NEON fused multiply-add/subtract
// instruction, so they're exact per-lane single roundings, matching what the x86 FMA3 path does.
inline __m128 _mm_fmadd_ps(__m128 a, __m128 b, __m128 c)
{
    return vfmaq_f32(c, a, b);
}

inline __m128 _mm_fmsub_ps(__m128 a, __m128 b, __m128 c)
{
    return vfmaq_f32(vnegq_f32(c), a, b);
}
#else
#include <immintrin.h>
#endif

inline constexpr bool MkwStateFreeAbiEnabled(uint32_t) noexcept
{
    return true;
}

#if defined(_WIN32)
#define MKW_PPC_FORCE_INLINE __forceinline
#define MKW_PPC_NO_INLINE __declspec(noinline)
#define MKW_PPC_INTERNAL_CALL __regcall
#else
// __forceinline/__declspec are MS-extension keywords Clang only recognizes when targeting
// Windows (MSVC or mingw); native Linux/Android Clang needs the GNU-attribute spellings
// instead. __regcall has no portable non-Windows equivalent worth chasing here (and the code
// generator's StateFreeCallingConvention() always emits an empty string anyway — see
// CxxLinearCodeGenerator.StateFreeAbi.cs:145; __regcall was abandoned there because a
// state-free callee using host r12 as scratch once collided with it) — plain calls are fine.
#define MKW_PPC_FORCE_INLINE __attribute__((always_inline)) inline
#define MKW_PPC_NO_INLINE __attribute__((noinline))
#define MKW_PPC_INTERNAL_CALL
#endif
// `inline` matters here, not just the attribute: without it, a plain `extern "C"` function with
// __attribute__((always_inline)) has genuine external linkage, and GCC refuses to honor
// always_inline for a symbol another translation unit could in principle override ("function
// body can be overwritten at link time") - confirmed hitting this across dozens of generated
// _statefree_v0 shard functions on GCC (devkitA64/Switch). `inline` gives it the ODR's vague/weak
// linkage guarantee instead, which is what always_inline needs to safely apply - harmless
// addition for Clang, which already tolerated the attribute-only form. Other shards that only
// forward-declare these functions (without repeating the attribute or `inline`) are unaffected:
// per the C++ standard, `inline` is a property of the definition, not something every mention
// needs to repeat, and a weak symbol is still emitted for genuinely cross-shard calls.
// `used` is a further belt-and-braces addition on top of the `inline` fix described above: that
// fix's own reasoning ("a weak symbol is still emitted for genuinely cross-shard calls") turned
// out not to hold in practice on devkitA64's GCC 16 - confirmed empirically (nm on the compiled
// .o showed the plain MKW_PPC_NO_INLINE sibling function present but the always_inline one
// missing, from the exact same translation unit, despite both being extern "C" with external
// linkage). `__attribute__((used))` forces GCC to emit code for the function unconditionally,
// sidestepping whatever specific heuristic (visible only via compiled-object inspection, not
// reasoned about correctly from the ODR/weak-linkage rules alone) was still eliding it.
#define MKW_PPC_ALWAYS_INLINE_BODY __attribute__((always_inline, used)) inline
#define MKW_PPC_COLD __attribute__((cold))


// ext_vector_type is a portable Clang vector extension (not x86-specific), so the same
// definition works unchanged on arm64 - but it's Clang-only. Switch (devkitA64) is GCC-only (see
// runtime/CMakeLists.txt's MKW_TARGET_SWITCH branch) and GCC does not understand ext_vector_type
// at all - it silently falls through to treating MkwStateFreeResult2 as a plain scalar uint64_t,
// which breaks every generated shard using [0]/[1] subscript access or brace-enclosed-initializer
// construction on it (confirmed: dozens of generated func_*.cpp files fail this way). GCC's own
// vector_size attribute is the portable equivalent - same subscript and brace-init semantics,
// verified directly against devkitA64's compiler.
#if defined(__clang__)
using MkwStateFreeResult2 = uint64_t __attribute__((ext_vector_type(2)));
#else
using MkwStateFreeResult2 = uint64_t __attribute__((vector_size(16)));
#endif
