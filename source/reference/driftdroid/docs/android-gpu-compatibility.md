# Android: GPU compatibility and custom drivers

Which phone GPUs the Android port runs on, what it does automatically for the ones with known
driver bugs, and how to try a different GPU driver on a Snapdragon phone.

## How the game picks a renderer

The game renders through [Dawn](https://dawn.googlesource.com/dawn) on top of the phone's
Vulkan driver. With **Graphics... -> Automatic** (the default) it tries, in order:

1. **Vulkan, full feature level.** The normal case on most phones from the last several years.
2. **Vulkan, Compatibility level.** For budget GPUs whose Vulkan driver works but stops short of
   WebGPU's full limits (for example 4096-pixel textures or 4 color attachments). Still much
   faster than OpenGL ES.
3. **OpenGL ES.** For phones with no usable Vulkan driver at all. It runs, but slower: OpenGL lets
   only one thread talk to the GPU, so shaders compile on the render thread.

If the renderer you chose could not start and another one was used, a notice appears when the game
starts. The in-game settings show the active **Graphics API** and **GPU** at the bottom.

When Vulkan is turned down, the reason (a missing feature, a limit that is too low) is written to
the game's log, and **Export Diagnostics** on the launcher's main screen includes it.

## GPU families

| GPU | Status | What the game does |
| --- | --- | --- |
| Adreno 6xx / 7xx (most Snapdragon phones) | Works | Nothing special. Custom drivers available. |
| Adreno 750 (Snapdragon 8 Gen 3, Galaxy S24) | Characters reported missing or silver | Character vertex repack and constant matrix lookup turn on by themselves. Not yet confirmed on this GPU. |
| Adreno 8xx (Snapdragon 8 Elite, Galaxy S25/S26, Z Fold 8) | Stretched meshes and textures (issue #6) | Vertex repack turns on by itself: every draw in the original game, characters in Retro Rewind. Not yet confirmed on this GPU. A custom driver is the other route. |
| Adreno 5xx and older | Should work | No custom drivers exist for these. |
| PowerVR BXM (Moto G54 and similar) | Older drivers had no Vulkan (issue #3) | Accepted since 1.3.0-gpu-test1 (driver reports 64 inter-stage components; the stock graphics library wanted 72). |
| PowerVR GE8320 / GE8322, Mali-T8xx | Vulkan below the full level | Uses Vulkan at the Compatibility level. |
| Mali-G31, G51, G52, G57, G72 | Should work | Nothing special. |
| Samsung Xclipse, other Mali-G | Not checked | |

"Should work" means the GPU's published Vulkan capabilities pass every check the graphics library
makes; it has not necessarily been played on.

### Settings that override the automatic choice

In the in-game settings sidebar:

- **Fix stretched graphics (Snapdragon):** Automatic, Off, Characters only, Everything (slower).
  Takes effect the next time the game starts.
- **Fix missing characters (some Adreno GPUs):** forces the constant matrix lookup on or off.
  Once you change it, your choice wins over the automatic one.

## Custom GPU drivers (Snapdragon only)

Many Snapdragon phones ship Qualcomm Vulkan drivers that are years old, and several of the
rendering faults players see are bugs in those drivers rather than in the game. Like other Android
emulators, the launcher can load a different driver in the AdrenoTools package format: a `.zip`
holding `meta.json` and the driver `.so`. This only exists for Adreno GPUs; Mali, PowerVR and
Xclipse drivers cannot be replaced on Android.

DriftDroid does not ship any drivers. Packages are published by their authors, for example
[K11MCH1/AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers/releases):

| Package | For |
| --- | --- |
| Turnip (Mesa), e.g. `Turnip_v26.0.0_R8.zip` | Adreno 6xx / 7xx. Open-source driver from the Mesa project. |
| `turnip_a8xx.zip` | Adreno 8xx. Early beta according to its author. |
| Qualcomm driver packages, e.g. `Qualcomm_849_adpkg.zip` | Newer official Qualcomm drivers repackaged, mainly for Adreno 7xx / 8xx. |

To install and use one:

1. Download the `.zip` on the phone.
2. Launcher: **Graphics...** -> **GPU driver...** -> **Install .zip...**, pick the file.
3. Select the driver in the list and tap **Save**.
4. Keep the graphics API on **Vulkan** (or Automatic) and start the game.
5. Check the bottom of the in-game settings: with Turnip loaded it reads `GPU: Turnip Adreno (TM) ...`.

To go back: **GPU driver...** -> **System driver (default)** -> **Save**.

A driver that fails to load never stops the game; it falls back to the phone's own driver.
Switching drivers throws away the compiled shader cache, so expect a few minutes of stutter after
the switch while it is rebuilt.

A different driver is not expected to make a phone that already renders correctly faster: the
port is limited by the CPU, not the GPU (see [android-performance.md](android-performance.md)).

## Reporting a graphics problem

Open an issue with the phone model, the GPU (bottom of the in-game settings), the graphics API,
and whether a custom driver was selected. A screenshot and the zip from **Export Diagnostics**
answer most questions in one go.

## For developers

- Release builds use Dawn built from source with the patches in `aurora-main/cmake/patches/`
  (`dawn-android-vulkan-library-override.patch`, `dawn-android-imgtec-interstage-floor.patch`);
  see the build traps in [android-performance.md](android-performance.md).
- The Adreno model is parsed from the adapter name (`webgpu::g_adapterAdrenoModel`); workarounds
  key on it. Turnip reports the same chip name with a `Turnip` prefix and gets model 0, so the
  Qualcomm-driver workarounds stay off under Turnip.
- The Vulkan fallback order lives in `webgpu::initialize` (`aurora-main/lib/webgpu/gpu.cpp`). A
  Compatibility-level device requests the adapter's vertex-stage storage buffer limits, because
  every GX vertex shader reads two storage buffers.
- To predict whether a GPU gets an adapter without the phone, compare its report on
  [vulkan.gpuinfo.org](https://vulkan.gpuinfo.org) against the checks in Dawn's
  `src/dawn/native/vulkan/PhysicalDeviceVk.cpp` (features, `InitializeSupportedLimitsInternal`).
- Open: on OpenGL ES a PowerVR phone showed no 3D models and no menu backgrounds. Suspected cause
  is a driver reporting zero vertex-shader storage blocks, which GX vertex shaders need; not
  confirmed.
