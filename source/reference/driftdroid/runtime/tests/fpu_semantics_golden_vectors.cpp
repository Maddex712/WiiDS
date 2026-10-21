// Golden-vector cross-arch test for the P2 ARM64 ISA port (03-ARM64-PORT-NOTES.md #15).
//
// Exercises the paired-single/scalar FPU helpers with a fixed set of interesting float32 bit
// patterns (normals, denormals, +-0, +-Inf, quiet/signaling NaN) and prints every result as raw
// hex bits. The SAME source, compiled once natively for x86_64 (SSE) and once for arm64 (via
// sse2neon - see ppc_isa_config.h), must be diffed for a correctness proof before trusting the
// arm64 ISA port - do NOT skip this step when touching ppc_isa_float.h / ppc_isa_quantized.h.
//
// Not wired into CMake/ctest yet (P2 has no arm64 execution target on this dev machine - no
// device/emulator). To reproduce the Session 2 verification manually:
//
//   # x86_64 (native reference)
//   clang++ -std=c++20 -I <repo root> -I runtime/include -mfma -O2 -o /tmp/golden_x86 \
//       runtime/tests/fpu_semantics_golden_vectors.cpp
//   /tmp/golden_x86 > /tmp/golden_x86_out.txt
//
//   # arm64, run under qemu-aarch64-static (apt-get download qemu-user-static + the
//   # gcc-aarch64-linux-gnu / libc6-dev-arm64-cross / g++-aarch64-linux-gnu family of packages;
//   # all installable with `apt-get download` + `dpkg-deb -x`, no root needed)
//   aarch64-linux-gnu-g++-13 --sysroot=<extracted sysroot> -std=c++20 -I <repo root> \
//       -I runtime/include -static -O2 -march=armv8-a -o /tmp/golden_arm \
//       runtime/tests/fpu_semantics_golden_vectors.cpp
//   qemu-aarch64-static /tmp/golden_arm > /tmp/golden_arm_out.txt
//
//   diff /tmp/golden_x86_out.txt /tmp/golden_arm_out.txt
//
// Session 2 (2026-09-03) result: bit-exact on every finite-number result across every paired
// add/sub/mul/div/madd/msub/madds0/madds1/nmsub/muls0/muls1/merge*/sel/neg/abs/sum0/sum1, fres,
// frsqrte, fctiwz, ForceSingleValue (NI mode too), fmadds/fmsubs/fnmadds/fnmsubs, and every
// quantized (psq) bit-manipulation helper tested. The ONLY divergences (30 lines out of ~660,
// all confirmed by grep to touch nothing but NaN/Inf bit patterns) are the sign bit and/or
// mantissa payload of a resulting quiet NaN from an operation with a NaN operand (e.g.
// Inf * NaN). This is expected: IEEE 754 leaves the sign and payload of a NaN produced by an
// operation implementation-defined, x86 SSE and ARM NEON pick different (both valid) answers,
// and the reference x86 implementation was itself already only an approximation of real Gekko
// hardware's NaN propagation (see PpcApproximateReciprocal*'s own comments). Not treated as a
// port bug; revisit only if in-game behavior ever depends on a specific NaN payload surviving
// paired-single arithmetic (very unlikely - NaN propagation through gameplay math is normally a
// bug state, not depended-upon behavior).

#include "runtime/include/isa/ppc_isa_quantized.h"
#include <cstdio>
#include <cstring>

void ShowRuntimeFatalPopup(std::string_view, std::string_view) noexcept {}
void MarkFatalErrorReported() {}
extern "C" void DumpHostStackTraceForRuntimeHelper() {}

static uint64_t Bits(double v) { uint64_t b; std::memcpy(&b, &v, 8); return b; }
static uint32_t Bits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }

static double MakePaired(uint32_t ps0Bits, uint32_t ps1Bits)
{
    return PpcPackPairedBitsInline(ps0Bits, ps1Bits);
}

static void PrintPaired(const char* label, double v)
{
    printf("%-10s = 0x%016llX  (ps0=0x%08X ps1=0x%08X)\n",
           label, (unsigned long long)Bits(v), Bits(PpcGetPs0Inline(v)), Bits(PpcGetPs1Inline(v)));
}

int main()
{
    const uint32_t testBits[] = {
        0x3F800000u, 0x40490FDBu, 0xBFC00000u, 0x00000000u, 0x80000000u,
        0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7F800001u, 0x00000001u,
        0x007FFFFFu, 0x00800000u, 0xC1200000u, 0x3E4CCCCDu,
    };
    const size_t n = sizeof(testBits) / sizeof(testBits[0]);

    // NI mode off (default) sweep.
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        const double a = MakePaired(testBits[i], testBits[i + 1]);
        const double b = MakePaired(testBits[(i + 2) % n], testBits[(i + 3) % n]);
        const double c = MakePaired(testBits[(i + 5) % n], testBits[(i + 7) % n]);

        printf("--- pair %zu ---\n", i / 2);
        PrintPaired("a", a);
        PrintPaired("b", b);
        PrintPaired("c", c);
        PrintPaired("add", PPC_PsAddInline(a, b));
        PrintPaired("sub", PPC_PsSubInline(a, b));
        PrintPaired("mul", PPC_PsMulInline(a, b));
        PrintPaired("div", PPC_PsDivInline(a, b));
        PrintPaired("madd", PPC_PsMaddInline(a, b, c));
        PrintPaired("msub", PPC_PsMsubInline(a, b, c));
        PrintPaired("madds0", PPC_PsMadds0Inline(a, b, c));
        PrintPaired("madds1", PPC_PsMadds1Inline(a, b, c));
        PrintPaired("nmsub", PpcM128ToPsInline(PpcNegateNonNanLanesInline(_mm_fmsub_ps(
            PpcPsToM128Inline(a), PpcPsToM128Inline(b), PpcPsToM128Inline(c)))));
        PrintPaired("muls0", PPC_PsMuls0Inline(a, b));
        PrintPaired("muls1", PPC_PsMuls1Inline(a, b));
        PrintPaired("merge00", PPC_PsMerge00Inline(a, b));
        PrintPaired("merge01", PPC_PsMerge01Inline(a, b));
        PrintPaired("merge10", PPC_PsMerge10Inline(a, b));
        PrintPaired("merge11", PPC_PsMerge11Inline(a, b));
        PrintPaired("sel", PPC_PsSelInline(a, b, c));
        PrintPaired("neg", PPC_PsNegInline(a));
        PrintPaired("abs", PPC_PsAbsInline(a));
        PrintPaired("sum0", PPC_PsSum0Inline(a, b, c));
        PrintPaired("sum1", PPC_PsSum1Inline(a, b, c));
        printf("fres_ps0   = 0x%08X\n", Bits(static_cast<float>(PpcApproximateReciprocalInline(static_cast<double>(PpcGetPs0Inline(a))))));
        printf("frsqrte_a  = 0x%016llX\n", (unsigned long long)Bits(PpcApproximateReciprocalSquareRootInline(a)));
        printf("fctiwz_a   = 0x%08X\n", (uint32_t)PpcClampIntegerWordInline(a));
        printf("force_sgl  = 0x%08X\n", Bits(PpcForceSingleValueInline(a)));
        printf("fmadds     = 0x%016llX\n", (unsigned long long)Bits(PpcFmaddsInline(a, b, c)));
        printf("fmsubs     = 0x%016llX\n", (unsigned long long)Bits(PpcFmsubsInline(a, b, c)));
        printf("fnmadds    = 0x%016llX\n", (unsigned long long)Bits(PpcFnmaddsInline(a, b, c)));
        printf("fnmsubs    = 0x%016llX\n", (unsigned long long)Bits(PpcFnmsubsInline(a, b, c)));
    }

    // NI mode on sweep (denormal flushing) - exercise MkwApplyHostNiMode / PpcForceSingleValueInline.
    MkwApplyHostNiMode(0x4u);
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        const double a = MakePaired(testBits[i], testBits[i + 1]);
        printf("--- NI pair %zu ---\n", i / 2);
        printf("force_sgl_ni = 0x%08X\n", Bits(PpcForceSingleValueInline(a)));
        PrintPaired("add_ni", PPC_PsAddInline(a, a));
    }
    MkwApplyHostNiMode(0x0u);

    // Quantized (psq) bit-manipulation helpers that don't need guest memory addressing -
    // exercises the SSE shuffle/pack intrinsics in ppc_isa_quantized.h directly.
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        const uint32_t bitsA = testBits[i];
        const uint32_t bitsB = testBits[i + 1];
        printf("--- quant pair %zu ---\n", i / 2);
        printf("loadFloatBits    = 0x%08X\n", PpcLoadPsqFloatBitsInline(bitsA));
        printf("storeFloatBits   = 0x%08X\n", PpcStorePsqFloatBitsInline(bitsA));

        const uint64_t packed = (static_cast<uint64_t>(bitsA) << 32) | bitsB;
        printf("loadPairPacked   = 0x%016llX\n", (unsigned long long)PpcLoadPairPsqFloatBitsPackedInline(packed));
        printf("storePairPacked  = 0x%016llX\n", (unsigned long long)PpcStorePairPsqFloatBitsPackedInline(packed));

        const float valueA = PpcBitCastToFloatInline(bitsA);
        for (uint32_t scale = 0; scale < 32; scale += 7)
        {
            const int32_t clamped8 = PpcScaleAndClampPsqInline<int8_t>(valueA, scale);
            const float dequant8 = PpcDequantizePsqInline<int8_t>(static_cast<int8_t>(clamped8), scale);
            printf("scale=%2u i8 clamp=%d dequant=0x%08X\n", scale, clamped8, Bits(dequant8));
        }
        printf("quantU8Scale61   = 0x%02X\n", PpcQuantizePsqU8Scale61Inline(valueA));
        const double pairV = MakePaired(bitsA, bitsB);
        printf("quantPairU8S61   = 0x%04X\n", PpcQuantizePairPsqU8Scale61PackedInline(pairV));
    }

    return 0;
}
