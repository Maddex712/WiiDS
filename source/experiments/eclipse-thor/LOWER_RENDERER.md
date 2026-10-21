# Eclipse lower-screen rendering comparison

The Eclipse-only companion retains Claude's Sunshine artwork, layout, drawing helpers and
live counters. It also handles Eclipse's settings/character-select contexts and uses a
truthful fallback for custom stages instead of borrowing retail stage artwork.

An initial hardware Canvas run developed an Android input ANR. The debug Java stack
observer found the main thread in `HardwareRenderer.nSyncAndDrawFrame` via
`ThreadedRenderer.draw`; it did not establish a controller-input or snapshot-reader hang.
The native renderer also logged a geometry-shader compilation failure. That message alone
does not establish the cause of the Android UI stall.

The controlled comparison changed only the lower `Board` to `LAYER_TYPE_SOFTWARE` before
attachment and cleared the lower Presentation's hardware flag. The top SDL/GLES window
and all native libraries stayed unchanged. Android reported `canvasHardware=false` and
`windowHardware=true`: the artwork uses software Canvas, but this is not a claim that
HWUI was removed from the whole window or application.

The comparison reached Eclipse's Tutorial with the companion visible, physical buttons,
dialogue confirmation, Start and stick movement responsive. No ANR was observed during
the bounded run. A sampled stack in `buildSky` initially raised a separate cache-cost
question. PID22435's actual log puts that sample at 20 seconds and shows an idle main
thread at 30 seconds; it does not show a 30-second cache build.

The following PID23094 run measured the actual cache misses:

| Cached artwork | Build duration |
| --- | ---: |
| Fallback sky | 58.96 ms |
| Brochure card | 5.35 ms |

These measurements do not justify a background cache rewrite. The software Board mode
is retained only for Eclipse because it avoided the observed hardware Canvas stall in
this comparison. The exact native GPU/driver cause is not established. This is bounded
Tutorial evidence, not an all-stage, sustained-performance or original-save-persistence
claim. Final non-debuggable release validation is recorded separately by the coordinator.

Private raw evidence and receipts are under `build/eclipse-thor/lower-hwui-comparison/`.
`Prepare-App.py` reproduces the companion adaptation. Debug-only cache-duration logs can
remain; the startup stack observer is removed from the final host. Its source template
is archived in `diagnostics/ThorUiWatchdog.java`, outside all application source sets.

Android's supported per-view software layer and the distinction between Canvas and
window acceleration are documented in
[Hardware acceleration](https://developer.android.com/develop/ui/views/graphics/hardware-accel).
