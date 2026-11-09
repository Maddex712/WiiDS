#pragma once
#include "gx/render.h"
#include "lower_state.h"
#include <unordered_map>

namespace wiids::mkdd {
// Owns renderer-side texture identities only. It never writes guest memory or save data.
class WidescreenHud {
public:
    struct Result { unsigned left = 0, right = 0, clipped = 0; };
    Result fit(gx::Batch& batch, bool widescreen, const Packet& race);
private:
    std::unordered_map<uint32_t, int> textures_;
    std::vector<uint32_t> dead_pending_;
    std::vector<std::array<uint32_t, 2>> shifted_;
};
}
