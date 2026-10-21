#include <jni.h>
#include <string>

#if defined(__aarch64__)
#include <arm_neon.h>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_MainActivity_nativeToolchainCheck(JNIEnv* env, jobject /* this */) {
    std::string result = "libwii.so loaded via JNI.\n";

#if defined(__aarch64__)
    float32x2_t v = vdup_n_f32(1.0f);
    result += (vget_lane_f32(v, 0) > 0.5f) ? "NEON: OK\n" : "NEON: FAIL\n";
#else
    result += "NEON: not arm64 (unexpected on device)\n";
#endif

    result += "This is a toolchain smoke test, not the real app.";
    return env->NewStringUTF(result.c_str());
}

#if defined(__aarch64__)
namespace {

// Same ESR-record-walking algorithm as runtime/src/main.cpp's PosixMemoryFaultHandler /
// ExtractEsr - duplicated here (rather than #including main.cpp, which pulls in SDL3/aurora)
// specifically to validate that exact algorithm against a real hardware Data Abort, which
// qemu-aarch64-static's user-mode signal emulation cannot produce (confirmed during Session 2
// development: qemu never populates the esr_context record). This is a real device test for a
// code path that was previously only compile-checked.
bool ExtractEsr(const mcontext_t& mc, uint64_t& esrOut) {
    const uint8_t* ptr = mc.__reserved;
    const uint8_t* end = mc.__reserved + sizeof(mc.__reserved);
    while (ptr + sizeof(_aarch64_ctx) <= end) {
        const auto* head = reinterpret_cast<const _aarch64_ctx*>(ptr);
        if (head->magic == 0 && head->size == 0) break;
        if (head->magic == ESR_MAGIC) {
            esrOut = reinterpret_cast<const esr_context*>(ptr)->esr;
            return true;
        }
        if (head->size == 0) break;
        ptr += head->size;
    }
    return false;
}

sigjmp_buf g_jumpBuf;
volatile sig_atomic_t g_esrFound = 0;
volatile sig_atomic_t g_isWrite = 0;
volatile uint64_t g_esrValue = 0;

void FaultHandler(int /*sig*/, siginfo_t* /*info*/, void* ucontextVoid) {
    if (ucontextVoid != nullptr) {
        auto* uc = static_cast<ucontext_t*>(ucontextVoid);
        uint64_t esr = 0;
        if (ExtractEsr(uc->uc_mcontext, esr)) {
            g_esrFound = 1;
            g_esrValue = esr;
            g_isWrite = ((esr >> 6) & 1u) != 0 ? 1 : 0;
        }
    }
    siglongjmp(g_jumpBuf, 1);
}

// Triggers a real fault (write to a read-only mmap'd page, or a read from an unmapped one) and
// reports what the ESR walk saw. Returns true if the expected write/read classification matched.
bool RunFaultCase(bool wantWrite, std::string& detail) {
    g_esrFound = 0;
    g_isWrite = 0;
    g_esrValue = 0;

    struct sigaction action {};
    struct sigaction previous {};
    action.sa_sigaction = FaultHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, &previous);

    const long pageSize = sysconf(_SC_PAGESIZE);
    // PROT_NONE guarantees a fault on EITHER a read or a write, unlike guessing at an address
    // that happens to be unmapped (which can silently land in some other valid mapping instead -
    // exactly what happened on-device the first time this test ran with a PROT_READ page and an
    // offset guess for the "read" case: no fault occurred at all, so the test correctly reported
    // it as unhandled rather than a false pass).
    void* page = mmap(nullptr, static_cast<size_t>(pageSize), PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool handled = false;
    if (page != MAP_FAILED) {
        if (sigsetjmp(g_jumpBuf, 1) == 0) {
            if (wantWrite) {
                *static_cast<volatile int*>(page) = 42;
            } else {
                volatile int value = *static_cast<volatile int*>(page);
                (void)value;
            }
        } else {
            handled = true;
        }
        munmap(page, static_cast<size_t>(pageSize));
    }
    sigaction(SIGSEGV, &previous, nullptr);

    char buf[192];
    snprintf(buf, sizeof(buf),
             "  %s fault: handled=%d esr_found=%d esr=0x%llx detected_write=%d (expected %d)\n",
             wantWrite ? "write" : "read", handled ? 1 : 0, static_cast<int>(g_esrFound),
             static_cast<unsigned long long>(g_esrValue), static_cast<int>(g_isWrite),
             wantWrite ? 1 : 0);
    detail += buf;

    return handled && g_esrFound && (static_cast<bool>(g_isWrite) == wantWrite);
}

}  // namespace
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_MainActivity_nativeArm64FaultCheck(JNIEnv* env, jobject /* this */) {
#if defined(__aarch64__)
    std::string result = "arm64 ESR write/read fault detection (real hardware test):\n";
    std::string detail;
    const bool writeOk = RunFaultCase(true, detail);
    const bool readOk = RunFaultCase(false, detail);
    result += detail;
    result += (writeOk && readOk)
                   ? "RESULT: PASS - ExtractEsr() correctly classified both faults on real hardware.\n"
                   : "RESULT: FAIL - see esr_found/detected_write above.\n";
    return env->NewStringUTF(result.c_str());
#else
    return env->NewStringUTF("Not arm64 - test skipped.");
#endif
}
