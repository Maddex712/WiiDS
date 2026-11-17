# XD rendering regression checks

Run `python run.py --cxx clang++ --out /path/to/disposable/test-output` with a C++20 compiler. GCC also works. For Zig, pass its executable with `--cxx /path/to/zig --zig`. Outputs stay in the chosen directory. No game, GPU, ROM, save, Android device or proprietary asset is needed.

The runner extracts production overlay functions and compiles them with the synthetic harnesses. It covers packet freshness/mode changes, duplicate companion-window ownership/recovery, ready-shader notification before simulated cache pruning, and presentation recovery during persistent suboptimal results/timeouts. Graphics/driver boundaries are simulated. These checks establish source control flow, not physical stutter/flicker resolution or FPS.

The release's earlier baseline and deliberately broken variants failed as intended. Physical scene, firmware, attachment and controller acceptance remains separate.
