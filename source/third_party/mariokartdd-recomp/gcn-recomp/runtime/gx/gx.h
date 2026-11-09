// GX (Flipper graphics) command processing: shared state definitions.
#pragma once
#include <cstdint>
#include <vector>

namespace gx {

// Primitive types (command byte & 0xF8)
enum Prim : uint8_t {
    PRIM_QUADS = 0x80, PRIM_QUADS2 = 0x88, PRIM_TRIANGLES = 0x90, PRIM_TRISTRIP = 0x98,
    PRIM_TRIFAN = 0xA0, PRIM_LINES = 0xA8, PRIM_LINESTRIP = 0xB0, PRIM_POINTS = 0xB8,
};

struct State {
    uint32_t bp[256];
    uint32_t cp[256];
    uint32_t xf_mem[0x800];   // 0x0000-0x07FF: matrices, lights
    uint32_t xf_regs[0x100];  // 0x1000-0x10FF
    uint32_t bp_mask = 0xFFFFFF;
    // Set whenever a BP or XF register the pixel state is built from takes a new value;
    // cleared by the front end once it has snapshotted. A race frame issues some 13,000
    // draw commands but changes this state only about 1,000 times, so between changes the
    // front end hands back the last snapshot without building one to compare.
    bool pixel_dirty = true;
    // Bumped whenever a CP register is written. The vertex layout and the byte size of a
    // vertex are a few dozen bit extractions each and were both re-derived for every one
    // of those 13,000 draw commands, from registers the game sets once per model. Each is
    // cached against this counter, separately, so that neither can clear a flag the other
    // still needs. See layout_for() in xf.cpp and vertex_size() in cmd.cpp.
    uint32_t cp_gen = 1;
    // Bumped whenever the part of XF the vertex transform reads changes, one counter per
    // region (see XfRegion), so that a draw can snapshot only what moved since the last
    // one: the transform runs after the frame is submitted, off the guest thread, from
    // those snapshots. See XfDraw in xf.cpp.
    uint32_t xf_gen[5] = {1, 1, 1, 1, 1};
    // And one per sixteen-word block, XF memory's 128 then the registers' first six, so
    // that a region that has changed is copied a block at a time rather than whole: a
    // matrix load touches one or two blocks of a region of seventeen.
    uint32_t xf_block_gen[0x86] = {};
};

extern State g_state;

// The regions of XF state the vertex transform reads, each snapshotted on its own. A region
// runs a little past what it is named for, because a matrix index can address a few words
// beyond it and the transform must read what the hardware would.
enum XfRegion { XF_MTX, XF_NRM, XF_POST, XF_LIGHT, XF_REGS, XF_REGIONS };
constexpr uint32_t kXfBase[XF_REGIONS] = {0x000, 0x400, 0x500, 0x600, 0x00};
constexpr uint32_t kXfLen[XF_REGIONS] = {0x110, 0x110, 0x110, 0x090, 0x60};
constexpr uint32_t kXfRegBlocks = 0x80;  // xf_block_gen index of the registers' first block
inline void xf_mem_written(uint32_t addr) {
    g_state.xf_block_gen[addr >> 4]++;
    for (int r = 0; r < XF_REGS; r++)
        if (addr - kXfBase[r] < kXfLen[r]) g_state.xf_gen[r]++;
}
inline void xf_reg_written(uint32_t reg) {
    if (reg < kXfLen[XF_REGS]) {
        g_state.xf_block_gen[kXfRegBlocks + (reg >> 4)]++;
        g_state.xf_gen[XF_REGS]++;
    }
}

// Byte-size of one vertex for the given VAT index, based on current VCD/VAT.
uint32_t vertex_size(int vat);

// Process a buffer of FIFO commands. Returns bytes consumed (may stop early on a
// partial command when `partial_ok`).
uint32_t process(const uint8_t* data, uint32_t len, bool partial_ok);

// The guest thread's side of a front end that runs on its own thread (fifo.cpp). Walks the
// same commands process() would, but executes none of them: it keeps its own copy of the
// CP registers (for vertex sizes) and of the BP registers (for the write mask), appends
// every complete command to `out` -- display lists inlined, since the memory they live in
// may be reused before the front end gets to them -- and calls `sync` at each command
// whose effect the guest can observe, after appending it: a PE token or finish, and the
// copy that ends a frame. Returns bytes consumed, as process() does.
// GCN_STALLS=1: how long the pipeline's threads wait on one another, at each place one can
// block -- the guest on the front end (its queue full, a draw-done, a draw-sync token
// read), the front end on the transform, the transform on the renderer -- summed and
// printed every 60 frames. A thread's time minus its stalls is its own work, which is
// what tells the stage that sets the pace from the stages waiting on it.
enum class Stall { FeFull, Finish, Token, XfFull, RenderFull, Count };
bool stalls_on();
uint32_t frames_taken();   // frames the renderer has taken off its queue
void stall_add(Stall s, uint64_t us);
// Times a wait when GCN_STALLS is set.
struct StallTimer {
    Stall s;
    uint64_t t0;
    explicit StallTimer(Stall st);
    ~StallTimer();
};

// Whether a draw-sync token read may show the GPU a frame behind the CPU (fifo.cpp, "draw-sync
// lag"). Off unless a game turns it on: what a game does with the value is the game's.
// GCN_GX_TOKEN_LAG=0 or 1 overrides the game.
void set_draw_sync_lag(bool on);

// Texture copies a game's CPU reads. An EFB copy is made on the GPU and never reaches RAM,
// which is all a game drawing with it needs; one that copies the frame out and then moves
// or reads the bytes with the CPU -- to find what is under a crosshair from ids drawn into
// the frame, say -- reads whatever RAM held there before. A game names those copies with a
// filter, called for every texture copy; each it accepts is written to RAM in its texture
// format, but late: at the display copy two after the one that ends the copy's own frame
// (copy_writebacks_land, on the guest thread, so a replay lands them at the same guest
// instruction). The hardware has them there by the next draw-done; making a frontend wait
// for that cut the frame in pieces (xf.cpp has the story), so this suits a copy the game
// reads every frame and can take a few frames old, not one it reads once, straight after.
// Before the bytes land the filter is asked again, with `clear` false, whether the copy is
// still wanted: the game may have freed the memory in between. Ignored without a renderer.
// `dest` is the physical address the copy register holds.
using CopyWritebackFilter = bool (*)(uint32_t dest, uint32_t w, uint32_t h, uint32_t format, bool clear);
void set_copy_writeback(CopyWritebackFilter filter);
// The guest thread's part, at each display copy, with the count of them so far.
void copy_writebacks_land(uint32_t frames);

enum class SkimSync { Token, TokenInt, Finish, Frame };
using SkimSyncFn = void (*)(SkimSync kind, uint32_t value, std::vector<uint8_t>& out);
// With partial_ok, stops at a command that runs past `len` and returns where it starts;
// `need`, when given, is then set to that command's whole length (0 while even its header
// is short), so a caller can tell how much more must arrive before it is worth calling again.
uint32_t skim(const uint8_t* data, uint32_t len, bool partial_ok, std::vector<uint8_t>& out, SkimSyncFn sync,
              uint32_t* need = nullptr);

// Set when the front end runs on its own thread: process() then leaves the PE signals and
// the frame count to the guest thread's skim, which raises them at the same guest
// instruction as before.
extern bool g_fe_threaded;

// Callbacks to the renderer (weakly defined null implementations in cmd.cpp).
struct DrawCall {
    uint8_t prim;
    uint8_t vat;
    uint16_t count;
    const uint8_t* data;  // vertex data (big endian, as in FIFO)
    uint32_t stride;
};
void renderer_draw(const DrawCall& dc);
void renderer_efb_copy(uint32_t dest_addr, bool to_xfb);
void renderer_bp_write(uint32_t reg, uint32_t value);

}  // namespace gx
