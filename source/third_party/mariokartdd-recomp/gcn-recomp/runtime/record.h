// --record=<file>: a movie of what the window shows -- the left eye in a headset, the flat
// frame otherwise -- with exactly what went to the audio device, made by ffmpeg.
//
// The frames are read back from the GL thread at their own resolution and placed on a
// steady timeline at `fps`, repeated or skipped to keep to the clock, whatever rate they
// arrive at (a headset's compositor runs at 72 or 90 Hz). They are piped to an ffmpeg
// that encodes as it goes; the audio is written beside it from the first frame on. When
// the game quits, finish() muxes the two into <file>. See docs/diagnostics.md, "Recording".
#pragma once

namespace record {

// Starts ffmpeg (found on the PATH, or GCN_FFMPEG) for a movie `w` x `h` at `fps`. False,
// with a message, if it cannot.
bool start(const char* path, int w, int h, int fps);
bool active();

// On the GL thread, with the frame in `fbo` (single-sampled; 0 is the window's back
// buffer), `w` x `h`. A picture about as tall as it is wide (an eye) fills the movie's
// frame, cropped about its centre; a wider one (a screen) is fitted, with bars.
void frame(unsigned fbo, int w, int h);

// On the way out: ends both streams and muxes the movie. Takes a moment.
void finish();

}  // namespace record
