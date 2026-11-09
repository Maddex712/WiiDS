#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace NandCheckContract {

inline constexpr int32_t kResultOk = 0;
inline constexpr int32_t kResultInvalid = -8;
inline constexpr uint32_t kHealthyResultBits = 0;
inline constexpr size_t kResultSize = sizeof(uint32_t);


template <typename RangeValidator, typename WordWriter>
int32_t WriteHealthyResult(uint32_t outResults,
                           RangeValidator&& contains,
                           WordWriter&& write32)
{
    if (outResults == 0 ||
        !std::forward<RangeValidator>(contains)(outResults, kResultSize)) {
        return kResultInvalid;
    }

    if (!std::forward<WordWriter>(write32)(outResults, kHealthyResultBits)) {
        return kResultInvalid;
    }
    return kResultOk;
}

} // namespace NandCheckContract
