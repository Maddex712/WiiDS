#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::RetroRewind,
        "Retro Rewind",
    };
    return descriptor;
}

} // namespace RuntimeProduct
