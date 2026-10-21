#include "runtime_product.h"
#include "abi_bridge.h"
#include "recomp_mod_loader.h"
#include "runtime_log.h"
#include "system_bridge.h"

#include <stdexcept>

// Used only by the combined Android library (both products in one .so). Desktop targets
// keep linking base_product.cpp / retro_rewind_product.cpp instead - see runtime_product.h.

namespace RuntimeProduct {

namespace {
Kind g_activeKind = Kind::BaseGame;
}

void SetActive(Kind kind) noexcept {
    g_activeKind = kind;
    const char* profile = kind == Kind::RetroRewind ? "retro_rewind" : "base";
    // Note: RT_LOG/std::cerr here is invisible in practice - this runs before main.cpp's
    // InitializeProcessTranscript/AttachParentConsoleForDiagnostics redirect stdout/stderr to the
    // runtime's own log file. Confirmed via a temporary direct-file-write diagnostic that this
    // function and SelectProfile/ActivateProfile below all do run and complete successfully -
    // don't mistake silence here for a call that never happened.
    // Both products' generated dispatch tables AND mod registrations (memory reservations, data
    // patches, ctors, Riivolution config) are linked into this one library unconditionally at
    // load time - this is the single call site that picks which profile actually drives guest
    // dispatch (TranslatedFunctionRegistry::SelectProfile, abi_bridge.cpp) and which profile's
    // deferred mod registration actually applies (RecompMod::ActivateProfile,
    // recomp_mod_loader.cpp - a no-op for "base", since only Retro Rewind's generated code ever
    // queues anything). Must run before TranslatedFunctionRegistry::Finalize() (main.cpp), which
    // is guaranteed here since SetActive is called from the JNI bridge before SDL's thread starts
    // main(). SelectProfile/ActivateProfile throw on error rather than abort() directly (matching
    // KartPad's design this was ported from) - translate to this runtime's usual fatal-popup
    // convention here since SetActive itself is noexcept.
    try {
        TranslatedFunctionRegistry::SelectProfile(profile);
        RecompMod::ActivateProfile(profile);
    } catch (const std::exception& error) {
        ShowRuntimeFatalPopup("translated dispatch initialization failed", error.what());
        std::abort();
    }
}

const Descriptor& Active() noexcept {
    static constexpr Descriptor kBase{Kind::BaseGame, "WiiCompiled"};
    static constexpr Descriptor kRetroRewind{Kind::RetroRewind, "Retro Rewind"};
    return g_activeKind == Kind::RetroRewind ? kRetroRewind : kBase;
}

} // namespace RuntimeProduct
