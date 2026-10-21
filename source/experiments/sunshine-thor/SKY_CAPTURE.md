# Original Sunshine title sky capture

2026-10-21 Original Sunshine source change, installed beta.5 candidate; bounded
live QA is still in progress. Eclipse's native runtime is unchanged.

The previous title hook called synchronous `GXPC_CaptureEFB` only every fourth
title draw. At 60 draws per second this supplies at most 15 distinct sky frames,
even if Android redraws the same bitmap more often. Each read also used the
640x360 area-photo dimensions and waited on client-memory `glReadPixels`.

The existing GL proxy layer adds another important boundary: a result-returning
`glClientWaitSync` waits for the GL thread even when its GPU timeout is zero.
The new `GXPC_CaptureSkyEFB` therefore queues the complete capture operation with
`glt::post`, rather than polling/maps from the game's thread. It keeps three
pixel-pack buffers, polls fences with flags=0/timeout=0, maps only signaled
buffers, and publishes the newest completed frame. A full GPU ring, an occupied
CPU result, or a queued operation skips work and retains the last picture.
Neither side spins waiting for the other. A failed map/unmap does not replace
the last valid pixels. Ring resources are released during GX shutdown.

Contract: sky is **320x180 RGBA**, rows/orientation and opaque-alpha handling
unchanged. Area photos remain **640x360 RGBA** using the existing occasional
capture path. The title hook runs on TCardLoad screen values 3/9/10 only; these
are card/title-screen values, not TApplication app-state identifiers. Leaving title retains
the final sky image. The Eclipse native runtime is unchanged.

`Test-SkyCapture.py` compiles extracted production GX and snapshot sections into
a freestanding Windows DLL against deterministic GL/queue mocks, then executes
31 assertions. This covers bounded work, zero fence timeouts, no GPU calls before
the queued command runs, full-ring skips, newest-frame selection, failed reads,
pixel-lock contention, resource cleanup, alpha/buffer sizes and sequencing.
120 synthetic ready-GPU draws produce 118 distinct published frames after the
two-draw warm-up. This is a deterministic pipeline check, not a device FPS claim.
The whole modified native files also passed Android ARM64 syntax-only compilation
using the existing `android-native-wiids/compile_commands.json` flags.

The cache-order follow-up extracts the production `GlCache`, `glcInvalidate`,
`glcBindTexture` and `glcBindSampler` code into the queued mock. A logo using the
same texture/sampler before and after capture is rebound correctly after capture
initializes its texture; EFB and pack-buffer state are correct. This rules out the
proposed normal-path cache-binding leak in that fixture, rather than establishing
that every driver/scene issue is absent. Retail `TCardLoad::titleDraw` deliberately
turns its intermediate letter panes white before fading them; a white logo alone
does not prove GPU corruption. Unexpected live menu/input gating still requires
state evidence or a controlled same-scene comparison before another native fix.

## Current live evidence and remaining benchmark

The installed candidate's stable 120-interval lower Android draw window measures
59.899 estimated draws/s (median 16.667 ms, maximum 16.905 ms), compared with the
prior stable 120-interval window's 20.083 draws/s (median 50.025 ms). The private
receipt is `build/sunshine-polish/pacing-comparison.json`, keys
`before-lower-gfxinfo` and `title-after-gfxinfo`. These are Android UI draw
intervals, **not counts of distinct native sky frames**, upper game FPS, or a
long-duration performance benchmark. The after file's name alone does not prove
which scene supplied every frame; a distinct-sky sequence measurement is still
required for the sky delivery claim.

The receipt also contains a short 14-interval after window with a 109.295 ms maximum
and a 19-interval before window spanning a 332-second background gap. These are
not silently merged into the stable-window comparison. They prevent treating
the reported 59.899 figure as a claim that every lifecycle/transition frame is
smooth.

Actual title video now confirms bird motion in the installed candidate. A
bounded game sleep/wake check restored both screens; three Original Sunshine
save files and `settings.txt` matched their backups afterward
(`build/sunshine-polish/save-verification.json`). The complete upper life-meter/
damage animation remains unverified. These observations, the enlarged Airstrip
overview and full MARIO lettering do not establish distinct native sky delivery
cadence or long-background recovery.

Use the same title scene, resolution, frame-rate setting and thermal state for a
20-30 second before/after window. Record native sky sequence changes and maximum
arrival gaps at the Java boundary, plus upper SDL SurfaceFlinger frame intervals.
Capture actual lower-screen video to inspect bird/cloud motion and orientation.
Allow two frames to warm up. The expected supply rate is the actual title draw
rate, subject to GPU completion; 60 distinct frames is not assumed in advance.

Reject a change that significantly worsens upper-frame cadence or repeatedly
stalls lower delivery. A stable GPU should generally publish every draw; a busy
GPU should skip rather than build an old-frame backlog. Finally enter one stage
and check that its area photo still transfers 640x360 pixels and retains the
previous quality. No all-course sweep is needed for this presentation change.
