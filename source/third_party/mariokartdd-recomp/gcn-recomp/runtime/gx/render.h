// Data passed from the GX front end (guest thread) to the render back end (main thread).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gx {

// Vertex after the XF stage (except projection): view-space position, lit colors,
// generated texture coordinates.
struct GpuVertex {
    // Does nothing on purpose. The batch's vertex array is grown with resize(), which
    // value-initialises what it adds -- and for a trivial type that means zeroing all
    // 116 of these bytes immediately before the transform writes every one of them. A
    // user-provided constructor makes that default-initialisation instead, which leaves
    // them alone.
    GpuVertex() {}
    float pos[3];
    uint8_t col[2][4];
    float tex[8][3];
    // Where the vertex came from, for the draw log: its position array index (~0 for
    // direct data) and the front-end draw that produced it. Diagnostics only.
    uint32_t src_idx;
    uint32_t src_draw;
};

// Decoded texture (RGBA8, all mip levels), immutable once created.
struct TexData {
    uint32_t id;
    uint32_t width, height;
    std::vector<std::vector<uint32_t>> levels;  // RGBA8, row 0 = top
    // A hash of the texels and of the palette entries they use: the same texture gets the
    // same hash in every run, where `id` is only the order it was decoded in. What a game
    // project names a texture by (a draw filter that hides a model's materials, say).
    uint64_t hash = 0;
};

// Everything the pixel pipeline needs for a draw.
struct PixelState {
    uint32_t bp[256];
    uint32_t tev_reg[4][2];    // [reg][0=RA,1=BG] 11-bit signed components
    uint32_t tev_konst[4][2];
    float proj[7];             // XF 0x1020..0x1026 (last is type as float)
    float viewport[6];         // XF 0x101A..0x101F
    uint32_t tex_id[8];        // texture per texmap (0 = none)
    uint8_t tex_is_efb[8];     // texmap is an EFB copy (sampled from render target copy)
    // For an EFB copy: the format it was copied in (EfbCopyCmd::format), and the palette
    // the game reads it through, if it binds the copy as CI4 or CI8 (TexLookup).
    uint8_t tex_efb_fmt[8];
    uint8_t tex_pal_bits[8];
    uint32_t tex_pal[8];
    uint8_t num_texgens;
    uint8_t num_colors;
    // The draw's position matrix was the identity, so the game placed this geometry in
    // view space itself rather than in the world. Here that means the countdown light
    // rig, which is 3D but belongs to the HUD: it hangs a fixed distance in front of the
    // camera and never moves with the course. Stereo has to tell the two apart --
    // re-projected into an eye as world geometry, the rig becomes a solid object
    // standing in the water between the viewer and the racer.
    uint8_t view_space;
    // XF 0x1005 bit 0: GXSetClipMode(GX_CLIP_DISABLE). Nothing is clipped at the near and
    // far planes; depth beyond them is clamped to the buffer's range.
    uint8_t clip_disable;
    // The game's tag for the draws (set_draw_tag) where the command stream had got to
    // when this state was taken; 0 for none.
    uint8_t tag;
};

// A game's own say over what an eye draws, on top of the eye hook: called once per pixel
// state per frame, before the eye passes, with what the renderer knows of a draw that a
// game's rule might turn on. It answers EyeKeep, EyeHideDraw to leave that state's draws
// out of the eyes, EyeHideObject to leave out every draw of the frame that shares a
// position matrix with one of them -- the rest of a rigid model whose other materials
// have nothing to name them by -- or EyeFlat for a 3D draw the game composes into its 2D
// picture (a model shown in a window of the HUD, with a frustum and a viewport of its
// own): painted on the HUD frame where the flat frame has it, sorted among the other such
// draws by its own depth, rather than floating in the world at the depth of its private
// little scene. EyeScreen is for an orthographic draw over the whole picture -- a film on
// a visor or a lens, steam or rain or a flash -- which on the HUD frame is a panel whose
// edges are plain to see: it is drawn across the whole of each eye instead, fixed to the
// head and a little past the eye's field (kEyeScreenOverscan), with the HUD frame's lack
// of depth. A perspective draw so called is the same thing made another way: an effect
// the game hangs just in front of its camera, so that it lands on the same place of the
// picture whatever the camera does -- drops or splashes on a visor. In the world, with the
// head free of the camera, it would stay put as the head turned; its place on the game's
// picture is mapped onto the eye's field instead, as an orthographic one's is. EyeRestore is for an orthographic draw that puts a copy of part of the frame,
// taken earlier in it, back over the place it came from with a depth test: a cut-away,
// which erases whatever was drawn since nearer than the draw's own depth and leaves the
// scene that was behind it. In an eye the copy is of the flat view, so on the HUD frame it
// is a picture of that view hanging in the air. Instead the eye grabs its own colour where
// the game copies, draws the quad across its whole field at the depth the game gives it in
// its band, with the game's depth test, and samples the grab at each fragment's own
// position. That depth compares only with draws that keep the game's depth in their band,
// a foreground layer's (render_set_depth_layers). EyeFacing is for an orthographic panel
// the game moves about its picture by translating its projection -- a window that follows
// a pointer or the gaze. On the HUD frame, which is flat and fixed to the camera, such a
// panel away from the frame's centre is seen at a slant, a trapezoid. It is drawn instead
// on the HUD frame turned about the camera to face the point the projection puts the
// panel's origin at, with that origin at the turned frame's centre: the same place, the
// same distance, square on to the viewer. EyeLateObject is for a marker the game draws
// into its world and then partly covers with a 2D window that magnifies a copy of the
// picture around it -- a scanner's or a scope's window. On the screen the copy holds the
// marker, magnified; in an eye the window covers the marker and the copy holds the flat
// view's, off where the flat view had it. Every draw sharing a position matrix with one
// so called is left out of the flat pass that feeds the copies (as EyeHideObject leaves
// them out) and drawn by each eye after everything else in the frame, with its own depth
// test against what the eye has drawn: hidden behind what stands in front of it, over
// the window. One the game puts inside a facing panel's box (EyeFacing, the window) is
// drawn on the panel's plane, scaled about the viewer; any other at its modelled distance
// whatever its band, as EyeTrueScale below. EyeTrueScale is for a perspective draw placed in the world that the game
// puts in a near layer's depth band only so that it is drawn over everything -- a marker
// on a target, a reticle out where the aim is: the layer's scale towards the camera
// (render_set_depth_layers) would bring it near, where the eyes cannot fuse it with what
// it marks. It is drawn at its modelled distance and keeps its band's depth.
// EyeHideEverywhere leaves a draw out of the eyes as EyeHideDraw does, and out of the flat
// view too wherever a frontend has installed the filter -- a headset's panel
// (render_execute, render_execute_stereo_pair), which then asks the filter as well: for a
// stray draw that is invisible on a television and not on a panel seen up close, such as a
// pixel's quad.
// EyeFrameScreen is for an orthographic draw over the whole picture whose inside has to
// line up with the game's other 2D elements -- a filter with a hole cut for a window the
// game draws on top, or a tint inside that window. Across the eye (EyeScreen) the hole is
// where the eye's field puts it, not where the HUD frame puts the window. It is drawn on
// the HUD frame like the window, but its vertices on the picture's edge are taken far out
// along the frame's plane, so that it still covers the whole view and its edges do not
// show; a lookup of a copy of the frame is still the eye's own grab.
// Otherwise the flat view is untouched; a hidden draw's textures are kept alive. For a game that hides a model's materials (by TexData::hash, the same in
// every run) or an effect that cannot be re-projected (a screen-space refraction of part
// of the frame).
struct EyeDrawFacts {
    bool ortho;                   // an orthographic draw: a 2D element, painted on the HUD frame
    bool indirect;                // uses indirect texturing (a warp or a ripple)
    bool samples_copy;            // samples an EFB copy
    bool samples_fullscreen_copy; // ... of the whole frame, which an eye substitutes its own grab for
    float band_lo, band_hi;       // the viewport's depth band
    float vp_x, vp_y, vp_w, vp_h; // the viewport on the screen, in EFB pixels from the top left
    // For an orthographic state, what its draws in this frame cover together, in its
    // projection's clip space: -1..1 on both axes is the whole picture. A state's viewport
    // says nothing of that (an outline drawn into a whole-screen viewport is still an
    // outline). Zero for a perspective state. clip_area is the area of its triangles in the
    // same space, overlaps counted each time: 4 is the whole picture once, and a frame or a
    // pair of letterbox bars, whose box is the whole picture too, comes to a fraction of it.
    float clip_x0, clip_x1, clip_y0, clip_y1, clip_area;
    uint64_t tex_hash[8];         // each texmap's texture by content, 0 for none or a copy
};
enum EyeVerdict : int { EyeKeep = 0, EyeHideDraw = 1, EyeHideObject = 2, EyeFlat = 3, EyeScreen = 4, EyeRestore = 5, EyeFacing = 6, EyeLateObject = 7, EyeTrueScale = 8, EyeHideEverywhere = 9, EyeFrameScreen = 10 };
using EyeFilter = int (*)(const PixelState& st, const EyeDrawFacts& facts);

enum class CmdType : uint8_t { Draw, EfbCopy, Present };

struct EfbCopyCmd {
    uint32_t src_x, src_y, src_w, src_h;   // EFB rect
    uint32_t dst_w, dst_h;
    uint32_t dest;                         // destination address in RAM (the XFB, or a texture)
    uint32_t tex_id;                       // destination texture (0 when to XFB)
    uint32_t format;                       // GX copy texture format (+0x10 if intensity)
    bool to_xfb;
    bool depth;                            // copying the Z buffer
    bool clear;
    bool clear_color, clear_alpha, clear_z;
    uint32_t clear_rgba;                   // 0xRRGGBBAA
    uint32_t clear_z_value;                // 24-bit
    bool writeback;                        // write it to RAM too (set_copy_writeback in gx.h)
    uint32_t writeback_ticket;             // which, for copy_writeback_done
};

// A copy written back (EfbCopyCmd::writeback): the renderer hands over its bytes, encoded
// in the copy's texture format, and the guest thread puts them in RAM a couple of frames
// later (set_copy_writeback in gx.h). Empty bytes for a format it cannot encode.
// g_renderer_writes_back is set by a renderer that will do them -- without one, none is
// asked for.
void copy_writeback_done(uint32_t ticket, std::vector<uint8_t>&& bytes);
extern std::atomic<bool> g_renderer_writes_back;

struct Cmd {
    CmdType type;
    uint8_t prim;       // GL-ish: 0 = triangles, 1 = lines, 2 = points
    uint32_t state;     // index into Batch::states
    uint32_t first, count;  // range of Batch::indices
    // The draw's position matrix: an index into Batch::mtxs. The vertices arrive already
    // transformed by it, so nothing here needs it to draw; it is kept for what a game's
    // eye hook reads off it -- which draws are the player's, and where they stand. The
    // index only changes when the matrix does, so a run of draws sharing one shares it.
    uint32_t mtx;
    // The vertices the draw's indices refer to, Batch::verts[vfirst, vfirst + vcount),
    // and where they are in Batch::packed: the region for the state's texture coordinate
    // count, vertex index v at packed vertex v + base_vertex. See Batch::packed.
    uint32_t vfirst, vcount;
    int32_t base_vertex;
    EfbCopyCmd copy;
};

// What the GPU is given of a vertex with `ntex` texture coordinates: the leading bytes of
// a GpuVertex -- position, both colours, then that many coordinates.
constexpr uint32_t kPackedHead = 20;
inline uint32_t packed_stride(uint32_t ntex) { return kPackedHead + 12 * ntex; }
inline uint32_t draw_ntex(const PixelState& st) { return st.num_texgens > 8 ? 8 : st.num_texgens; }
static_assert(offsetof(GpuVertex, tex) == kPackedHead, "a packed vertex is a GpuVertex's leading bytes");

struct Batch {
    std::vector<Cmd> cmds;
    // Each GX vertex once, in the order the draws delivered them; the quads, strips and
    // fans the game draws with are turned into lists by the indices instead of by
    // repeating vertices, which would nearly double the buffer -- and this buffer is
    // uploaded and fetched for every pass of every frame.
    std::vector<GpuVertex> verts;
    // The same vertices as the GPU takes them. A GpuVertex has room for eight texture
    // coordinates, and a draw uses about two: uploading `verts` as it is sent a frame of a
    // hundred thousand vertices as twelve megabytes, two thirds of them unused, and the
    // GPU read them for every pass. So each draw's vertices are packed at the stride its
    // texture coordinate count needs (packed_stride), in one region per count, each region
    // starting at a multiple of its stride so that it can be addressed in vertices of that
    // size; a draw is drawn with its count's vertex layout and Cmd::base_vertex.
    // `verts` stays whole for the renderer's own CPU-side reads.
    std::vector<uint8_t> packed;
    std::vector<uint32_t> indices;
    std::vector<PixelState> states;
    // The position matrices the draws used, twelve floats each in GX's row-major 3x4
    // layout (row r is m[r*4..r*4+3], the last column the translation). See Cmd::mtx.
    std::vector<float> mtxs;
    std::vector<std::shared_ptr<TexData>> new_textures;
    // Textures the front end has let go of -- unreferenced for a while, or replaced because
    // the game rewrote them -- which the renderer deletes once this batch is drawn. The
    // front end decides alone when a texture is dead: when the renderer aged textures on
    // its own clock, one the game still referenced could go from the renderer while the
    // front end, believing it sent, never sent it again, and drew black from then on.
    std::vector<uint32_t> dead_textures;
    // How far the game turned its camera for this frame beyond where the viewer stands, as
    // the last set_view_turn the command stream reached while the batch was built: a unit
    // quaternion (x, y, z, w)
    // in the eyes' frame (x right, y up, z back). The eyes undo it -- see set_view_turn.
    float view_turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // A race frame's batch is eight megabytes or so, and a fresh one grew into that
    // from nothing every frame -- reallocating and copying the vertex array a dozen
    // times over on the guest thread, then freeing it all on the render thread. So a
    // batch that is destroyed hands its storage, capacity intact, to a pool the front
    // end builds the next one from. The hand-off is in the destructor so that no
    // frontend has to know; `pooled` marks the copies resting in the pool, which must
    // not hand themselves over again.
    Batch() = default;
    Batch(Batch&&) = default;
    Batch& operator=(Batch&&) = default;
    ~Batch();
    bool pooled = false;
};

// Submission queue (guest thread -> render thread)
void submit_batch(std::unique_ptr<Batch> b);
std::unique_ptr<Batch> take_batch(int timeout_ms);

// A game that turns its own camera after the viewer's head -- so that what the head looks
// at is on the game's screen, where its targeting and its 2D overlays work -- says by how
// much, from the guest thread, before it draws from that camera. The turn takes its place
// in the command stream there (fifo.cpp), so the batch the front end builds from what comes
// after it carries it (Batch::view_turn), however far behind the guest the front end is.
// Each eye is drawn through its own view times the turn: the world stays where the viewer
// saw it, while everything the game places relative to its camera -- the HUD frame, what
// it holds in view space -- turns with the camera. A pose the game reads (vr::hand_pose,
// vr::eye_views) is in the unturned frame; the game takes the turn back off before using
// it against its camera.
// It should stay the identity wherever the game is not drawing from the camera it turns:
// a frame morphing between the panel and the eyes takes the turn at its panel end too.
// The identity until a game says otherwise.
void set_view_turn(const float q[4]);

// A game's tag for the draws it makes from here on (PixelState::tag), from the guest thread,
// 0 to end it: what its eye filter can tell a set of draws by when nothing in their state
// says what they are -- the draws one of its functions makes, say, tagged on entering the
// function and untagged on leaving. Like a view turn, the tag takes its place in the command
// stream, so the front end, however far behind, applies it to the draws written after it.
// It is only as exact as the stream: the last 31 bytes or less the game wrote before the
// call may still be in the gather pipe, so a draw that ends just before a change of tag can
// land on the far side of it. A tag is best changed where the game has written a little
// state (a projection, a viewport) between the change and the nearest draw.
void set_draw_tag(uint8_t tag);

// Frame pacing / stats
extern std::atomic<uint32_t> g_frames_submitted;

// Called on the guest thread at the end of every submitted frame, with the count above: the
// game's own per-frame work on its state, which is consistent there -- the guest is between
// frames -- unlike on the render thread, which may be frames behind it. A game installs it
// from a static initializer in its SOURCES; one hook.
using GuestFrameHook = void (*)(uint32_t frame);
void set_guest_frame_hook(GuestFrameHook hook);

}  // namespace gx
