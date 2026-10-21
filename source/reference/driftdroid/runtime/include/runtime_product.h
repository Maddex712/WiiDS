#pragma once

#include <string_view>

namespace RuntimeProduct {

enum class Kind {
    BaseGame,
    RetroRewind,
};

struct Descriptor {
    Kind kind;
    std::string_view displayName;
};

// Each public executable/library links exactly one small provider definition
// (runtime/src/product/{base,retro_rewind,combined}_product.cpp). Keeping this selection
// out of target-wide preprocessor definitions lets the native runtime be compiled once and
// shared by every product.
//
// base_product.cpp/retro_rewind_product.cpp (desktop, and Android's standalone
// single-profile WiiCompiled.so/RetroRewind.so targets kept around for testing) give
// Active() a fixed compile-time answer; their SetActive() is a documented no-op, present
// only so runtime/src/android_jni_bridge.cpp's single shared JNI export
// (nativeSetActiveProduct) links against any of the three providers.
//
// combined_product.cpp (Android's real libGameCombined.so, see
// runtime/cmake/build_combined_android_lib.py) is the one provider where Active() actually
// reads a value SetActive() set during startup, before any guest code or HLE runs.
const Descriptor& Active() noexcept;
void SetActive(Kind kind) noexcept;

inline bool IsRetroRewind() noexcept {
    return Active().kind == Kind::RetroRewind;
}

} // namespace RuntimeProduct
