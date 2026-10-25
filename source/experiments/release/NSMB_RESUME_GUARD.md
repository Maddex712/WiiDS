# NSMB inactive-frame copy guard

The historical Thor incident is recorded in
`build/achievements-ui/HANDOFF.md`: a resume crash in `GXCopyDisp`, read address
`0x40`, losing an unsaved run. The October 24 Thor pass retrieved the historical
crash text and bounded console tail read-only. They confirm the guest function
and address but do not contain a usable native backtrace proving this guard
addresses that exact fault. Memory dumps were not retrieved.

Source investigation found a separate, demonstrable contract violation:
`BeginNextAuroraFrameWithRetry` can expire with `g_auroraFrameActive == false`.
`EnsureAuroraFrameActive` returns void. Both copy wrappers called it and then
continued into copy work without checking whether a frame actually started.
Window/surface absence during Android pause is a reason frame startup can fail.

The change checks the flag with acquire ordering immediately after the retry in
`GX__CopyDisp_8016fc38` and `GX__CopyTex_8016fd74`, returning before GPU work if
there is still no active frame. It does not advance the display frame count or
publish an XFB for that failed frame. Later calls retry normally. Both the E1
and MSMBWii runtimes receive the same change.

The actual display wrapper and texture entry preamble are compiled into a small
Windows test with controlled frame-start results. The old display wrapper fails
the inactive-frame check. The guarded source passes 24 checks for inactive,
successful retry, already-active, 5,000 failed retries and later recovery.
Both actual ARM64 libraries compile/link. Disassembly confirms an acquire load
and early conditional branch after the frame-start retry in each built library.

This proves prevention of that invalid-frame path, not the root cause or final
resolution of the historical Thor crash. Owner Home/sleep/resume gameplay tests
are still required.

## October 24 physical reproduction and actual fault

Dev12 passed title/map checks and Home/recents during World 1-1, then crashed
after sleeping and waking the paused course. All 48 readable save/settings files
still matched the fresh baseline. The new guest stack again named GXCopyDisp
and read address 0x40, so the bounded-retry guard was insufficient.

Dev14 added Android ARM64 fault-context diagnostics and retained the exact
unstripped library privately. It reproduced after three sleep/wake cycles.
Its host PC offset `0x67e9b78` resolves to
`ImGui::GetBackgroundDrawList(ImGuiViewport*)`, imgui.cpp:5023; LR `0x13184ac`
resolves to `NetplayUi::Draw`, netplay_ui.cpp:1799. Disassembly confirms a load
from the null `CurrentWindow` pointer plus 0x40. This identifies the native fault
instead of inferring it from the stale guest PC.

`settings_overlay::Draw` waited for the render worker to finish, then assumed
that the worker had begun the next ImGui frame. A surface change can finish the
worker without successful preparation. Dev15 checks the actual ImGui context,
`WithinFrameScope` and `CurrentWindow` after joining the worker, skipping the
overlay until a valid frame exists. Both E1 and MSMBWii receive the correction.

`test_imgui_overlay.py` compiles the actual production Draw entry against the
real pinned ImGui sources and executes it on Android ARM64. The old entry
segfaults (exit 139) after an actual ImGui Render; each guarded entry passes 58
checks, including repeated closed-frame calls, worker-end/worker-begin ordering,
missing context/window and successful next-frame recovery. Physical dev15
acceptance remains a separate gate.

The workbench retains a paused RetroAchievements Casual-client experiment.
`build_resume_preview.py` constructs the NSMB preview from the dev7 VI source
receipt, omits the experiment's source unit and archive, and keeps the paused
workbench unchanged. It uses the existing compiled dependencies and translated
objects; it recompiles and links real source rather than editing binary bytes.
The resulting library has no Casual-client or rcheevos exports. Java packaging
likewise restores the recorded dev7 achievement sources only in its private
build copy. The original paused dev8 APK remains unchanged and uninstalled.
