#pragma once
// NW4R layout baking (BRLYT + BRLAN + BRCTR) for the Mario Kart menu and HUD art.
#include "wiids_art.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace wiids::art {

// Archive members by path, as the PC tools name them ("bg/blyt/bg.brlyt").
using Archive = std::map<std::string, Bytes>;
Archive OpenArchive(const Bytes& szs); // Yaz0 or plain U8
const Bytes& Member(const Archive& archive, const std::string& path);
std::string ArchivePath(const std::string& path);

struct ControllerBindings {
    struct Animation { std::string name, file; };
    struct Group { std::string name, panes; std::vector<Animation> animations; };
    std::string layout;
    std::vector<Group> groups;
};
ControllerBindings ControllerAnimations(const Bytes& brctr);

struct LayoutRequest {
    std::string layout;                          // archive path of the .brlyt
    std::vector<std::string> animations;         // archive paths of .brlan clips, in order
    std::vector<std::string> animationGroups;    // per clip: group whose panes it may drive ("" = all)
    double frame = 0;
    bool hasBounds = false;
    double bounds[4] = {0, 0, 0, 0};             // left, top, right, bottom (y down, screen centre origin)
    std::vector<std::string> include;            // only these panes draw (empty = all)
    std::map<std::string, std::array<std::array<double, 4>, 3>> materialOverrides; // TEV colour registers
    double aspect[2] = {1, 1};
};

// Bakes the layout's picture panes; writes the bounds used (auto when !hasBounds).
Image RenderLayout(const Archive& archive, const LayoutRequest& request, double outBounds[4]);

} // namespace wiids::art
