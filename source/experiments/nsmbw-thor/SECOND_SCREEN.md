# NSMB Wii lower screen

The E1 game process now owns a passive Presentation on Thor's internal Screen-2.
The upper game, original HUD and Wii mechanics are unchanged. Claude's launcher
source and installed launcher APK were not modified in this pass.

Valid course snapshots show displayed world/course, player one's character,
lives and coins, and the original remaining timer. Special course IDs use a
conservative world-only label. Title/demo/menu/map states clear course counters.
There is no fabricated progress, reserve item, star-coin state or touch input.

JNI copies an immutable 12-integer snapshot captured on the guest thread. The
UI polls at 10Hz and never reads guest memory; stalled snapshots clear after
1.5s. Display discovery accepts Screen-2 only. Its window cannot receive focus
or touches. Pause/destroy remove polling, unregister callbacks and dismiss the
Presentation. Original font/art decoding and SHA-256 checks run off the UI thread.
Native font bearings, advances, transparency and art colors are preserved.
The dark background and independent placement are presentation adaptations.
See NSMB_LOWER_ASSETS.md for extraction/staging.

## Device evidence and limits

Release fce979ade38b9b42fbf5e02a1f233a9a26589d13486b6161c84697d46ef6a5d9
was installed, device-hash matched and non-debuggable. PID23897 loaded the
original artwork on display4. Concurrent real World1-1 captures showed matching
upper/lower lives5, coins12 and time458; live coin/timer changes were observed.
41 actual-source ARM64 snapshot checks passed. Independent Java lifecycle,
resource and JNI review found no concrete blocker.

The game later invoked original __DVDShowDiskBcaErrorMessage -> OSFatal and the
process ended. The shared launcher became foreground. No further controls or
relaunch were sent after human play was observed. The source's delayed BCA error
alarm path strongly suggests metadata-content rejection; unsupported/negative
DI completion takes a different immediate fatal path. The log has no individual
DI completion trace, so this is an inference, not a per-request proof. No check,
metadata file or disc backend was changed in this pass. Obtain an independently
verified metadata dump from the original disc to resolve content rejection;
do not fabricate a sidecar or suppress the original check.

Private receipts/captures/logs are in build/nsmbw-thor/e1/second-screen.
before/installed.apk is the prior APK. Full-profile tar attempts are incomplete
because remote permission errors mixed into binary stdout; do not use them as
full restores. Direct banner.bin/wiimj2d.sav backups were subsequently copied and
hash verified; the source save timestamp remained before this test. No source
saves, configuration or game data were deleted or overwritten. Exact backup
timing and limits are in before/backup.json.

Complete-level stability, multiplayer, special courses, Home/resume, display
reconnect and ordinary Main Menu return are not validated on this APK. No audio,
haptic, long-session performance or all-course claim is made.

## Tools

Rebuild the selected E1 native checkout with Configure-AndroidNative.ps1 after
native changes, then Package-Nsmbw.ps1 -DiscRevision E1. Refresh the complete E1
source-only patch with export_source_patches.py --only-clean-e1, preserving
Claude's launcher work. Do not also apply the older E1 preparation patch.

Capture-SecondScreen.py NAME --serial SERIAL captures both physical panels
concurrently when the NSMB game is foreground and a current-process HUD
attachment has been logged. It also stores the scoped HUD state log.
Extracted art, screenshots, generated game source, native libraries, APKs and
save backups remain private; do not publish them.
