# Save states

A save state is the game at one moment, saved to a file and loaded back in place: in a level, `SAVE_STATE`
(F5, or L1 + R1 on a controller) saves one; in the pause menu, which shows those buttons at its top right
with LOAD SAVE STATE, the same buttons open the list of states, and `LOAD_STATE` (F6) loads the newest. The
player's side is in the [README](../README.md#save-states).

## Two ways to load

Each state holds two copies of the moment.

- **Exact**: the game's whole memory. That is MEM1 (the emulated 24 or 64 MiB at `0x80000000`, where all of
  the game's heaps are), ARAM (16 MiB, the sound banks and some archives), and the game's own global
  variables. Loading it puts back everything: the level, every actor, the camera, the music where it was.
  It works only in the build that saved it, because the memory holds the build's own addresses (vtables,
  functions, globals). The game is non-PIE everywhere for this, so those addresses are the same every run.
- **Portable**: where the game was, as plain values (`PortSaveStatePortable` in
  `src/port_include/port_savestate.h`). That is the area and episode, Mario's position, facing, speed,
  health, FLUDD's water and nozzles, the camera, and the progress flags (`TFlagManager` banks 1 to 4).
  Loading it sets the flags and warps to the area as the warp menu does, then puts Mario back
  (`TMario::warpRequest`). It warps even into the area already loaded. The level's objects (a blue coin, a
  Shine) and the HUD's counters only follow the flags when a stage loads. The HUD's blue coin and Shine
  counters count up a step a frame toward their flags, so a lower flag under a running HUD had it count on
  to 1000 and crash. The old stage's HUD is synced to the new flags for the frames it still runs. Any build
  can load it, including another word size or a later version. The level's other actors start as the level
  starts.

A state that this build cannot load exactly loads the portable way; the state menu says so under the list. A
state from another game (another disc, or Super Mario Eclipse for Sunshine) does not load at all.

## When states are saved and loaded

Saving and loading happen in `ApplicationSaveStateFrame` (patch `zzzz-savestate-01-frame`), at the top of
`TApplication::gameLoop`'s frame loop, on the game's thread. That gives an exact load these guarantees.

- Nothing of a frame is under way. The two frames on the stack (`proc` and `gameLoop`) hold only loop state
  that is the same at every frame start (`nextState` is `APP_STATE_DEFAULT` there), so the game carries on
  from the restored memory as if it had always been there. That holds even from another level, or from
  the pause menu.
- The game's thread owns the emulated CPU (`platform/os/os_thread.cpp`), so no other game thread runs. They
  are all waiting in their loops (audio, the loaders, the card), and their stacks, which are host stacks,
  hold nothing between iterations.
- A load also requires the same threads waiting the same way as when the state was saved. That is each
  thread's address, state, priority, suspend count, and the queue and mutexes it waits on (the `THRD`
  chunk). The thread records live in game memory but belong to the scheduler, so a load copies the live
  ones back after restoring the memory.
- No disc read or interrupt work is outstanding (`port_dvd_busy`, `port_os_irq_work_pending`).

Saves happen only in a level, while it plays (`TMarDirector` state 4, no stage change pending). In the pause
menu `SAVE_STATE` opens the state menu instead, and loads happen there too.

The state menu (`src/savestate_menu.cpp`) is game code, built into the game library and drawn by the game:
the system font, and the HUD's art from `game_6` (the brushstroke behind its counters, the Shine, coin and
life icons, the counter digits, the pointing hand), in GX quads as the font draws its glyphs. It lists the
states on the brushstrokes beside a stack of their pictures that scrolls as the list does (the selection in
front, the states before and after it behind, above and below), one selection in one list
(`port_savestate_scan`) that up and down (or left and right) step through. The worlds' names are the game's
own art, read from the disc by the port (`port_savestate_world_art`): the file select's lettering
(`option.szs`) in the list, the episode select's banners (`select.szs`) under the pictures.
The pause menu gives it the pad while it is open (patch `zzzz-savestate-02`), and the frame loop draws it
last, over the HUD (patch `zzzz-savestate-01`). The pictures are the states' PNGs, decoded by the port
into GX textures in four reusable buffers below 4 GiB (`port_savestate_picture`).

The pause menu's hint (patch `zzzz-savestate-02`) draws `SAVE_STATE`'s binding through the system font, as
the HUD's button prompts are drawn: `port_savestate_hint` names the keys or buttons as the device in use
labels them (`port_pad_savestate_labels`), and the launcher's prompt strips carry two extra cells with their
icons (`PORT_PROMPT_SAVESTATE` in `gx_prompts.cpp`), used even with the original GameCube prompts.

After an exact load, the GX layer stamps all of MEM1 as written (`GXPC_MemoryWritten`), so every texture and
display list decoded from game memory is checked again.

An exact load plays between two fades, as long as the game's own (0.25 s, `TSMSFader`). The state is checked
first (`port_savestate_can_load_exact`, reading only its small chunks). Then the screen fades to black while
the game runs on, the memory is loaded behind it, and the fader, which the load put back as it was, is set
black again and fades in. A load that cannot be exact needs no fades of its own: it reloads the level, which
has the game's transition.

## The game's globals

Only the link map says where the linker put the game's global variables, so `tools/savestate_ranges.py`
handles them after each link (a `POST_BUILD` step of `sms`). It reads the map (GNU ld on Linux and Windows,
ld64 on macOS) and keeps the writable sections (`.data`, `.bss`, `COMMON`) that came from `libsms_game.a`
and `libsms_eclipse.a`. It merges neighbours (about 1 MB in two ranges on Linux), then writes them with the
build's fingerprint into a placeholder table in the executable, found by its magic (`kRanges` in
`platform/savestate/savestate.cpp`). The fingerprint is the SHA-256 of the executable with the table blank.

The platform layer's own globals are not in the ranges. That covers the scheduler, GX, SDL, the audio
device and the DVD tables, which describe the host and are never restored. The game's code holds no host
pointers. `std::` containers, `malloc` and host files appear only in the platform layer. The patched game
code was checked for them when save states were added; keep it that way.

## The file

`*.smsstate` files go in a `savestates` folder beside the memory card folder (`SMS_SAVE_STATE_DIR` sets
another). The format is in `platform/savestate/state_file.h`: a header and tagged chunks. Each chunk is raw
or LZ4-style compressed, in 4 MiB blocks with 64 KiB matches, and readers skip tags they do not know.

| Chunk | What |
| --- | --- |
| `JSON` | description for the launcher: time, game, stage and episode names, build fingerprint |
| `THMB` | PNG of the last frame shown, 320 pixels wide |
| `INFO` | the build: fingerprint, where the code is, memory sizes, game code and flavour |
| `PORT` | `PortSaveStatePortable` |
| `THRD` | the game threads' signatures |
| `MEM1`, `ARAM`, `GLOB` | the memory, compressed |

A 64-bit Sunshine state is about 24 MB. The game copies the memory in the frame and compresses and writes it
on a thread of its own, so saving does not stall the game. Files are written under a temporary name and
renamed.

## Limits

- **Exact loading needs the identical build.** Two players' builds of the same version are identical only if
  their compilers produce the same bytes. Builds made in different folders may differ, because paths end
  up in the binary. Those players' states still load the portable way.
- **macOS** builds link with `-no_pie` where Apple's linker still accepts it, so the code stays put. If a
  linker ignores it, the code moves on every launch and exact loads work only in the run that saved the
  state. `codeAnchor` in `INFO` catches this, and those states load the portable way. Apple's newer linker
  writes archive members as `lib.a[12](obj.o)` in its link map; `savestate_ranges.py` reads both spellings,
  and warns in the build log when it finds no game data.
- Portable loads restore Mario and the progress, not Yoshi, a ride, or what the level's actors were doing.
- Super Mario Eclipse's music streams from the disc (`platform/audio/dtk.cpp`), host-side, and is not in
  a state, so after a load the stream playing may not be the state's until the music next changes. Save
  states in Eclipse builds have not been tested yet.
- Loading a state brings its progress. Saving to the memory card afterwards saves that progress.

## Testing

`platform/savestate/tests` checks the file format and the compression (`make -C platform/savestate/tests
run`), and `tools/tests/test_savestate_ranges.py` the link map reading. Run the game headless with scripted input to check a save and a load in place. `SMS_SHOTS` captures
fields to compare, and the game draws the state menu, so they include it; `SMS_WINDOW_SHOTS` captures the
window with the port's messages too.

```sh
SMS_SAVE_DIR=$(mktemp -d)/card-a SMS_SETTINGS=/dev/null SMS_TEXTURE_PACKS=0 SMS_HEADLESS=1 SMS_AUDIO=0 \
SMS_SKIP_MOVIES=1 SMS_VI_DETERMINISTIC=1 SMS_FRAME_RATE=60 SMS_WARP=2,7,1 \
SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,SAVE_STATE@5200,STICK_LEFT@5300+300,LOAD_STATE@5800' \
SMS_SHOTS=5190,5790,5900 build/linux-64/sms /path/to/GMSE01.iso
```

Field 5900 shows Mario back where he was at 5190. `SMS_MEM_MB=80` makes the same run load the portable
way, since the memory is another size. With `SMS_AUDIO_WAV`, the audio after the load matches the audio
after the save.
