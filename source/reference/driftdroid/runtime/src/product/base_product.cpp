#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "WiiCompiled",
    };
    return descriptor;
}

// No-op: this provider's Active() answer is fixed. Exists only so
// android_jni_bridge.cpp's nativeSetActiveProduct links against this provider too - see
// runtime_product.h.
void SetActive(Kind) noexcept {}

} // namespace RuntimeProduct
