# Double Dash / WiiDS implementation plan

- Host: Windows x64; target AYN Thor Android ARM64 with GLES 3.2 and two displays.
- Owner input: USA GM4E01 rev0; full transfer pending. Required main.dol SHA-1
  `e389e50357d4a5076ab3d38672204658f411123d` already matches readable executable.
- Route: ethannicholas/mariokartdd-recomp with gcn-recomp, static PPC-to-C build;
  doldecomp/mkdd supplies structure reference. Explicit runtime substitution.
- Worktree: `M:/WiiDS-mkdd-2026-11-01/runtime`; generated code/data stay private.
- First milestone: compile a flat Android runtime plus verified passive lower
  state and a stable renderer API for Claude. Do not expose an unbooted library case.
- Offline lab only. Own memory card/config under a fresh task-specific lab root;
  no existing WiiDS profile reused. Back up any existing lab data before reruns.
- Snapshot once on the guest frame thread; publish an owned copy. Android never
  dereferences guest pointers. Unsupported/missing/expired states show unavailable.
- Keep the original upper HUD until lower data and display readiness are proven.
- Device acceptance requires reconnecting and inspecting hardware, current build,
  foreground and displays first; preserve newer owner experiments.
- Verification: native reader boundary/corruption tests, Android compilation,
  exact input checks and real flat-render/dual-display gameplay observation.
  Component passes do not establish gameplay, audio, saves or performance.
- Design handoff: `CLAUDE_HANDOFF.md` beside this file, with API and limitations.
