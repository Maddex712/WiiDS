// Motion telemetry for this game: the player's kart, sent to a motion platform through
// gcn-recomp's telemetry (runtime/telemetry.h). See motion.cpp.
#pragma once

namespace mkdd {

// Once per game frame, on the guest thread. `live` is the stereo answer: a race the player
// drives, unpaused (vr_mkdd.cpp); outside it the platform settles to rest.
void motion_frame(bool live);

}  // namespace mkdd
