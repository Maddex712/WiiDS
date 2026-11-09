#include "widescreen_hud.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wiids::mkdd {
namespace {
enum class HudFamily { Left = 1, Right, Side, Item, SlotMask };
struct HudSignature { uint64_t hash; HudFamily family; };
// Fingerprints only, built from the owner's verified disc outside the repository.
#include "wiids_hud_profile.h"

int family(uint64_t hash) {
    const auto* end = std::end(kHudSignatures);
    const auto* found = std::lower_bound(std::begin(kHudSignatures), end, hash,
        [](const HudSignature& signature, uint64_t h) { return signature.hash < h; });
    return found != end && found->hash == hash ? static_cast<int>(found->family) : 0;
}

struct Bounds { float x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY; };
bool bounds(const gx::Batch& b, const gx::Cmd& c, const gx::PixelState& st, Bounds& box) {
    if (!c.count || c.first > b.indices.size() || c.count > b.indices.size()-c.first) return false;
    for (uint32_t i = 0; i < c.count; ++i) {
        const uint32_t index = b.indices[c.first+i];
        if (index >= b.verts.size()) return false;
        const float* p = b.verts[index].pos;
        const float x = st.proj[0]*p[0]+st.proj[1], y = st.proj[2]*p[1]+st.proj[3];
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        box.x0 = std::min(box.x0, x); box.x1 = std::max(box.x1, x);
        box.y0 = std::min(box.y0, y); box.y1 = std::max(box.y1, y);
    }
    return true;
}

int side(int value, const Bounds& box) {
    const float x = (box.x0+box.x1)*0.5f;
    // PAN2/PIC2 positions from the disc's p1_race.blo and characterindication.blo.
    // Shared digits use their original position; countdown/GO/result art is not in the profile.
    switch (static_cast<HudFamily>(value)) {
    case HudFamily::Left: return x < -0.25f ? -1 : 0;
    case HudFamily::Right: return x > 0.10f ? 1 : 0;
    case HudFamily::Side: return x < -0.32f ? -1 : x > 0.20f ? 1 : 0;
    case HudFamily::Item: return x < 0.15f && box.y0 > 0.25f ? -1 : 0;
    case HudFamily::SlotMask:
        return box.x0 > -0.70f && box.x1 < 0.17f && box.y0 > 0.40f ? -1 : 0;
    }
    return 0;
}
}

WidescreenHud::Result WidescreenHud::fit(gx::Batch& batch, bool widescreen, const Packet& race) {
    // Observe identities even in 4:3 so switching aspect live works with already-resident art.
    // Match the renderer's one-batch delayed deletion of textures.
    for (uint32_t id : dead_pending_) textures_.erase(id);
    dead_pending_ = batch.dead_textures;
    for (const auto& texture : batch.new_textures) {
        const int f = family(texture->hash);
        if (f) textures_[texture->id] = f;
    }
    Result result;
    if (!widescreen) return result;
    bool race_hud = (race[Flags]&Race) && race[Screens] == 1 &&
        race[Mode] >= 1 && race[Mode] <= 3 && race[Phase] == 17;
    // The race's goal-rank animation becomes a centred result, rather than an edge HUD.
    const int target = race[TargetKart];
    if (target >= 0 && target < int(kKarts) && race[kHeader+target*kStride+Finished]) race_hud = false;

    const size_t original = batch.states.size();
    constexpr uint32_t none = std::numeric_limits<uint32_t>::max();
    shifted_.assign(original, {none, none});
    for (auto& cmd : batch.cmds) {
        if (cmd.type != gx::CmdType::Draw || cmd.state >= original) continue;
        const gx::PixelState& st = batch.states[cmd.state];
        if (st.proj[6] != 1.0f || !std::isfinite(st.proj[0]) || st.proj[0] <= 0 ||
            std::abs(st.viewport[0]) < 250 || std::abs(st.viewport[1]) < 180) continue;
        // A sampled EFB image is the already-drawn course/composite, never a HUD picture.
        bool copy = false, known = false;
        for (int i = 0; i < 8; ++i) {
            copy |= st.tex_id[i] && st.tex_is_efb[i];
            known |= textures_.find(st.tex_id[i]) != textures_.end();
        }
        if (copy) continue;
        Bounds box;
        if (!bounds(batch, cmd, st, box)) continue;
        // Widening the projection also exposed whole 2D panes the game's animations
        // park outside the original view (stray rank digits on the title/course menu).
        // They were completely clipped on the console. Keep them hidden, before anchoring.
        // Sampled frame copies were excluded above; partially visible panes stay intact.
        if (box.x1 <= -1 || box.x0 >= 1 || box.y1 <= -1 || box.y0 >= 1) {
            cmd.count = 0;
            ++result.clipped;
            continue;
        }
        if (!race_hud || !known || box.x1-box.x0 >= 1.2f || box.y1-box.y0 >= 1.9f) continue;
        int anchor = 0;
        bool conflict = false;
        for (int i = 0; i < 8; ++i) {
            auto found = textures_.find(st.tex_id[i]);
            if (found == textures_.end()) continue;
            const int candidate = side(found->second, box);
            if (candidate && anchor && candidate != anchor) conflict = true;
            if (candidate) anchor = candidate;
        }
        if (!anchor || conflict) continue;
        const unsigned slot = anchor > 0 ? 1 : 0;
        uint32_t& shifted = shifted_[cmd.state][slot];
        if (shifted == none) {
            gx::PixelState moved = st;
            // The flat renderer already squeezes x by 3/4. An extra +/-1/3 in the
            // original projection becomes +/-1/4 of clip space: 240 px at 1920 wide.
            // P00 (icon width), y, UVs, vertices and all original states stay untouched.
            moved.proj[1] += anchor/3.0f;
            shifted = static_cast<uint32_t>(batch.states.size());
            batch.states.push_back(moved);
        }
        cmd.state = shifted;
        if (anchor < 0) ++result.left; else ++result.right;
    }
    return result;
}
}
