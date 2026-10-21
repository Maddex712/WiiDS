// Auto-generated GX fatal stubs for NSMBW SMNE01 rev 1 (tools/gen_gx_fatal_stubs.py)
#include "hle_stubs.h"
#include "runtime_log.h"

namespace {
[[noreturn]] void HaltGX(uint32_t addr, const char* name) {
    const char* symbol = name ? name : "<unknown GX symbol>";
    RT_LOGF(RT_TAG_GX,
            "unimplemented GX entry point: %s at guest address 0x%08X.\n"
            "[gx] This graphics call has no Aurora implementation bound to it yet, so the\n"
            "[gx] runtime cannot continue without silently dropping GPU state. Bind it in\n"
            "[gx] runtime/src/hle/gx/ and remove the stub from gx_fatal_stubs.cpp.\n",
            symbol, addr);
    std::fflush(stderr);
    char message[512]{};
    std::snprintf(message, sizeof(message),
                  "%s at guest address 0x%08X has no Aurora implementation bound to it, so the "
                  "runtime stopped rather than keep rendering with missing GPU state.",
                  symbol, addr);
    ShowRuntimeFatalPopup("the game called an unimplemented graphics function", message);
    std::abort();
}
} // namespace

// Every fatal stub is the same two statements with the address and the symbol
// name substituted, so the body comes from this macro. The registration is
// deliberately still spelled out per entry so the translator's runtime-native
// index sees the literal PPC_NATIVE_OVERRIDE_VOID invocation. Hiding it inside
// this macro would leave the index unable to associate an address with the stub.
#define GX_FATAL_STUB(addr, sym) \
    extern "C" void gx_stub_##addr(CpuContext* ctx) { (void)ctx; HaltGX(0x##addr, sym); }

GX_FATAL_STUB(801c19f0, "__GXDefaultTexRegionCallback_801c19f0") PPC_NATIVE_OVERRIDE_VOID(801c19f0, gx_stub_801c19f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c1ae0, "__GXDefaultTlutRegionCallback_801c1ae0") PPC_NATIVE_OVERRIDE_VOID(801c1ae0, gx_stub_801c1ae0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c1b10, "__GXShutdown_801c1b10") PPC_NATIVE_OVERRIDE_VOID(801c1b10, gx_stub_801c1b10, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c2b80, "GXCPInterruptHandler_801c2b80") PPC_NATIVE_OVERRIDE_VOID(801c2b80, gx_stub_801c2b80, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c35c0, "__GXCleanGPFifo_801c35c0") PPC_NATIVE_OVERRIDE_VOID(801c35c0, gx_stub_801c35c0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c3c40, "__GXSetVCD_801c3c40") PPC_NATIVE_OVERRIDE_VOID(801c3c40, gx_stub_801c3c40, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c3cf0, "__GXCalculateVLim_801c3cf0") PPC_NATIVE_OVERRIDE_VOID(801c3cf0, gx_stub_801c3cf0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c4420, "__GXSetVAT_801c4420") PPC_NATIVE_OVERRIDE_VOID(801c4420, gx_stub_801c4420, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c4b70, "__GXAbort_801c4b70") PPC_NATIVE_OVERRIDE_VOID(801c4b70, gx_stub_801c4b70, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c56c0, "__GXSendFlushPrim_801c56c0") PPC_NATIVE_OVERRIDE_VOID(801c56c0, gx_stub_801c56c0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c58f0, "__GXSetGenMode_801c58f0") PPC_NATIVE_OVERRIDE_VOID(801c58f0, gx_stub_801c58f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c97f0, "__GXSetProjection_801c97f0") PPC_NATIVE_OVERRIDE_VOID(801c97f0, gx_stub_801c97f0, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c9b40, "__GXSetViewport_801c9b40") PPC_NATIVE_OVERRIDE_VOID(801c9b40, gx_stub_801c9b40, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c9d90, "__GXSetMatrixIndex_801c9d90") PPC_NATIVE_OVERRIDE_VOID(801c9d90, gx_stub_801c9d90, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801c9e20, "GXSetGPMetric_801c9e20") PPC_NATIVE_OVERRIDE_VOID(801c9e20, gx_stub_801c9e20, (CpuContext* ctx), (ctx));
GX_FATAL_STUB(801ca640, "GXClearGPMetric_801ca640") PPC_NATIVE_OVERRIDE_VOID(801ca640, gx_stub_801ca640, (CpuContext* ctx), (ctx));
