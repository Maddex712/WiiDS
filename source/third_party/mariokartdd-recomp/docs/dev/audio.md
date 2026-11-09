# Audio: the JAudio DSP ucode

The game does not use AX. JSystem's audio library (JAudio) boots its own DSP program, a
synthesizer ucode of 0x1D20 bytes at 0x80343680 (gcn-recomp fingerprint `2FCDF1EC`), and
drives it with a mail protocol of its own. gcn-recomp emulates that protocol in
`runtime/hw/jaudio.cpp`; the protocol itself is described at the top of that file. This
page is how this game uses it.

## The game's side

- `fn_800A1C40` boots the ucode as a DSP task; `fn_800A1C00` (`DspHandShake`) is the task's
  init callback and spins on the CPU mailbox until the ucode's second boot mail,
  `F3551111`, arrives. Before gcn-recomp knew the ucode it treated it as AX, sent only
  `DCD10000`, and the game hung there before the Nintendo logo.
- `fn_800A2000` replaces the SDK's DSP interrupt handler: `DCD10000`/`DCD10001` run the
  task's init/resume callback, `DCD10003` (done) and `DCD10002` (yield) switch tasks,
  `DCD10004` (sync) calls `fn_80092D28`, which reads the following `F355xxxx` mail, and
  `DCD10005` (frame end) is answered with `CDD10003`.
- `fn_800A1D00` (`DSPSendCommands2`) sends a command: a count mail, then the words. Its
  wrappers: `fn_800A1AE0` (opcode `0x81`, five words: 64 voices and four buffers) and
  `fn_800A1BA0` (opcode `0x82`, render: 7 frames, volume `0x4000`, left and right output
  buffers 0x460 bytes apart -- 0x50 samples a frame). `fn_800A1A60` sends a voice-group
  sync: a zero mail, then `(group << 16) | flags`.
- The audio thread (`fn_80092B9C`) waits on a queue: message 0 comes from the AI DMA
  callback, message 1 from each `F355FF00` (a frame rendered). It counts the frames down
  and, at zero, starts the next render (`fn_80092374`); otherwise it sends the next frame's
  syncs (`fn_80091F48`).

## The synthesizer

gcn-recomp's `jaudio.cpp` renders the voices. What the game's side looks like, from the
MKDD decompilation's `JASDSPInterface.cpp` (a voice block is its `JASDsp::TChannel`):

- 64 voices, PCM8, 4-bit ADPCM (the `.aw` banks) and PCM16 (the `.ast` streams, which
  `JASAramStream` feeds through a ring in ARAM, reading back the ucode's address and
  samples-left fields at 0x70 and 0x74 and moving the loop fields to wrap the ring).
  No oscillators and no per-voice filters seen.
- The race's voices (its music stream among them) use the **auto mixer**: their channels
  are in JAudio's surround ("Dolby") mode, so the channel code calls `updateAutoMixer`
  (volume at 0x56, pan and depth at 0x50, effect send at 0x52) and never fills in the
  mixer slots, which keep stack garbage (pointers such as `800A3F80`). Mixing the slots as
  volumes clipped the race music by about 3x; the first sign was that every voice had the
  same "volumes" and none of them sent to the left bus. The title and menu music use the
  mixer slots; their levels did not change when the auto mixer went in.
- The output volume is `0x5999` (DSP level 1.4, x4 in 4.12). Voice volumes are around
  `0x0600`-`0x1900`.
- The four effect lines are a stereo echo: 64- and 32-frame delays, one tap at 0.5, lines
  0 and 2 returning to the left bus, 1 and 3 to the right. Voices send to `0x0DC0` and
  `0x0E20`.

## Status

Synthesised, not yet listened to on this machine: levels were checked from
`GCN_JAUDIO_WAV` dumps (race RMS about 7000-9000 with brief peaks; the menus about 6000).
The rear quadrants are folded into the front pair rather than matrix-encoded.
