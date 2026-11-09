// What this game does with the pad's sticks, for controls.txt's stick ranges
// (gcn-recomp/runtime/input_defaults.h): see docs/dev/controls.md.
//
// Every stick reaches the karts through two steps. The SDK's PADClamp, which JUTGamePad
// calls for the game's clamp mode, takes 15 off each axis and caps it at 72; then
// KartGamePad::compress packs the pad for the ghost recorder and the karts drive from
// what it unpacks: x divided by 4 and y by 18, truncated, x held to +-15 and y to +-3.
// So x does nothing up to 18 from the centre and reaches its 15th and last step at 75; y
// steps at 33 and reaches its third at 69.
#include "input_defaults.h"

namespace {

const bool installed = [] {
    input_set_default("stick_x_min", 19.0f);
    input_set_default("stick_x_max", 75.0f);
    input_set_default("stick_y_min", 33.0f);
    input_set_default("stick_y_max", 69.0f);
    return true;
}();

}  // namespace
