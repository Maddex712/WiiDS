#include "gx.hpp"
#include "__gx.h"

#include "../../gx/command_processor.hpp"
#include "../../gx/fifo.hpp"
#include "../../gfx/common.hpp"

#include <cstring>

static __GXData_struct sSavedGXData;

// process() stops at a draw that no longer fits the staging batch and returns where it stopped; submit the batch and
// resume there, as fifo::drain does. Ignoring that return dropped the rest of the list (SPM: back walls and floors of
// big map display lists never drew).
static void process_display_list(const u8* data, u32 nbytes, bool bigEndian) {
  struct ReplayScope {
    bool prev = aurora::gx::fifo::g_replayingDisplayList;
    ReplayScope() { aurora::gx::fifo::g_replayingDisplayList = true; }
    ~ReplayScope() { aurora::gx::fifo::g_replayingDisplayList = prev; }
  } replayScope;
  u32 consumed = 0;
  bool retried = false;
  while (consumed < nbytes) {
    const u32 count = aurora::gx::fifo::process(data + consumed, nbytes - consumed, bigEndian);
    if (count == 0 && retried)
      throw aurora::gfx::StagingCapacityError("display list draw does not fit after capacity submission");
    consumed += count;
    if (consumed >= nbytes) break;
    aurora::gfx::split_staging_batch();
    retried = true;
  }
}

extern "C" {
bool GXDisplayListIsEmpty() {
  return aurora::gx::fifo::in_display_list() && aurora::gx::fifo::detail::sDlWritePos == 0 &&
         __gx->dirtyState == 0;
}

void GXBeginDisplayList(void* list, u32 size) {
  CHECK(!aurora::gx::fifo::in_display_list(), "Display list began twice!");

  // Flush any pending dirty state before recording
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Save current shadow register state if requested
  if (__gx->dlSaveContext != 0) {
    std::memcpy(&sSavedGXData, __gx, sizeof(sSavedGXData));
  }

  __gx->inDispList = 1;

  // Redirect FIFO writes to the user-provided buffer
  aurora::gx::fifo::begin_display_list(static_cast<u8*>(list), size);
}

u32 GXEndDisplayList() {
  // Flush any pending dirty state into the display list
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // End FIFO redirection and get the byte count (ROUNDUP32)
  u32 bytesWritten = aurora::gx::fifo::end_display_list();

  // Restore saved shadow register state
  if (__gx->dlSaveContext != 0) {
    std::memcpy(__gx, &sSavedGXData, sizeof(*__gx));
  }

  __gx->inDispList = 0;

  return bytesWritten;
}

void GXCallDisplayList(const void* data, u32 nbytes) {
  // Flush any pending dirty state before calling
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Flush pending primitives
  if (*reinterpret_cast<u32*>(&__gx->vNum) != 0) {
    __GXSendFlushPrim();
  }

  // Nested display-list calls add bytes instead of changing live renderer state.
  if (aurora::gx::fifo::in_display_list()) {
    aurora::gx::fifo::write_data(data, nbytes);
    return;
  }

  // Decode the display list immediately while its borrowed resources are valid.
  aurora::gx::fifo::drain();
  process_display_list(static_cast<const u8*>(data), nbytes, true);
}

void GXCallDisplayListLE(const void* data, u32 nbytes) {
  // Flush any pending dirty state before calling
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Flush pending primitives
  if (*reinterpret_cast<u32*>(&__gx->vNum) != 0) {
    __GXSendFlushPrim();
  }

  // Decode little-endian lists separately after finishing the normal FIFO work.
  aurora::gx::fifo::drain();

  // Process the display list through the command processor (little-endian)
  process_display_list(static_cast<const u8*>(data), nbytes, false);
}
}
