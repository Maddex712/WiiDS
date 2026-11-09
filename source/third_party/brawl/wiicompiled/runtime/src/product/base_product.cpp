#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "Super Smash Bros. Brawl",
    };
    return descriptor;
}

} // namespace RuntimeProduct
