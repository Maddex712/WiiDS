// Force feedback on the wheel that controls.txt steers with (input_map.h).
//
// Every force that depends on where the wheel is turned is an SDL spring, which the wheel's
// own firmware runs and which always pulls towards its centre point, so no wheel's sign
// convention can turn one into a push outwards:
//
//   - The stop at full lock. The game's steering is at full lock well before a sim wheel's
//     own end (the stick_x binding's gain), so past that point a spring takes over. A
//     spring's stiffness is capped at full force over half the wheel's travel, far too soft
//     for a stop, so while the wheel is past the lock the spring's centre is moved, every
//     update, to where it gives `stop_stiffness` times that: full force a few degrees in.
//   - Centring, from light at rest to firmer with speed and sideways load, and lighter in a
//     slide, from the game's telemetry::Motion when the game publishes one.
//   - A damper, which steadies a direct-drive wheel, and is far heavier past the lock: the
//     stop's spring holds between updates, and the damper takes out the kick that leaves.
//
// The rest have no position, and a sign that does not matter: the GameCube pad's rumble
// motor as a vibration, and the road -- the bumps under the front wheels, a knock when the
// vehicle is struck -- as short kicks of a constant force.
#pragma once

struct _SDL_Joystick;

struct FfbSettings {
    bool on = true;
    float gain = 0.4f;            // everything but the stop, as a fraction of the wheel's strongest force
    // The stop, not scaled by gain, so that turning the rest down leaves it a stop: its
    // strongest force, its stiffness against a spring's most (see above), and the damper
    // past the lock, which keeps it from kicking back.
    float stop = 0.8f;
    float stop_stiffness = 40.0f;
    float stop_damper = 0.6f;
    float centre_rest = 0.1f;     // centring standing still, and in menus
    float centre = 0.35f;         // centring at speed_kmh
    float speed_kmh = 100.0f;     // the speed where centring reaches `centre`
    float load = 0.3f;            // centring added per g of sideways acceleration
    float slide = 0.6f;           // how much of the centring a full slide takes away
    float damper = 0.1f;
    float road = 0.3f;
    float jolt = 0.5f;
    float rumble = 0.3f;
};

void ffb_configure(const FfbSettings& s);
// The wheel, null when there is none: its joystick, the axis it steers with, and how far
// along that axis, 0 to 1 from the centre, the game's steering reaches full lock.
void ffb_attach(_SDL_Joystick* j, int axis, float lock);
// Once a frame or more often, on the thread that pumps SDL's events. `motor`: the pad's
// rumble motor is on.
void ffb_update(bool motor);
void ffb_shutdown();
