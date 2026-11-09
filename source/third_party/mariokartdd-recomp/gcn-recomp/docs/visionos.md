# The Vision Pro frontend

`visionos/` runs a game standalone on an Apple Vision Pro, with the same frontend core a
Quest runs (`openxr/xr_frontend.cpp`): the theater panel, stereo, the morph between them,
the controllers and everything a game says through `vr::GameHooks`. A game project opts
in with `VISIONOS_BUNDLE_ID` in `gcn_add_game` and builds with CMake's Xcode generator:

```
cmake -S . -B build-visionos -G Xcode -DCMAKE_SYSTEM_NAME=visionOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DGCN_APPLE_TEAM=<team id>
xcodebuild -project build-visionos/<game>.xcodeproj -scheme <game> \
    -configuration RelWithDebInfo -destination id=<udid> \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration build
xcrun devicectl device install app --device <id> build-visionos/RelWithDebInfo-xros/<game>.app
```

The game project calls `gcn_enable_languages()` at its top level first (the app is
Swift, the frontend Objective-C++).

Built for the headset itself (`-destination id=`), and through a scheme: a `-target` build
ignores `-destination`, and without it Xcode never registers the device with the team, so
the profile leaves it out and the install fails with 0xe8008012 ("This provisioning profile
cannot be installed on this device"). Xcode also needs the visionOS platform matching the
SDK before it will build for a device (`xcodebuild -downloadPlatform visionOS`).

`devicectl` copies at about 3 MB/s over Wi-Fi, so a disc image takes minutes. A copy that
is killed part way can leave the copy service wedged -- the next one connects and sends
nothing -- until `killall CoreDeviceService`. `log collect --device-udid` does not work over
Wi-Fi ("Device not configured"); `log.txt` is the log to read.

**Never pass `--remove-existing-content true` to `devicectl device copy to` an app's data
container.** It empties the whole container, not the destination named: on 2026-10-22,
meant to clear one cache folder (`Library/Caches/<bundle id>/com.apple.metal`), it deleted
everything under the container, the disc image and the memory card with its save included.
To clear a cache, copy an empty file over each file in it instead, after copying anything
worth keeping off the headset first.

## Pieces

| File | What it is |
|---|---|
| `App.swift` | The app: the immersive space whose `CompositorLayer` the game renders into, which it opens straight into, and a window that appears only when there is no usable disc image (`gcn_vos_check_image`), to copy one in from Files. |
| `visionos_main.mm` | The shell, as `android/openxr_main.cpp` is on a Quest: the log, the GL context, the order the game is brought up in. |
| `xr_runtime.mm` | A minimal OpenXR runtime: exactly the calls the core makes, over CompositorServices, ARKit and GameController. |
| `angle.mm` | GL ES, through ANGLE. |
| `audio_coreaudio.mm` | The mix out through a RemoteIO unit, under a playback audio session. |

Everything the app reads and writes is in its Documents folder, which the Files app shows
and `xcrun devicectl device copy to|from --domain-type appDataContainer
--domain-identifier <bundle id>` reaches: the disc image (any `.iso` or `.ciso`), `vr.txt`,
`gcn_env.txt` and `gcn_input.txt` as on the Quest, the memory card, the shader cache, the
input logs and `log.txt`, the last run's output. The log also goes to the unified log
(subsystem `com.gcn-recomp`).

## There is no GL: ANGLE

visionOS has no OpenGL for a native app. `OpenGLES.framework` is in the SDK, but
`EAGLContext` is marked unavailable on visionOS: it is there for iPad apps run in
compatibility. (The first build of this frontend was written against it, and failed to
compile on exactly that.) So the renderer runs on ANGLE's Metal backend, GL ES 3.0, which
is what WebKit's WebGL runs on there.

`tools/build-angle-visionos.sh` builds it from WebKit's copy of ANGLE at a pinned commit,
not Chromium's: WebKit's carries an Xcode project for every Apple platform, where
Chromium's GN build knows iOS and tvOS but not visionOS. The project expects pieces of a
full WebKit build (WebKit's `Configurations/`, the SDK additions in `WebKitLibraries/`, a
clang overlay); the script supplies them. The first configure runs it into
`<build>/angle/` (`GCN_ANGLE_DIR`), and the app embeds `libANGLE-shared.dylib`.

WebKit's build exports ANGLE's entry points under prefixed names, `EGL_Initialize` for
`eglInitialize` and `GL_DrawArrays` for `glDrawArrays`, so as not to collide with the
system's GL. `angle.mm` opens the library from the bundle and maps each standard name to
its export; the glad loader resolves GL through it.

The renderer at GL ES 3.0 (`GCN_GL_ES30`): GLSL `#version 300 es`, and the two things it
takes from 3.2 have fallbacks. `glDrawElementsBaseVertex` comes from
`GL_OES_draw_elements_base_vertex` or the `EXT` one when the context is below 3.2
(`gl_load.cpp`), and failing both, each draw's base vertex is added into its indices as
they are uploaded (`render_gl.cpp`). Pointing the vertex arrays at the base vertex
instead, the first attempt, cannot work: a base vertex is often negative. Clip distances and dual-source blending are extensions the renderer
already checks for.

## The OpenXR runtime

The core is written against OpenXR, and the alternative to emulating the part of it the
core uses was a second core. What a runtime does for the core, `xr_runtime.mm` does the
same way:

- **Frames** are a compositor thread's (`compositor_main`), as a real runtime's are its
  own process's. Every display frame it takes a `cp_frame` through its update phase,
  waits for the optimal input time and queries the drawables, then hands the render
  thread the frame's pose and views (a `Tick`: `xrWaitFrame` waits for the next one,
  `xrLocateViews` reads it). It waits for that frame's layers until 2 ms before the frame's
  rendering deadline, then composites whatever was submitted last into every drawable
  with one Metal pipeline (a textured unit quad) and presents them. `xrEndFrame` makes the
  GPU sync on the render thread and hands the layers over as a `Submission`, each image
  fixed by index; the compositor holds an image's `in_flight` count from taking the
  submission until the GPU has read it, so the render thread never draws into one on show.
  See "A render thread held up", below.
- **The head.** ARKit world tracking: the device anchor at the drawable's trackable anchor
  time. `xrLocateViews` composes it with each `cp_view`'s transform, and takes each eye's
  field from `cp_drawable_compute_projection`.
- **LOCAL** is where the head was when tracking first came up, turned to its heading and
  levelled, which is what OpenXR's LOCAL is. ARKit's origin is on the floor where the space
  opened.
- **Swapchains** are Metal textures (BGRA8), each wrapped as a GL texture through
  `EGL_ANGLE_metal_texture_client_buffer`. The compositing reads them through an sRGB view:
  the renderer writes the game's gamma-encoded colour, and decoding it as it is sampled
  into the sRGB drawable stores what was written, as an sRGB swapchain does on a Quest.
  ANGLE signals a shared event (`EGL_ANGLE_metal_shared_event_sync`) when the renderer's
  work before a frame's compositing is done, and the compositing waits for it on the GPU.
  `xrWaitSwapchainImage` waits for compositing still reading an image.
- **A projection layer** is drawn across each view at infinity. Each image keeps the device
  anchor current when it was released, and a frame showing it gives the drawable that
  anchor, so the compositor reprojects an image from the pose it was rendered at. That is
  what lets the core re-submit the last eye images on a display frame with no new game
  frame, as it does on a Quest.
- **Quad layers** (the theater panel, as a single image or a stereo pair) are drawn into
  each view at their place in LOCAL, with depth, for the same anchor.
- **Input.** Only the Touch profile's suggested bindings are taken. A spatial controller
  (PS VR2 Sense: one stick, A and B, trigger, grip, a menu button) is mapped onto it per
  hand: A and B are Touch's A and B on the right and X and Y on the left, and either menu
  button is the left menu. Its hand comes from ARKit's accessory chirality, its aim pose from
  ARKit's accessory tracking (the `aim` location, predicted for the display time). A
  gamepad is merged as a pad (`Platform::merge_input`), with its stick clicks fed to the
  thumbstick-click actions so the view toggle and the game's left click work on it too. It
  is read by position (`vr::Gamepad`) and laid out by the game's `map_gamepad` hook, or
  failing one by `vr::default_gamepad_layout`, the GameCube's buttons in their places.
- **Lifecycle.** Session states are queued as a runtime would send them. When the layer is
  invalidated (the viewer left the space) the session is lost and the core's loop ends.

## What the device showed

Each of these was a belief or an omission that the first runs on a headset overturned.

- **The scene role.** The app's preferred default scene is
  `CPSceneSessionRoleImmersiveSpaceApplication` (`Info.plist.in`). UIKit's
  `UISceneSessionRoleImmersiveSpaceApplication` looks like the same thing and is not: with
  it, SwiftUI found no scene matching the role and stopped the app at launch. Opening
  straight into the space, with the window's launch suppressed and its restoration off,
  is what keeps the window from appearing over the game; closing that window used to
  interrupt the audio session.
- **Depth 0 is "nothing here".** The compositor treats a pixel left at the cleared depth
  of a reverse-Z buffer (0) as empty, and in mixed immersion fills it with passthrough, in
  tiles: the stereo eyes, drawn at infinity, came out black, and the theater panel had a
  ragged edge of square tiles. Everything is drawn at a far but finite depth (50 m,
  `far_depth`) and cleared to it.
- **`imageRect`.** The core renders each eye into part of its swapchain image (dynamic
  resolution), and the projection layer's `subImage.imageRect` says which part. Sampling
  the whole image put each eye's picture in a quad off to one side, drifting as the
  resolution changed.
- **A frame with no drawables** (`cp_frame_query_drawables` empty: the layer is paused or
  going away) is dropped, not submitted. Ending its submission is a client error that
  ends the app.
- **The audio session** is interrupted when a window opens or closes over the space, and
  the RemoteIO unit stays stopped unless restarted: `audio_coreaudio.mm` retries the
  session and the unit every second until both run again.
- **Hands.** The system draws the wearer's hands and arms over immersive content unless
  the space asks for `.upperLimbVisibility(.hidden)`.
- **Stutter is shader builds.** Through ANGLE on Metal a program takes about 90 ms to
  build; one restored from a binary costs nothing (345 in 89 ms). Every frame of more than
  50 ms in a first playthrough held one or two builds, hence `render_set_async_shaders`
  (graphics.md, "Programs built in the background"), which left a draw out until its
  program was built. Since games ship their keys a build mid-play is rare, and a missing
  draw looked worse than the hitch, so since 2026-10-22 the frame waits again. The first
  draw of a new program and blend combination, where Metal makes a pipeline, costs a
  further 10-20 ms.
- **A start that stays black.** Twice the space opened and stayed black, with no sound:
  the layer never reached the running state, and in one case ARKit's providers sat paused.
  A relaunch (once immediately, once later) cured it; the cause is not known. The app logs
  the layer's state while it waits, and the scenes it has, into `app.txt`.

## Known limits

- **A console reset ends the app.** On a Quest the app relaunches itself into a clean
  process; visionOS gives an app no way to do that, so the viewer opens it again.
- **A projection layer has no depth**, so the compositor reprojects stale eye images for
  rotation only. The eyes' depth buffer could be handed over as the drawable's depth.

## Resolution: foveation and render quality (2026-10-22)

The belief was that with foveation off, the drawable was simply the display's size and the
eyes would be as sharp as it allowed. The log said otherwise: each view was 1888x1792,
fewer pixels than a Quest 3's eye (2064x2208), and in stereo dynamic resolution held the
eyes at 0.50-0.55 of each side besides, so a race was drawn at about 1400x1330 an eye.

- **Render quality needs foveation.** visionOS 26's render quality (the configuration's
  `maxRenderQuality`, the layer's `renderQuality`, 0 to 1) is what makes the drawables
  larger. Asked for with foveation off, the layer is refused and the app aborts as the space
  opens (`Layer configuration not supported`; the SDK's error says foveation must be on).
  So the layer is foveated now, and `render_quality` in vr.txt (default 1) sets the
  quality.
- **Only the compositing is foveated.** The game's eyes are still rendered uniformly by
  ANGLE into the runtime's own swapchain textures; the one Metal pass that draws them into
  the drawable is given the view's rasterization rate map, and the view's viewport and
  projections are already in the map's screen space. At quality 1 a view is 6261x5023 in
  screen space, stored in a 2432x2368 texture.
- **The recommendation is the texture's size**, 2432x2368, not the view's: rendered
  uniformly at the view's size an eye would be 31 megapixels. The view's size is the most
  an eye can be.
- **ANGLE's timer queries overstate the GPU.** ANGLE on Metal answers `GL_TIME_ELAPSED` by
  adding up `GPUEndTime - GPUStartTime` of each command buffer committed inside the query.
  Those spans overlap and include time the GPU spent on the compositor and the system.
  Through a whole championship of a racing game with the budget not acted on, the eyes
  read over 17 ms a frame in 107 seconds that still held 60 game frames a second, which
  real GPU time could not do. The 0.50 the eyes had been held at was that reading, not the
  GPU.

So the platform's defaults (`vos_defaults` in `visionos_main.mm`, over the game's and under
vr.txt) are `eye_scale` 1 and `gpu_budget_ms` 0. That championship ran at 60 with them, at
2432x2368 an eye and msaa 4. A real GPU burst is
therefore unprotected here: the fix for that is a measure of the eyes' own time (Metal's
counter sample buffers, or timing the eyes' command buffer alone), not the budget.

## A render thread held up (2026-10-22)

The belief had been that a frame held up by a shader build only cost a hitch. On the
device, two apps were ended by the system (`SIGKILL`, termination namespace `0x28`, no
exception) with their render thread inside Metal's compiler: one waiting for a program's
link (`finish_program`, since the frontends stopped leaving such draws out), the other
building a pipeline at a draw's first use. Every other thread was waiting on the render
thread, and in one the log's once-a-second line stopped about three seconds before the
kill. The render thread was also the one presenting frames, so no frame reached the system
for those seconds. Which limit the system applies, and what namespace 0x28 is, is not known.

So the frames are now the compositor thread's (above): a render thread held up leaves the
last images on show, reprojected to the head from the pose they were drawn at, and the log
says so when it comes back:

```
[vos] the render thread was away 448 display frames (5000 ms); its last images were shown meanwhile
```

That line is from `GCN_VOS_STALL=5000` in `gcn_env.txt`, which holds the render thread up
that long once, 20 s of frames in. Worn, the picture held still and the head kept tracking,
and the game carried on after; the compositor presented at 90 frames a second throughout.
A render thread that is merely late (past the deadline less 2 ms) now has its layers shown a
display frame later rather than the frame presented late.

## Address space (2026-10-22)

A racing game ended at its countdown with `could not start host thread 217`: a new guest
thread's host thread could not be created. Not a thread leak: 26 threads were alive. The
crash report's VM summary put the app at about 5 GB of address space besides the system's
own 385 GB guard: 1.8 GB of `__TEXT` (a game that is mostly relocatable modules, all of them
recompiled), 1 GB untagged (guest memory among it), the libraries, mapped files, malloc and
stacks. An app's address space is about that by default, and a 64 MB host stack
(`threads.cpp`, `HOST_STACK`) had nowhere to go.

The app now has `com.apple.developer.kernel.extended-virtual-addressing`
(`visionos/app.entitlements`, through `CODE_SIGN_ENTITLEMENTS`); automatic signing adds the
capability to the App ID itself. With it the same game played through races. A thread
that cannot be started now logs `pthread_create`'s reason first.

