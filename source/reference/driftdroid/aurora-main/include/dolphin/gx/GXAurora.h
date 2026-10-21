#ifndef DOLPHIN_GXAURORA_H
#define DOLPHIN_GXAURORA_H

#include <dolphin/types.h>

#if __cplusplus
extern "C" {
#endif

//
// Subcommands for GX_LOAD_AURORA.
//

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Must be followed by six f32 values: left, top, width, height, nearz, farz.
 */
#define GX_LOAD_AURORA_VIEWPORT_RENDER 0x0001

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Must be followed by four u32 values: left, top, width, height.
 */
#define GX_LOAD_AURORA_SCISSOR_RENDER 0x0002

/**
 * Aurora equivalent of CP_REG_ARRAYBASE_ID: sets the base address and size of a vertex array.
 * This command must be followed by a 64-bit memory address, 32-bit size, and 1-byte little-endian flag.
 * The index of the vertex array is given by the lowest 4 bits of the command ID,
 * e.g. writing GX_LOAD_AURORA_ARRAYBASE + 5 will set the vertex array for the sixth vertex attribute.
 * To set strides, use the normal CP_REG_ARRAYSTRIDE_ID register.
 */
#define GX_LOAD_AURORA_ARRAYBASE 0x0010

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
#define GX_LOAD_AURORA_DEBUG_GROUP_PUSH 0x0020

/**
 * Pops a previously pushed debug group.
 * Followed by nothing.
 */
#define GX_LOAD_AURORA_DEBUG_GROUP_POP 0x0021

/**
 * Sends a debug marker to the backend graphics API.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 */
#define GX_LOAD_AURORA_DEBUG_MARKER_INSERT 0x0022

#define GX_LOAD_AURORA_TEXOBJ 0x0030

#define GX_LOAD_AURORA_TLUT 0x0031

#define GX_LOAD_AURORA_DESTROY_TEXOBJ 0x0032

#define GX_LOAD_AURORA_DESTROY_TLUT 0x0033

#define GX_LOAD_AURORA_DESTROY_COPY_TEX 0x0034

#define GX_LOAD_AURORA_INVALIDATE_TEX_ALL 0x0035

/**
 * Internal: run the next closure queued by aurora::gx::fifo::run_in_stream on the GX worker, in
 * stream order. No payload.
 */
#define GX_LOAD_AURORA_RUN_DEFERRED 0x0036

/**
 * Sets the source vertex description of one attribute (GXSetSourceVtxDesc).
 * Must be followed by a u8 attribute and a u8 GXAttrType.
 */
#define GX_LOAD_AURORA_SOURCE_VTXDESC 0x0040


/*
 * Debug marker stuff
 */

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
void GXPushDebugGroup(const char* label);

/**
 * Pop a debug group previously pushed via GXPushDebugGroup().
 */
void GXPopDebugGroup();

/**
 * Sends a debug marker to the backend graphics API. These may show in debugging tools such as RenderDoc.
 */
void GXInsertDebugMarker(const char* label);

typedef enum _AuroraViewportPolicy {
  AURORA_VIEWPORT_FIT = 0,     // Preserve logical aspect in the content framebuffer
  AURORA_VIEWPORT_STRETCH = 1, // Match content framebuffer aspect to the native surface
  AURORA_VIEWPORT_NATIVE = 2,  // Use active framebuffer pixels directly
} AuroraViewportPolicy;

/**
 * Configures content framebuffer sizing and how GXSetViewport/GXSetScissor parameters are applied to rendering.
 * When AURORA_VIEWPORT_NATIVE is used, GXSetTexCopySrc/GXSetTexCopyDst will use native framebuffer resolution.
 */
void AuroraSetViewportPolicy(AuroraViewportPolicy policy);
// Opt-in workaround for GPU drivers that mis-handle dynamically indexed matrix arrays in vertex
// shaders (reported on Adreno: skinned characters render as only their eyes). Takes effect for
// pipelines created after the call.
void AuroraSetConstantMatrixIndexing(bool enabled);
// True when the active adapter is one the workaround above should be on for unless the player
// chose otherwise: the Adreno 750, whose driver drops skinned characters. Valid once the
// graphics backend is up.
bool AuroraAdapterWantsConstantMatrixIndexing(void);
// Workaround for Adreno 8xx "vertex explosion" (stretched meshes, smeared textures): the CPU
// resolves indexed vertex attributes and uploads aligned, direct vertices. The AUTO modes only
// act on Adreno 8xx. Call before the first draw; the choice is fixed for the process.
typedef enum {
  AURORA_VERTEX_REPACK_AUTO_ALL = -2,        // Adreno 8xx: every triangle draw; otherwise off
  AURORA_VERTEX_REPACK_AUTO_CHARACTERS = -1, // Adreno 8xx: skinned draws only; otherwise off
  AURORA_VERTEX_REPACK_OFF = 0,
  AURORA_VERTEX_REPACK_CHARACTERS = 1, // Draws with a per-vertex matrix index (characters)
  AURORA_VERTEX_REPACK_ALL = 2,        // Every triangle draw (also fixes tracks and menus; more CPU)
} AuroraVertexRepackMode;
void AuroraSetVertexRepackMode(AuroraVertexRepackMode mode);
// Experimental: decode GX commands on a dedicated worker thread. Call before rendering starts.
void AuroraSetThreadedGx(bool enabled);
// True while GX commands are decoded on the worker thread (AuroraSetThreadedGx took effect).
bool AuroraIsThreadedGx(void);
// Diagnostics for threaded GX: cumulative waits of the game thread on the GX worker, in total and
// per calling site (a code address). Returns how many sites were written to `sites`.
typedef struct {
  u64 site;
  u64 waits;
  u64 nanos;
} AuroraGxSyncSite;
u32 AuroraGetGxSyncStats(u64* totalWaits, u64* totalNanos, AuroraGxSyncSite* sites, u32 maxSites);

/**
 * Retrieves the current content framebuffer size.
 */
void AuroraGetRenderSize(u32* width, u32* height);

/** Retrieves the current native presentation-surface size, ignoring the GX sizing policy. */
void AuroraGetSurfaceSize(u32* width, u32* height);

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetViewport.
 */
void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetScissor.
 */
void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht);

/** Maps the logical GX viewport and scissor into a centered safe area, for fixed-aspect UI bridges. */
void GXSetViewportScissorRenderSafeArea(f32 aspect);

/** Restores the viewport and scissor from logical GX state after GXSetViewportScissorRenderSafeArea. */
void GXRestoreViewportScissorRender(void);

/** Sets the texture-copy source in active render-target coordinates, for bridges that already
 *  resolved VI/window scaling. */
void GXSetTexCopySrcRender(u16 left, u16 top, u16 wd, u16 ht);

/**
 * Create an offscreen framebuffer and switch rendering to it.
 * All subsequent GX rendering will target this framebuffer until GXRestoreFrameBuffer() is called.
 * Use GXCopyTex to resolve the offscreen content into a texture.
 */
void GXCreateFrameBuffer(u32 width, u32 height);

/**
 * Restore rendering to the main EFB framebuffer.
 * Must be called after GXCreateFrameBuffer() to resume normal rendering.
 */
void GXRestoreFrameBuffer(void);

void GXApplyBPReg(u8 reg, u32 value);

#if __cplusplus
}
#endif

#endif
