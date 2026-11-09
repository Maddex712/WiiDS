#pragma once

#include "wii_remote_input.h"

#include <array>
#include <cstdint>

// Deterministic mode (det_clock.h): the Wii Remotes every guest read sees during one frame. KPAD and
// WPAD normally read the controllers live, which would let a press land on different frames on
// different devices; in this mode all four channels are latched at each VI retrace and held until
// the next one. Network play swaps the latch source for every player's input for that frame.
namespace FrameInput {

// A GameCube controller port as PADRead reports it (Brawl's main input).
struct GcPad {
    bool connected = false;
    uint16_t button = 0;
    int8_t stickX = 0, stickY = 0, substickX = 0, substickY = 0;
    uint8_t triggerL = 0, triggerR = 0;
};

struct Remote {
    bool connected = false;
    WiiRemoteInput::Kind kind = WiiRemoteInput::Kind::NotWii;
    WiiRemoteInput::KpadSample sample{};
    GcPad pad{};  // the GameCube port with the same number
};
using Frame = std::array<Remote, 4>;

// True when KPAD/WPAD read the latched frame instead of the live controllers.
bool Active();
// Latches the channels for the frame that starts at this retrace.
void Latch(uint32_t retraceCount);
const Remote& Get(uint32_t chan);

// The local controllers as one frame (what Latch uses without a network source). Neutral while an
// overlay owns input.
Frame SampleLocal();

// Network play: fills `frame` for `retraceCount`, waiting until every player's input for it has
// arrived. nullptr restores local sampling.
using Source = void (*)(uint32_t retraceCount, Frame& frame);
void SetSource(Source source);

} // namespace FrameInput
