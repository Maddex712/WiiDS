#pragma once

namespace aurora::input::switch_pad {

// Exposes the Switch's controllers as SDL virtual gamepads, one per player (1-4). Call after
// SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD.
void attach() noexcept;
void detach() noexcept;
// Adds and removes the gamepads of players 2-4 as their controllers connect and disconnect. Call
// from the thread that pumps SDL events, once per poll; it only looks a few times a second.
void poll_connections() noexcept;

} // namespace aurora::input::switch_pad
