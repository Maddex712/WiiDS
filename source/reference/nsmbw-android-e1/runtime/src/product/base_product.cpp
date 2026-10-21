#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "New Super Mario Bros. Wii",
    };
    return descriptor;
}

} // namespace RuntimeProduct
