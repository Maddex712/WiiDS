// Input logging and replay.
//
// A run leaves behind a record of every controller state the game polled, keyed by the
// poll's sequence number, so that the same route can be played again without a human at
// the controls -- "get me to the boss" rather than scripting a handful of presses. With
// the virtual clock (runtime.h) the guest's timeline is a function of its own execution,
// so the Nth poll of a replay is the same instant in the game as the Nth poll of the
// recording, and the replay is exact. The log also records the run's start time, which
// the replay feeds to the RTC, so even the calendar looks the same to the game.
//
// Version 1 logs (keyed by presented frame, recorded under the host clock) still load;
// they replay approximately, as they always did.
//
// A log is a directory:
//   inputs.txt       one line per change of pad state (see the file's own header)
//   memcard_a.raw    the memory card as it was when the run started, if there was one
// A replay restores that card to a scratch copy so that the run sees the same saves and
// the real card is untouched.
#pragma once
#include "hw/pad.h"
#include <cstdint>
#include <string>

// Start recording into `dir` (created). `memcard_path` is copied in as the snapshot when
// it exists. Returns false, and records nothing, if the directory cannot be made.
bool input_log_start(const std::string& dir, const std::string& memcard_path);


// Load `dir` for replay. The card snapshot, if any, is copied to `dir`/memcard_replay.raw
// and `memcard_out` receives that path; the frontend points the EXI card at it before
// boot. Returns false if there is no inputs.txt.
bool input_replay_load(const std::string& dir, std::string& memcard_out);

// Marks where the run ended: the log's last line is otherwise the last *change*, and a
// replay would hand control back to the live pad there, which can be well before the end.
// Called on a normal exit and from the crash and check handlers, so that a replay holds
// the last state up to the instant the run stopped.
void input_log_end();

// Called by the SI layer each time the guest polls a pad, with the live state in `s`.
// While a replay is loaded and not yet past its end, overwrites `s` with the logged state
// for `chan` and returns true; past the end the live controller takes over again and this
// returns false. Either way the state the guest will see is recorded, when logging.
bool input_pad_poll(int chan, PadState& s);

// Whether a replay is in progress (loaded and not yet past its last poll).
bool input_replay_active();

// Poses the game's hooks read on the guest thread (vr::hand_pose and vr::eye_views in
// vr_game.h) are input too: a game that aims from a controller or culls to the viewer's
// head steers its simulation by them, and a replay without them parts from the recording
// as soon as they matter. Each read is counted per kind, in the order the guest makes
// them, and a change in what a read returned is logged against that count
// (`pose <kind> <read> <valid> <values...>`); a replay answers the Nth read of a kind
// with the state its log had at N, so a hook sees exactly what it saw. Only reads from
// the guest thread are logged: a frontend's own reads do not reach the game. A log
// without pose lines, or one that has run out, leaves the live poses in force.
// `valid` and the `n` floats are the live result on entry and the result to use on
// return; returns true when a replay supplied it.
bool input_pose(const char* kind, bool* valid, float* v, int n);

// The clock's catch-up jumps (clock_pace in cpu.cpp): a jump of `edges` back-edges made
// when the guest's back-edge count was `at`. Recorded so that a replay makes the same
// jump at the same count, which keeps it exact: the guest then reads the same time base
// at every instruction. A log without any (recorded before version 3, or on a host that
// kept up) replays without any.
void input_log_jump(uint64_t at, uint64_t edges);
// The count the next recorded jump is due at, UINT64_MAX when none: the clock polls there.
uint64_t input_replay_next_jump_at();
// If a recorded jump is due at `count` (or was passed), takes it and returns its size.
uint64_t input_replay_take_jump(uint64_t count);
