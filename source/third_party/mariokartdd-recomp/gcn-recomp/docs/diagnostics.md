# Diagnostics and measurement

How to drive a game without a controller, record and replay a run, measure the port, and
find out which guest code or which draw is responsible for something. Environment variables
are `GCN_*`; none of this is needed to play.

## Input logging and replay

Every run of the desktop frontend records the controller state the guest polled, keyed by
the poll's sequence number, into `saves/inputs/<timestamp>/inputs.txt`, with a snapshot of
the memory card beside it. The path is printed at startup. `--no-input-log` turns it off;
`--input-log=<dir>` chooses the directory.

`--replay=<dir>` plays a log back: the guest reads the logged controller instead of the live
one until the log runs out, then the live controller takes over. The run uses a scratch copy
of the logged card (`memcard_replay.raw` in the log directory), so the real card is never
touched and the replay sees the same saves the original did. A replay is still recorded as a
new log, so a route can be extended by replaying it and then playing on.

A replay is exact. Guest time is virtual (see "The clock" below): it is a function of what
the guest executes, not of the host's clock, so the Nth pad poll of the replay is the same
instant in the game as the Nth poll of the recording, every timed event (retrace, DVD
completion, decrementer) lands on the same instruction, and the RTC is started from the
recording's start time, which the log carries. A replay therefore reproduces the recording
bit for bit, including a crash at its end, unless the runtime itself has a nondeterministic
path -- which is then the bug to find. `--fast` runs a replay as fast as the host allows;
the game still sees 60 Hz.

Logs recorded before the virtual clock (version 1, keyed by presented frame) still load and
replay approximately, as they always did.

Pad polls are the SI's own, on a 120 Hz timer of virtual time, not the game's reads, so a
log keyed by them replays at the same instants on a game patched to run at a different
frame rate (`GCN_CPU_SCALE`, below): the inputs land at the same virtual times, and what
differs from there is the game's own doing.

The log format is text: a header (version, game, start time and its epoch), then one line
per change of a channel's state --
`poll frame chan connected buttons stick_x stick_y cstick_x cstick_y trig_l trig_r`, in
hex -- the clock's catch-up jumps where they happened (`jump <back-edge count> <edges>`,
decimal; see "The clock" below), and, when the run ended in a way the runtime saw (a
normal exit, a fault, a failed guest check or an interrupt), a final `# end <poll> <frame>`. A replay holds the last
state up to that end before handing over to the live pad; without the marker it hands over
at the last change, which can be well before where the run stopped, so a log that lacks
one (a run killed from outside) can be given one by hand. Lines can be edited or written
by hand; `frame` is for the reader.

A headset's poses are input too. A game hook that reads the controllers or the eyes on
the guest thread (`vr::hand_pose`, `vr::eye_views`) and steers the game by them -- aiming
a weapon from a hand, culling to where the viewer is looking -- makes the simulation
depend on them, and a replay without them parts from its recording as soon as that
matters: a session that fought its way through a room replays into standing still and
dying. So every such read is counted per kind (`hand0`, `hand1`, `eyes`) in the order the
guest makes them, and a change in what a read returned is logged against that count
(`pose <kind> <read> <valid> <values...>`, floats printed to round-trip exactly); a replay
answers the Nth read of a kind with the state its log had at read N, so the hook sees
exactly what it saw, in the harness as much as in the app. Reads from a frontend's own
threads are not logged, since they never reach the game. A log without pose lines (version
3 or earlier) or one that has run out replays with the live poses, as before. An eyes line
is over 300 characters; until 2026-10-15 the reader took lines of 256, cut every one, and
replayed the eyes as live without a word (the hands, shorter, were fine). It takes 2 KB
now and refuses a log with a longer line rather than replaying it half-read. Until the
same day a pose logged as lost (`pose eyes 17116 0`, no values) was not replayed at all: the
replay answered only records with a full set of values, so every stretch of lost tracking
went to the live pose -- in the harness its stand-in eyes, tracked and level -- and an hour's
session, with 123 such stretches, parted from its recording within minutes. A lost pose now
replays as lost.

A replay that parts from its recording says so, once: `[input] the replay has parted from its
recording: <what> at poll <n> (frame <f>), recorded between polls <a> and <b>`. Each jump and
each pose record carries the polls of the pad lines either side of it in the log, so when the
replay makes a recorded jump, or answers a pose read with the record logged at that read, at a
poll outside that span, the guest has done a different amount of work, or read the poses a
different number of times, than it did when recording. Before it the only sign was a
replay that went somewhere its player never went, minutes after the cause. The usual causes
are the game's own: a hook steering the simulation by something a replay does not
reproduce (the render thread's progress, say), or a diagnostic that reads a pose itself, so
that a run recorded with it on replays only with it on.

## Scripted input

`GCN_INPUT="frame:BUTTON:duration,..."` presses buttons at given submitted-frame counts; `+`
joins buttons (`A+B`), and `SL SR SU SD` push the control stick all the way, or as far as a
number after them says (`SR19`, 19 of 127). Presses are counted in
submitted frames, not wall-clock: display copies the guest has issued. For most games that
is one per presented frame, but a game that renders its frame in parts (graphics.md, "Frames
rendered in parts") issues several, so its count runs several times ahead of the frame
numbers the dumps carry; `GCN_INPUT_LOG=1` shows the count. It prints each press as it fires. A
scripted press is recorded in the input log like any other, so a script is one way to make
a log.

## Frame dumps

`--hidden --dump-dir=DIR --dump-every=N` runs with the window hidden and writes every Nth
presented frame as a PNG. `GCN_DUMP_RANGE=a-b` restricts that to a window of frames.
`--headless` runs the guest with no renderer at all.

## Recording a movie

`--record=<file>` makes a movie of what the window shows -- in a headset (`--vr`) that is
the left eye, or the screen while the game is flat -- with exactly what went to the audio
device (`runtime/record.cpp`). The frames are read back at the source's own resolution
into a `--record-size=WxH` movie (2560x1440 by default): a picture about as tall as it is
wide, an eye, fills it, cropped about its centre (a Quest 3's eye at 1.4x is 2889x3180, so
2889x1625 of it); a wider one, a 4:3 screen, is fitted with bars. They are placed on a 60
fps timeline by the clock, repeated or skipped to keep to it whatever rate they arrive at
(the compositor runs at 72 or 90 Hz), and piped to ffmpeg, which must be on the PATH
(`GCN_FFMPEG` names another). The audio is written beside the movie from the first frame
on, and muxed in when the game quits, offset by when its first block came after the first
frame. Quitting normally matters: a killed process leaves `<file>.video.mkv` and
`<file>.audio.wav` unmuxed, and a game's reset (a relaunch) loses them.

In theater the left eye is not something the app has: the panel is a quad layer, and the
compositor draws it. The first version recorded the panel's own image, so a theater
stretch played back as a flat game filling the frame, while the viewer had seen a screen
hanging on black that moved as they did. Now, while a recording runs
(`Platform::mirror_view`), the frontend draws the quad again itself, every display frame:
the panel's latest image (copied out as it is drawn, since a released swapchain image is
the runtime's) at the layer's pose and size, through the left eye's located pose and field
of view, on black, in an eye-sized target (`compose_view` in `openxr/xr_frontend.cpp`).
The window shows the same view during a recording. What it lacks is the compositor's own
work: lens distortion, which a flat movie wants left out anyway, and the timewarp between
the predicted pose and the one at display.

The encoder is the first of `h264_nvenc`, `h264_amf` and `libx264` (superfast) that a
one-frame trial can open, or `GCN_RECORD_VCODEC`'s ffmpeg arguments as they are. A
`h264_nvenc` newer than the NVIDIA driver fails that trial (the ffmpeg of 2026-10 wants
driver 610, and Quest Link holds this machine's at 591), and AMD's encoder on a Ryzen's
own graphics, idle while the NVIDIA card drives the headset, takes it at no cost to the
game. The readback is asynchronous (pixel buffers, fenced and mapped three frames later),
and a writer thread pipes the frames; when ffmpeg falls behind, a frame is not kept and
the next one is repeated for it, so the movie keeps time. The count is printed at the end:
on a 35-second flat run through AMF with four frame buffers, 39 of 2,103 frames, hence
eight.

## The eye preview

`--eye` renders through the stereo path into an offscreen target and shows that in the
window instead of the flat frame: the eye sits where the game's camera is, looking straight
ahead with a 90 degree field, so a change to the VR renderer can be looked at on a desktop
before it goes near a headset. `--first-person` puts the eye wherever the game project's eye
hook says (`render_set_eye_hook` in `runtime/gx/render_gl.h`: the renderer asks the game, per
frame, for a view transform and the draws to leave out, and a game without a hook keeps the
chase camera). With `--dump-dir`/`--dump-every` the eye's frames are written as `eye_NNNNN.png`.
An eye frame presents nothing through the EFB, so the renderer steps its presented count
once per eye frame instead, and `GCN_DRAWLOG=<n>` names the frame `eye_<n>.png` shows (it
logged nothing in the eye preview before 2026-10-12, the count never moving).

## On a headset

A game built with `ANDROID_PACKAGE` has two device-side tools besides the app.

`<game>_egl` is the GL ES renderer on a headless EGL pbuffer, run over `adb shell` from a
directory holding the image (`game.iso` or `game.ciso`). `--dump-dir`/`--dump-every`,
`--seconds=N`, `--frames=N` and `--scale=N` work as on the desktop. `--eye` renders through
the stereo path instead, with the VR settings the headset uses (the game's defaults, then
`./vr.txt` if present): `--eyes=2` renders both eyes, `--eye-size=WxH` and `--msaa=N` match
the headset's target, `--eye-yaw=deg`/`--eye-pitch=deg` turn the head (dump at two yaws:
anything that does not move is head-locked), `--eye-fov=UP,DOWN` gives the eye a
headset's asymmetric field in degrees (45,45 by default; a Quest 3's is about 43,52, and
the HUD frame is sized from the larger, so where a 2D element lands against the world can
only be checked with the field the headset has), `--eye-pos=X,Y,Z` moves the head that far
from the game's camera in metres (a step to the side is what tells near from far: a
rotation shows no parallax), and `--first-person` with `--fp-window=a-b`
calls the game's `set_first_person` hook. `GCN_EYE_GPU=1` times the eye passes,
`GCN_EYE_MORPH=0,0.5,1` dumps the theater/stereo morph at those points beside the flat
frame, and `GCN_STEREOLOG=1` prints the game's `wants_stereo` answer whenever it changes,
and in the harness its `wants_panel_pair` answer too (`panel pair` or `panel flat`), asked
every frame even in `--eye`. A replay should be checked in `--eye`, because the game reads the
eyes' poses there as it does in a headset: without them the replay parts from the recording.
The desktop build honours `GCN_STEREOLOG` too, numbering frames as its dumps are, so a
game's stereo hook can be checked against a replayed route without a device.
`GCN_RAMSNAP=<dir>` (with `GCN_RAMSNAP_EVERY`, `GCN_RAMSNAP_RANGE=a-b`) writes the low 8 MB
of guest RAM beside the frames, for finding the state such a hook reads.
`GCN_THEATER_STEREO=<metres>` draws the flat frame as the stereo pair the headset hangs on
its panel with `theater_stereo` (see "The theater panel as a stereo pair" in `graphics.md`),
the eyes that far apart; the dumps come as `frame_NNNNN_l.png` and `_r.png`, and the
desktop window shows the right eye's. `GCN_RESCALE=<frame>:<scale>` (the harness) changes
the internal scale before that presented frame, as the app does when it moves between
stereo and theater, so the first frame after the change can be dumped.

The app has no environment of its own, so `gcn_env.txt` in its files directory
(`/sdcard/Android/data/<package>/files/`), one `KEY=VALUE` per line, is put into it before
the runtime starts; `gcn_input.txt` there holds a `GCN_INPUT` script, and `GCN_REPLAY=<dir>`
in the env file replays an input log relative to that directory. Its log is
`adb logcat -s <game>`, with a line every second: compositor and game frames, the longest
wait between game frames, the render thread's time per stereo frame, how many display frames
each game frame stayed up (`held 1:a 2:b 3:c 4+:d`; even pacing is all one column), the
eyes' GPU time and the resolution dynamic resolution drew them at (`graphics.md`, "Dynamic
resolution"), and the sound ring's underruns and lowest fill in that second. The compositor's
own `VrApi FPS=` line beside it has the clocks and the app's GPU time (`App=`). Raise the
buffer first (`adb logcat -G 64M`) and capture it as the session runs: with `GCN_FRAMETIME`
on it laps even that in minutes. The variables the env file sets are read on first use, so
they all work in the app; until 2026-10-18 `GCN_FRAMETIME`, `GCN_XFSTATS` and
`GCN_APPLYSTATS` were read during static initialisation, before the env file was applied,
and could not be turned on there at all.

Every runtime thread names itself (`top -H`, the profiles): `guest` for each of the guest's
host threads, `gx-frontend`, `gx-transform`, and on a headset `xr-frame-loop`. The OpenXR
frontend hands those roles to the runtime as thread hints (`thread_hints` in vr.txt,
`XR_KHR_android_thread_settings`); a Quest 3 accepts the main and renderer ones and refuses
the workers. Turning them off and on made no difference to a replayed session's frame rate.

`GCN_XR_INPUT_LOG=1` (either frontend) prints every button action's state on change:
*inactive* means the runtime has bound it to nothing present, which is how an API layer
eating an input shows. Each implicit layer's manifest names a `disable_environment`
variable; setting those one at a time finds the one responsible. On PC, `XR_RUNTIME_JSON`
picks a runtime for one launch and Meta's `OculusMirror.exe` shows what its compositor sends
to the headset; on either platform `GCN_XR_SRGB=0` takes a linear swapchain (see `graphics.md`,
"PC VR").
`GCN_GL_DEBUG=1` on the desktop makes a debug context and prints the driver's errors and
high- and medium-severity messages, each distinct one counted rather than repeated.
`GCN_AUDIO_DEVICE=<name>` opens that SDL output device instead of the default (the PC VR
shell picks the headset's on Meta's runtime by itself).

## Measuring the guest

`GCN_PROFILE=<file>` (Linux and Android) samples the host's program counters with a profiling
timer while `<game>_bench` or `<game>_egl` runs and writes a histogram at the end; it needs no
perf permissions, which a Quest's shell does not have. The kernel rounds the timer, so the
counts are proportions rather than milliseconds. `GCN_PROFILE_DELAY=<seconds>` starts
sampling later, to profile one stretch of a route. `tools/profile_report.py <file>
<unstripped binary> --addr2line=<llvm-addr2line>` sums it by function, overall and per
thread; with `--symbolizer=<llvm-symbolizer> --outer=1` instead it names each sample by the
outermost function of its inline chain -- the recompiled function a guest load was inlined
into, rather than `LD32`. Symbolise against the binary that was profiled: any other build
puts samples in the wrong functions. Both device tools take `GCN_REPLAY=<dir>` to drive a run
with an input log, and the harness `--fast` to run unpaced; `GCN_INPUT_LOG=<dir>` has the
harness record its own run's log, as the app does. A log's directory holds the card the run
started with (`memcard_a.raw`), and a replay uses it: copy the directory, not just the log.
With `--eye` the harness draws the EFB at the game's `stereo_scale`, as the app does in
stereo, unless `--scale` says otherwise. A game that reads its own frame back -- an id
buffer it picks a scan target from -- sees other pixels at another scale and goes another
way; a headset session replayed at scale 1 parted from its recording a few minutes in.

The GX front end runs on a thread of its own and the vertex transform on worker threads (see
"Threads" in `graphics.md`). `GCN_GX_SYNC=1` puts the front end back on the guest thread,
`GCN_XF_SYNC=1` runs the transform per draw there, and `GCN_XF_THREADS=N` sets how many
threads share the transform of a frame (2 by default).

`GCN_STALLS=1` prints, every 60 frames, the milliseconds per frame each thread spent
waiting on another at each place one can block: the guest on the front end (its queue
full, a draw-done, a draw-sync token read), the front end on the transform, the transform
on the renderer, and how many token reads were answered a frame behind
(`GCN_GX_TOKEN_LAG`, see "Threads" in `graphics.md`). A thread's frame time minus its
stalls is its own work, which is what tells the stage that sets the pace from the ones
waiting on it. `GCN_GX_TOKEN_EAGER=1` makes the guest wait for the front end at every
token, as it once did.

Which stage sets the pace is easy to misread. In an unpaced harness run the guest does not
wait at the end of a frame (`kFeMaxFrames` applies in real time only), so it runs ahead and
its stalls read zero, and a profile's share per thread (CPU time, sampled) tells how busy
each thread was only relative to the others. `GCN_SCHEDLOG=1` in the headset app prints, once a
second, each named thread's run time, its time spent runnable waiting for a core, and the
core it last ran on (`/proc/self/task/<tid>/schedstat`); run time per presented frame is
each stage's real cost there, and a thread near 16.7 ms of it a frame at 60 is the one that
sets the pace. `GCN_SWITCHLOG=1` prints, per frame, how often the baton passed between the
guest's host threads and what the handoffs cost, which neither a profile nor the stall
counters see.

To measure a change against the app's conditions without the app, run the harness with
`taskset 38` (the three cores a Quest 3 app gets) and an input log, unpaced, and sample
each thread's `utime + stime` from `/proc/<pid>/task/*/stat` every couple of seconds. The
difference across a stretch of frames is each thread's CPU per frame there. It is the
steadier figure: between identical runs a stretch's frame rate moves by 1-2 fps, its
guest milliseconds per frame by about 2%. Check that a speed change is exact by dumping
frames of the stretch (`GCN_DUMP_RANGE`) from both builds and comparing the files: they
must be byte-identical, and the replay must not report that it parted.

A replay in the app runs at its recording's pace, not its own: the recorded catch-up jumps
are made and the pacer sleeps whenever the build is faster than the one that recorded, so
its frame rate is the recording's. It still gives each thread's work per frame. For the
app's own capacity, replay unpaced (`GCN_TIMESCALE=0` in `gcn_env.txt`).

`<game>_bench` runs the game with no graphics, audio or input, unpaced, and reports how fast
the guest advances; `--warmup=N` (default 8) excludes boot from the steady-state figure. Its
last column is the guest's work per presented frame in loop back-edges, the figure the
virtual clock's tick-per-edge constant is tuned against.

## The clock

Guest time is virtual by default. Every backward branch in recompiled code counts one
back-edge, and the time base is that count times a constant (`TICKS_PER_EDGE` in
`cpu.cpp`); events are due when the count reaches them, and the SDK's idle spin
(`idle.txt` in the game's tables) jumps the count to the next event instead of iterating.
The host is held to real time by sleeping when virtual time runs ahead. So the game's view
of time depends only on what it executed, which is what makes replays exact; the cost is
that a scene heavier than a frame's budget of back-edges looks to the game like a dropped
frame, at a threshold set by the constant rather than by the real console.

When the host cannot keep up, virtual time **catches up**: once it has fallen more than
4 ms behind the wall clock, it jumps to it (`clock_pace` in `cpu.cpp`). The game's next
reading of the time base then sees the frame it really took and steps the simulation
accordingly, as it would on a console that dropped a frame; the AI DMA's blocks come due
together and are delivered at speed, so the sound carries on; the retrace is late once.
Without this the game ran at full speed in slow motion and the sound, produced a virtual
block at a time, came up short of the device's real seconds -- audible dropouts whenever a
scene was heavy. Each jump is written to the input log (`jump <count> <edges>`) at the
back-edge count it was made at, and a replay makes exactly the recorded jumps at exactly
those counts and none of its own, so a replay stays exact; a log with none replays as
before. A host more than 100 ms behind is re-anchored rather than caught up (a load, a
shader build). `GCN_CATCHUP=0` keeps the slow-motion behaviour; `GCN_CLOCKLOG` counts the
jumps and their total.

**A jump is not a dropped frame.** The guest waits in real time at the end of a frame for
the stages after it (`kFeMaxFrames`), and virtual time does not move while it waits, so a
jump follows; the frame still lands on its retrace. In a headset most of a heavy second can
be jumps -- 300-400 ms of every second at a full 60 frames, 600-870 ms in a scene running at
40-55 -- so counting a log's jump lines as lost time, as was once done here, reads a session
that ran at 59 frames a second as one at 45. The frame rate a session really ran at is its
presented frames per virtual second (`GCN_CLOCKLOG` on a replay, which reproduces virtual
time exactly), or its frame count over the wall-clock length of the session.

**The sound across a jump.** The blocks that came due during a jump are owed, and the AI
DMA delivers them as soon as the guest has queued the next buffer (`aid_start_block`,
`aid_block_done` in `hw/dsp.cpp`). They were once spaced a quarter of a block apart in
virtual time, to give the guest time to queue, which repaid three milliseconds of backlog
for every one the guest ran: once jumps passed 75% of virtual time the backlog outgrew that
and passed the 100 ms limit, past which it is thrown away, and the sound cut out for a fifth
to two fifths of every second. `GCN_CLOCKLOG` now prints, after each second's line, the
sound the DMA delivered in it (`[clock] audio N frames`), which should be the DMA's rate;
before the fix a replay of such a scene showed 18,700-29,000 frames a second of 32,000, and
after it the full rate through every second. What is left: a guest that fills its buffers
from its main loop rather than in the interrupt has not refilled when blocks come so close
together, and the DMA plays the same buffer again (as the hardware does when nothing new was
queued); in a slow scene that is about one block in ten, heard as crackle. Holding a block
back until the guest had queued the next one, measured in what the guest executed, was tried
and was worse: such a guest queues only after the DMA has moved on. `GCN_AIDWAV=<file>`
writes every block the DMA delivers (raw 16-bit stereo at the DMA's rate, little endian),
and `<file>.txt` each block's start, length, virtual time, whether it was delivered catching
up, its address and whether the guest had queued the next one; it works in the benchmark,
so a replay's sound can be checked block by block for repeats and broken joins.

The sound ring between the DMA and the device keeps a cushion (`audio.cpp`), 100 ms by
default and `GCN_AUDIO_CUSHION_MS` to change it. Running dry, it stays silent until the
whole cushion is back, so every underrun is a gap at least that long; 60 ms, as it was, did
not ride through the 40-60 ms a guest waits on a slow render stage.

`GCN_CPU_SCALE=N` (or `clock_set_cpu_scale` from a game's sources) makes the emulated CPU
N times as fast as the console's: the ticks per edge are divided by N, so the same frame of
work takes 1/N of the virtual time. A game that paces itself on the retrace and is patched
to wait for one field instead of two needs this, or the virtual clock itself makes its
frames two fields long; 2 is the most the clock resolves (one tick per edge). The scale can
change while the game runs: the clock keeps a base (ticks and count at the last change) and
counts from there at the new rate, so the time already elapsed keeps its value and the
pending limit is recounted. It did not at first -- virtual time was simply the count times
the ticks per edge -- and a switch a few minutes into a paced run rescaled all of that time:
halving it put every pending timer minutes away, doubling it put the guest minutes ahead of
the host and the pacer slept at every poll, and either way the game stood still for about as
long as it had run. An unpaced run never shows this, since its idle jumps cross any gap at
no cost, which is why switching mid-run looked safe in the harness.

`GCN_CLOCK=host` restores wall-clock time (the ticker thread polls for due events every
200 µs). `GCN_TIMESCALE=N` paces virtual time at N× real time (0: unpaced; `--fast` on
the game, and the benchmark's default). `GCN_CLOCKLOG=1` prints once per virtual second how
virtual and host time compare, the work per presented frame and the sound delivered.

`GCN_FRAMETIME=1` prints a line per frame from each thread: the guest's interval between
presents and how much of it the GX front end took, the batch's shape, and the renderer's
time to issue it (`[rt]` for the flat pass, with the texture and vertex uploads' share and
the EFB copies'; in stereo `[eye-rt]` adds, per eye, the flat pass's time and the eye's
own). A batch whose textures took more than 5 ms also gets an `[rt-tex]` line: how many
textures were deleted and in what time, and how many were uploaded, their size and time. `GCN_XFSTATS=1` prints, per frame over every 64, what the vertex transform was asked
for: vertices, how many carried a normal, how many had a lit colour or alpha channel,
light evaluations, texgens, and the texgens with a post matrix or an emboss -- which is
what decides where the transform's time goes. `GCN_APPLYSTATS=1` prints every 64th frame, per pass, how
many state applications there were and which groups of GL state each actually sent --
program, projection, viewport, TEV registers, textures, samplers, blend and so on -- which
is what a state application costs on a given game. `GCN_EYE_FULLFLAT=1` and
`GCN_EYE_KEEPDEPTH=1` undo the two stereo savings described in `graphics.md`, for
measuring them.

## Textures

`GCN_TEXLOG=1` prints a line per decoded texture: frame, id, guest address, format, size and
how many top-level texels are not black -- which tells a texture the game never wrote (all
zero) from one the shader mishandles. For an indexed texture it adds the palette entries the
texels use, decoded, and which palette variant this is. `GCN_TEXDUMP=<dir>` writes each
texture's top level as `<dir>/tex_<id>.png`, alpha forced opaque.

The texture cache checks a texture's texels once per frame, assuming a game does not rewrite
a texture between two draws of one frame. `GCN_TEXHASH_ALWAYS=1` checks on every lookup; if
an artifact goes away under it, the game is doing exactly that. (Palettes are not subject to
this: an indexed texture is decoded once per distinct palette its texels see, so a font drawn
black for a shadow and then white in the same frame gets both.)

An EFB copy lives on the GPU and is never written to RAM, so a lookup at its address returns
the copy -- until the RAM there changes. The copy's destination is fingerprinted when it is
made and checked once a frame (every lookup under `GCN_TEXHASH_ALWAYS`); when it differs, the
game has loaded something else into that memory, and the copy is dropped so the texture is
decoded from RAM. Before this a copy outlived its memory: a texture loaded where an old copy's
buffer had been was drawn as that copy -- black, or a stale screen -- for as long as it was in
use, which every lookup prolonged.

## Finding a draw or a function

- `GCN_DRAWLOG=<frame>` lists every draw in one frame with its index, its projection (type
  and the XF scales and offsets, since a frame may use several), its depth band and
  view-space box, and its textures by id and content hash (the hash is the same in every
  run; an EFB copy is starred); in stereo it also prints, per draw the eye makes, which
  layer the eye gave it (`[eyedraw]`: world, sky, the foreground or HUD bands, the HUD
  frame). `GCN_DRAW_SKIP=a-b` then drops a range of them, in the eyes too, to attribute a
  piece of the image to the draws that made it.
- `GCN_PIXEL=x,y[;x,y...]` (EFB pixels) with `GCN_DRAWLOG` reads each listed pixel back
  after every draw of that frame and prints it, colour and 24-bit depth, whenever it
  changed, with the draw's index: which draws touch a pixel and what each leaves there,
  from one frame. This is what found a shadow pass darkening a whole terrain -- the draw
  that blackened the ground was six indices long, and every probe before it had been read
  through later draws that painted over it.
- `GCN_COPYDUMP=<dir>` with `GCN_DRAWLOG` writes every texture copy made in that frame
  as it was produced, twice: its colour, and its alpha as grey (`copy_<frame>_<n>_tex<id>`).
  `GCN_COPYDUMP_SIZE=WxH` writes every copy of that size instead, in whatever frame: a
  render to texture done once, at a moment that differs from run to run, is caught
  without knowing the frame.
- `GCN_DRAWLOG_COPY=WxH` logs, like `GCN_DRAWLOG`, every batch holding a copy of that
  size, and `GCN_PIXEL` probes it too. Both forms of the log list the copies between the
  draws (`[copy] after draw n`, with the copy's texture id and format), give each draw's
  whole projection (the last two figures are the z scale and offset), mark a draw made
  with clipping off `noclip`, and print the q of the first texture coordinate.
- `GCN_COPYLOG=1` also says how many draw commands the batch holds since its last copy
  and the front end's running draw count (`draws=`, `seq=`): a copy with nothing drawn
  before it, or a count that does not move between copies, means the draws never
  reached the parser. It also prints each copy written back to RAM
  (`gx::set_copy_writeback`) as the guest thread lands it, or drops it because the game no
  longer wants it, with how many of its bytes are not zero.
- The draw log's verbose form (`GCN_DRAWLOG_VERBOSE=1`) also prints the pixel format and
  each bound map's mode register (wrap, filters, LOD bias) and image format; for an EFB
  copy, the format it was copied in and whether it is read through a palette
  (`fmt7=8(copy 10 pal4)`: a 4-bit intensity copy bound as CI4).
- `GCN_LIGHTLOG=<min verts>` prints, once per distinct lighting setup (channel controls,
  materials, ambients, light colours and attenuations; a light's position and direction
  move every frame and are not part of it), the channel and light registers of a draw of
  at least that many vertices, with the raw colours of its first vertices, how many
  distinct alphas they carry, the colour layouts and -- live, so only under
  `GCN_XF_SYNC=1` -- the colour arrays. A draw here is a front-end draw, before the batcher
  merges runs of them: a terrain of thousands of six-vertex tiles never reaches a
  threshold its merged draw-log entry would.

## Reading a TEV program

Each of these regenerates the fragment shaders (the cache keys programs by their source,
so the variants never mix with the real ones).

- `GCN_TEV_STOP=<n>` ends every program after stage n and writes out what
  `GCN_TEV_REG` selects: 0-3 the TEV registers (prev, c0, c1, c2), 4 the texel the stage
  sampled, 5 its rasterised colour, 6 its konstant, 7 and 8 the two vertex colours as the
  fragment sees them; `GCN_TEV_ALPHA=1` shows the alpha as grey. The value is written
  opaque, so a blended draw covers whatever was under it. `GCN_TEV_STOP_STAGES=<n>`
  confines the stop to programs of exactly n stages, so a later draw of as many stages
  or more does not paint its own probe over the one wanted.
- `GCN_SHOW=prev|c0|c1|c2|konst|col0|col1|stages` (with `GCN_SHOW_ALPHA=1`) replaces the
  final colour without changing control flow -- every stage, the alpha test and fog
  still run -- so it cannot perturb what it measures. `stages` paints the program's stage
  count as a grey level, n/16, which says which program drew each pixel.
  `GCN_SHOW_STAGES=<n>` confines it to programs of exactly n stages;
  `GCN_SHOW_MIN_STAGES=<n>` leaves programs of fewer stages alone, which keeps a
  one-stage full-screen blit from painting its own value over everything.
  `GCN_SHOW_SPLIT=<x>` shows the value only right of EFB column x (in the target's own
  pixels: 640 is the middle at `--scale=2`), so the probe and the normal image are read
  off one frame. Since GCN_SHOW writes an opaque colour too, a blended draw that touches a
  pixel last covers it; `GCN_PIXEL` is the tool when that is the question.
- `GCN_TEX_WHITE=1` makes every texture sample opaque white, leaving a draw's colour to
  its channels and konstants; `GCN_RAS_WHITE=1` does the same to the rasterised colour.
- `GCN_DUMP_SHADERS=1` prints every program's source as it is compiled, which at start-up
  is all of the cache: the one wanted is found by its stage count and the maps it samples.
- The frame a run presents at a given count is not the same moment in every run: under
  load, or flat out (`--fast`), the attract sequence drifts by hundreds of frames, and
  two runs reach "frame 10600" in different scenes. Probes that compare pixel values
  across runs are unsound; the split, the stop by stage count and `GCN_PIXEL` all exist
  so that one run answers the question.
  `GCN_DRAWLOG_VERBOSE=1` adds each draw's TEV setup (stage orders, colour and alpha
  combiners, konst selectors, the colour registers and konsts, alpha test, Z and blend
  modes), for a draw with indirect stages its indirect registers (`indmtx=` the three
  matrices, `indcs=` the coordinate scales, `iref=` the stages' maps and coordinates,
  `indcmd=` each TEV stage's indirect command) as the raw BP words, and its first vertices
  with colours and texture coordinates.
- `GCN_MTXLOG=<frame>` prints the position matrix and projection of every draw in that frame;
  `GCN_PNMLOG=a-b` lists the position matrices used over a window of frames.
- `GCN_WATCH=a,b,c` (with `GCN_MTXLOG` set) prints those guest addresses every frame;
  `GCN_PEEK=a,b` prints them in the crash dump.
- `GCN_RAM_DUMP_AT=<frame>,...` writes the guest RAM at the end of each of those submitted
  frames (the count `GCN_INPUT` keys on) to `GCN_RAM_DUMP_DIR` as `ram_<frame>.bin`, from
  the guest thread; for finding a game's variables by comparing its memory over a scripted
  run. The log line names the presented frame it was taken at too, the number the frame
  dumps carry, so a dump can be matched to what was on screen. The dump is physical RAM: a
  game that maps memory through its page tables has its paged variables wherever their
  pages happen to be, and its page table (in the dump too) says which virtual page each is.
- `GCN_TOKLOG=1` prints every PE token as it is signalled, and every draw-done (finish)
  as it is raised and acknowledged, with a note when one is raised while the last is
  still pending.
- `GCN_COPYLOG=1` logs EFB copies; `GCN_GXSTATS=1` prints per-frame GX counts.
- `--sample` reports once a second which guest functions each guest thread is in, by
  walking the guest's own stack; names come from the game's `symbols.txt`.
- Build with `-DGCN_TRACE_CALLS=ON` to keep a real guest call stack, printed by the crash
  dump (`GCN_TRACE_DEPTH` sets how many frames). Compiling with `-DGCN_WATCH` adds
  `GCN_COUNT=addr,...` (call counts per function, printed on interrupt),
  `GCN_WATCH_ADDR=<hex>` (report every change to that word; a page-mapped address is read
  with `vm_peek32` and is silent until the game maps it) and the store histogram below.
- `--log-all` enables every log category, including the DSP mailbox and DVD reads (off by
  default, since the game reads the disc every frame);
  `--log=exi,dsp` enables just those (`cpu os hw dvd gx vi si exi dsp ai thr`).
- Build with `-DGCN_GUEST_CHECKS=ON` to call the game project's consistency check (a
  function installed with `debug_set_guest_check`, which inspects the guest's own data
  structures and reports the first thing wrong) at every interrupt poll, after every
  runtime write into guest memory (DVD, ARAM and locked-cache DMA) and at every presented
  frame. The first failure prints what was wrong, where in the runtime it was noticed and
  the thread dump, then exits (`GCN_GUEST_CHECK_CONTINUE=1` to carry on instead). Since a
  replay is exact, the way to a heap corruption is: replay the run with the checks on,
  read the corrupted word's address off the report, replay again on a `-DGCN_WATCH` build
  with `GCN_WATCH_ADDR=<that>` and the store that did it is named. A check that fires
  right after a DMA names the runtime itself.
- `GCN_GUARD=<hex address>` watches one word at the same points without any build option:
  every interrupt poll, every presented frame, and after each of the runtime's own writes
  into RAM (DVD, ARAM and locked-cache DMA, EFB copy write-back, `dcbz`). Each change is
  printed with where it was noticed, and a runtime write that covers the word is named as
  it happens. A change noticed at a poll or a frame was the guest's own store, made since
  the last check; `GCN_RAM_DUMP` at the crash and the guest's stack (back chain at r1,
  saved LR at +4) take it from there.

## Finding a game's per-frame steps

A game that steps its simulation by per-frame constants runs at double speed when paced
at twice its native frame rate; running it at that rate for real means finding every step
and halving it. The mechanism is in three parts: a static candidate list that the game
project makes from the generated C (a store of a value computed from the old value at the
same address: `x += t`, `x *= k`, `x = x*k + b`, or an integer `x += 1`), two runs of a
`-DGCN_WATCH` build that narrow it, and the recompiler's `steps.txt`, which scales what is
left.

- `GCN_STORE_HIST=<from>-<to>` (debug build) counts how often each store site in the
  recompiled code ran between those two presented frames and prints the table when the
  window closes, one `[sthist] <pc> <count> <streak>` line per site. A site that ran once
  per frame of the window is a per-frame step; one that ran two hundred times is in a loop
  over objects or vertices; one that did not run is not in this scene. The streak counts
  the frames it ran in that directly followed another frame it ran in, which tells a step
  that is only active for part of the window (a countdown runs on consecutive frames) from
  one that fires on events.
- `GCN_STORE_HIST_PCS=<file>` (hex addresses, one per line) adds, for every address one of
  those sites wrote, who else wrote it: `[stwr] <addr> <pc>:<count> ...`. That tells a
  value carried from frame to frame (its only writers are steps) from a temporary that
  something assigns afresh each frame before a step adds to it, which must not be scaled.
- `GCN_READERS=<file>` (hex guest addresses or `lo-hi` ranges, one per line) is the
  readers' side: within the same window, which code loads those addresses, printed as
  `[readers] <addr> <pc>:<count> ...`. It answers where a value whose unit the frame rate
  changes (a speed the game measures as this frame's position minus the last one's) is
  consumed, which no static search tells apart from the other structures with a float at
  the same offset.
- `steps.txt` in the game's tables lists, per instruction address, which operand (`A`, `B`
  or `C`) carries the value from the previous frame. The recompiler emits that instruction
  through `runtime/step.cpp` (`ppc.translate_step`): `x += t` becomes `x += s*t`, `x *= k`
  becomes `x *= k^s`, `x = x*k + b` keeps its fixed point and approaches it at `k^s`, and an
  integer increment is taken on every `1/s`-th frame. The game sets `s` with
  `step_set_scale` (0.5 at twice the native rate) and calls `step_frame()` once a game
  frame. A patch in `patches.txt` at the same address wins.
- A third column gives the power of `s` a site takes, 1 when absent. A game that
  integrates a body by Verlet (`x += (x - x_prev) + F`, the displacement carried whole and
  the force acting over the square of the step) is not a rate: the force's additions are
  listed with power 2 and the position step is left alone, and the speed the game then
  derives (`x - x_prev`, in units of the step) is stored with the letter `R` and power -1,
  which scales the whole result back to the game's units for everything that reads it. The
  letter `P` on an add or subtract raises operand B to the power instead of scaling it,
  for the `1 - k` that sits beside a `k * x` when a damping is split into the part kept
  and the part lost. Those sites are read out of the code, not found by the histogram,
  and the game project keeps them in a table of their own that its emitter copies in.
- `GCN_STEP_SKIP=<file>` leaves the listed sites (hex addresses or `lo-hi` ranges) unscaled
  and `GCN_STEP_ONLY=<file>` scales only those: how a site that breaks the game is bisected
  out of a list of hundreds without a rebuild.
- `GCN_STEP_POW=<file>` (lines of `<hex address> <power>`) gives the listed sites that power
  instead of the table's, for trying a reading of a site -- a force as a rate, a difference
  converted or not -- against a run at the native rate before it goes into the table.
- A body the game integrates in N passes a frame, each pass a complete step of a fixed
  size (its forces, dampings and constraint corrections all inside the pass), is not
  served by scaling the sites in the pass: that runs N passes of size s per paced frame,
  N/s a game frame, and the dampings and corrections act too often, so the body is
  stiffer and more damped than the game's. It runs as the game's own with N*s passes
  of the original size per paced frame: the game patches the count and brackets the
  loop with `step_override(1)` and `step_override(NAN)`, under which every site returns
  the scale 1. Only what runs once a paced frame outside the loop (a control law, a
  measurement over the frame) keeps the table's scaling. A writer watch on one
  accumulator over a frame (`GCN_WATCH_ADDR`) shows the passes directly: the same
  sequence of stores N times between two frame marks.

What this cannot tell apart on its own is a counter from a cursor: an integer stepped by
four was a buffer offset whose halving faulted the GX flush, and one stepped by another
word was a sum that stalled the game, so the game project's list keeps integer steps to
those that count by one. The method and what it found are in the game project's notes.

## Motion telemetry

`runtime/telemetry.h`: a game publishes its vehicle's motion once a game frame, and
`--telemetry[=host:port]` on the desktop (or `GCN_TELEMETRY`, empty for the default) sends it
as SimRacingStudio's custom-game API packet to `127.0.0.1:33001`. `GCN_TELEMETRY_LOG=1`, or
`log 1` in `telemetry.txt`, prints what is sent twice a second, plus every ease to or from rest
and every crossfade.

The motion never steps where the game did not: a motion platform moves a person. The output
eases to and from rest and crossfades across anything the game marks
`Motion::discontinuity`. It is not limited in range or rate unless a `telemetry.txt` key
asks (`max_angle`, `angle_rate`, `yaw_rate`, `max_accel`, `accel_rate`; 0, the default, is
none): a platform has its own travel and speed, and the limits, which used to be on, only
made the motion tamer than the game's. The reasons are in `publish()`. A game still has to
mark its own discontinuities and must not difference across a pause: a frozen vehicle reads
as a dead stop. Noise the game sends arrives as noise; smoothing is the game's.

`yaw` is the angle the platform itself turns to, never the vehicle's heading: a platform
turns a couple of dozen degrees at most, so yaw is always relative, and the game decides
when there is any -- the nose against the way the vehicle is moving in a slide, a spin --
from what a driver feels through the seat. There used to be a `yaw_heading` flag that took
a heading and washed it out instead (`yaw_washout_s`). It turned the platform with every
corner, which was jarring, and game after game had to be moved off it onto the slip; it is
gone, so that a new game does not start from it.

Two things more are the game's to choose. `suspension` (each wheel's compression, with
`has_suspension`) is the road texture: the springs' movement about their running mean, on
heave, pitch (front against rear) and roll (left against right), so the bumps come through
as the wheels met them; it goes into the packet's suspension fields too. A game without
suspension to read gives `roughness` (0 to 1) instead, and gets bumps of random size ringing
in a damped spring over a finer grain, growing with speed. Without either a platform sits
dead still on a straight; with filtered noise alone it felt like noise, all one note. `rpm`, `max_rpm` and `gear` go to the packet for a shaker's engine
effect: SimRacingStudio plays a recorded engine (an FMOD car-engine event, by rpm) on the
shakers' sound card, so a step in the revs is a thump. The revs ease to rest with the motion,
winding down when a race ends or pauses and back up after, rather than cutting out. `telemetry::set_default` gives a key a game's own default, which `telemetry.txt` still
overrides: a vehicle's gains, or a faster `yaw_rate` for a spin.

The packet is version 102 of SRS's "api" format. Its layout comes from SRS's own listener,
which unpacks it with Python's `struct` format `3sI50s50s50sfffifffffffffffIIII` and no
byte-order prefix, so in Python's native alignment: 236 bytes, a pad byte after the `api` tag
and two after the three 50-byte strings. Field names, in order: tag, version, game, vehicle,
location, speed (km/h), rpm, max rpm, gear, pitch, roll, yaw, lateral velocity, lateral,
vertical and longitudinal acceleration, four suspension travels, four wheel terrains. A version
103 with longitudinal velocity, steering angle, fan powers and wind direction also exists. Its
field order is not known, so it is not sent. A listener that decodes with that same format string
is the way to test without moving a rig, given SRS holds 33001: send to another port.

## The command stream

When the GX parser derails -- a draw in a vertex format the game never set, or a partial
command it waits on forever (`FIFO parser waiting on a ...-byte partial command`) -- the
question is whether the bytes it was handed are the game's. `GCN_FEED_DUMP=<path>` writes
every byte handed to the parser, in order, with a `<path>.idx` of `offset address length`
per chunk, so the stream can be decoded offline and each offset mapped back to the FIFO
address it came from. `GCN_FIFO_DUMP=<path>` writes the GP FIFO's memory, base to end, in
the FIFO dump that the interrupt and fault handlers print. `GCN_RAM_DUMP=<path>` writes the
guest RAM when `call_indirect` finds no function at its target, for seeing what the code
there is and where it came from.

## Panics and faults

A game's `OSPanic` should be listed in its `hle.txt`: the replacement prints the message and
a guest backtrace and exits, instead of spinning in `PPCHalt` looking like a hang. The report
goes to stderr, to the crash record (first line only) and to `<save dir>/panic.txt`, which is
the one to read on a device after the fact. A host fault inside guest memory prints the
guest address and every thread's registers.

`OSResetSystem` belongs in `hle.txt` too. Games return to their title by resetting the
console, after a death or a quit, and the runtime emulates no reboot. Left to the SDK, a
restart clears the exception and interrupt tables at 0x80003000 on its way to loading the
apploader, and the next interrupt calls a null handler (`call_indirect: no function at
00000000`, with an OS idle loop for its LR). The replacement prints `[reset] the game reset
the console (restart, code N)`, closes the input log and hands over to the frontend's reset
handler (`set_reset_handler`). The desktop runs itself again (not in a replay), the
headset app starts its activity again in a new process, and the harness and the benchmark
exit 0. `GCN_RESET_AFTER=<seconds>` makes a reset happen that far into guest time, for
trying a frontend's restart without playing to one.

## The guest heap

The SDK's OSAlloc keeps no record of who owns a cell, so when a game panics on a failed
`OSAllocFromHeap` the message is all there is. Two things fill that in, both in
`runtime/heap_trace.cpp`:

- Every panic report ends with the heaps walked out of guest memory: each heap's size, free
  total, number of free blocks and largest free block, and the allocated total -- which
  answers whether the heap was exhausted or fragmented. Any failed allocation prints the
  same, whether or not the game then panics.
- `GCN_HEAP=1` keeps a table of live cells keyed by the caller's return address and the one
  above it (games reach the allocator through thin wrappers), and prints it grouped by
  caller on any failed allocation or panic. `GCN_HEAP=<frames>` prints it every so many
  presented frames, to watch a trend before the crash.

Both need four one-line patches in the game's `patches.txt`, hooking `OSAllocFromHeap`'s
entry and both of its returns and `OSFreeToHeap`'s entry; the entry hook takes the address
of the SDK's `HeapArray` variable, which is where the report finds the heaps. Each line
keeps the instruction it replaces -- the first instruction of `OSAllocFromHeap` is
`mulli r0,r3,12` and `OSFreeToHeap`'s is `mflr r0`, and the returns are `blr`:

```
<OSAllocFromHeap>      heap_trace_alloc(c, <HeapArray var>); c->r[0] = (uint32_t)((int32_t)c->r[3] * 12);
<its NULL return>      heap_trace_alloc_result(c, 0); RET();
<its normal return>    heap_trace_alloc_result(c, 1); RET();
<OSFreeToHeap>         heap_trace_free(c); c->r[0] = c->lr;
```

`HeapArray` is the small-data word `OSAllocFromHeap` loads first (`lwz r3, off(r13)`), and
`r13` is set by `__start`. Without the hooks the panic report says the heaps were not traced.
