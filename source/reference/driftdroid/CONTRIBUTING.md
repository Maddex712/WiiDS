# Contributing to DriftDroid

Thanks for wanting to help! A few ground rules.

## Where a change belongs

DriftDroid is the Android fork of
[WiiCompiled](https://github.com/patchzyy/Wiicompiled). Sending a change to the
right repository saves everyone a round trip:

- **Here**: the Android app shell (`android/`), touch controls, the settings
  sidebar, the launcher, thermal and renderer work specific to phones, and
  anything under `aurora-main/` that only matters on Android or OpenGL ES.
- **Upstream WiiCompiled**: the recompiler itself, game behavior, HLE, and
  anything portable. Portable fixes are welcome here too, but they belong
  upstream first so every port gets them.

If you aren't sure, open an issue and ask before writing the code.

## The short version

- Code is judged on quality, not where it came from.
- You must understand and be able to explain every line you submit.
- PR descriptions and responses must be written by you, **not** generated.
- Accuracy is the bar for anything touching game behavior.

## Code quality

We don't care how your code came into existence. What we care about is whether
it meets or improves the project's patterns and standards, and the only way we
measure that is by reading it.

Low-quality code won't be merged, regardless of origin. AI slop and human slop
get the same treatment.

## If you use AI tools

That's ok, but rules apply:

1. **You must be able to explain your changes.** If a reviewer asks why a line
   exists or what a function does and you can't answer, the PR will be closed.
   "The AI wrote it" is not an explanation.
2. **Write your own PR description.** The description exists so reviewers know
   what you changed and why, in your words. Generated descriptions tend to
   describe everything and explain nothing, and they will get your PR closed.

## Pull requests

- Keep PRs focused. Try to keep it at 1 change per PR. Small PRs get reviewed
  fast.
- Explain **what** and **why**. Reference the issue if there is one.
- For anything affecting game behavior: identical behavior to real hardware is
  the goal. Be prepared to show your change doesn't diverge from the original
  game (hardware comparison, logs, whatever fits).
- Review feedback is about the code, not about you ;).

## Testing on a device

This is a phone port, so "it builds" is not the same as "it works". Anything
touching input, rendering, audio, or the lifecycle needs a run on real
hardware before it's submitted. Say which device and Android version you tested
on, and note whether it was a debug or release build - debug builds carry
Android's own runtime checks and are noticeably slower, so they're no basis for
a performance claim.

Useful while testing:

```bash
# the runtime's own log (debug builds only - release isn't debuggable)
adb shell run-as com.driftdroid.android cat files/WiiCompiled/android_runtime_attempt.log
```

Performance claims should come with numbers and the conditions behind them. A
phone that has been running for ten minutes is thermally throttled and will
score differently from a cold one, and some devices have a boost or performance
mode that silently changes the answer.

## Supporting a new Retro Rewind release

Retro Rewind ships faster than this app does, and most of the time nothing needs
doing: the app reads the official release list from `update.rwfc.net` at runtime
and decides what it can run from the SHA-256 of `Binaries/Code.pul`, not from a
version number. A release that only changes tracks, characters or other assets
is picked up on its own, with no code change and no app update.

A release that changes `Code.pul` is different. That file is the mod's PowerPC
game logic, and because everything here is compiled to native code ahead of time
rather than emulated, it has to be translated on a desktop and compiled into the
app - a phone cannot pick it up at runtime. The app detects this case from the
release's small delta archive before downloading anything large, refuses it, and
leaves the player's working install alone.

To add support for such a release:

1. Re-translate against the new `Code.pul` (see the translator's `retro_rewind`
   build shards under `generated/build_shards`) and rebuild. If the new release
   patches different base-game functions, `translate-mod` refuses and says so:
   re-translate the base game first (`translate-recursive`, then
   `emit-base-manifest`), then `translate-mod`, then `emit-build-shards`.
2. Put the new `Code.pul` SHA-256 in `SUPPORTED_CODE_PUL_SHA256` in
   `android/app/src/main/kotlin/com/driftdroid/android/RetroRewindRelease.kt`,
   with the version in a trailing comment, in place of the old one: one build
   carries the translated code of one `Code.pul`. Also add the release to
   `KNOWN_CODE_PUL_SHA256`, so the app can judge it without downloading its
   update archive. An install left on the old code is updated by the app before
   it is launched.
3. Move the fallback release used when the device is offline: update `VERSION`,
   `ARCHIVE_URL`, `ARCHIVE_BYTES` and `ARCHIVE_SHA256` in the same file to the
   new full archive. Not every release has one (6.13.1 only exists as an update
   on top of 6.13.0); the app installs the newest full archive and applies the
   updates above it.
4. Check hand-written code that depends on the mod's own code addresses
   (`runtime/src/hle/low_detail_karts.cpp`).
5. Test both a fresh install and "Check for updates" on an existing install.

Never relax the hash check to make a release install. It exists because an
unsupported `Code.pul` desyncs from the recompiled logic at runtime instead of
failing cleanly, which is far worse for the player than being told to wait.

## Bug reports

Use the issue templates, and see the FAQ in the [README](README.md). The most
useful report names the device, the Android version, the DriftDroid version,
whether it's Original or Retro Rewind, and includes the runtime log if you can
get it.

## A note on related projects

WiiCompiled, KartPad, Wheel Wizard, and other projects in this ecosystem are
developed independently and each has its **own** contribution rules, including
their own rules around AI usage. What applies here does not automatically apply
there, and vice versa. Check each project's own CONTRIBUTING file.

## Legal

- Never!!! include Nintendo code, assets, or game data in a PR, an issue, or
  anywhere else in this project. No exceptions. That includes logs or
  screenshots that embed game data, and links to game files.
- By contributing, you agree your contributions are licensed under
  [GPL v3.0](LICENSE), like the rest of the project.
