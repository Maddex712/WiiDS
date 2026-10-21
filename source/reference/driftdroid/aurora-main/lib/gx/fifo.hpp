#pragma once

#include "../internal.hpp"

#include <cstring>
#include <functional>

namespace aurora::gx::fifo {

namespace detail {
extern uint8_t* sBufferData;
extern uint32_t sBufferSize;
extern uint32_t sBufferCapacity;
extern bool sInDisplayList;
extern uint8_t* sDlBuffer;
extern uint32_t sDlSize;
extern uint32_t sDlWritePos;
} // namespace detail

void init();

// Out-of-line slow path: grows internal buffer then appends data
void write_data_grow(const void* data, uint32_t length);

inline void write_data(const void* data, const uint32_t length) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (detail::sBufferSize + length <= detail::sBufferCapacity)
        LIKELY {
          std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
          detail::sBufferSize += length;
          return;
        }
      write_data_grow(data, length);
    }
  else if (detail::sDlWritePos + length <= detail::sDlSize) {
    std::memcpy(detail::sDlBuffer + detail::sDlWritePos, data, length);
    detail::sDlWritePos += length;
  }
}

inline void write_u8(const uint8_t val) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (detail::sBufferSize < detail::sBufferCapacity)
        LIKELY {
          detail::sBufferData[detail::sBufferSize++] = val;
          return;
        }
      write_data_grow(&val, 1);
    }
  else if (detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = val;
  }
}

inline void write_u16(const uint16_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u32(const uint32_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u64(const uint64_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_f32(const float val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

// Display list recording
void begin_display_list(uint8_t* buf, uint32_t size);
uint32_t end_display_list();
bool in_display_list();

// Drain the internal FIFO buffer through the command processor. Returns once everything written so
// far has been processed, so the caller may read or change renderer state afterwards.
void drain();

// Hand the internal FIFO buffer to the command processor without waiting for it. With threaded
// processing enabled the GX worker decodes it later; otherwise this is drain().
void drain_async();

// Queue a complete command stream owned by the caller (display lists). The bytes are copied when
// threaded processing is enabled, since the caller's memory may change once this returns.
void submit_stream(const uint8_t* data, uint32_t size, bool bigEndian);

// Wait for the GX worker to finish everything submitted so far. Required before touching renderer
// or GX state from the game thread outside the command stream. Cheap when nothing is pending.
void sync();

// Decode commands on a dedicated worker thread instead of the game thread (Dolphin "dual core"
// style). The worker may lag within a frame; every drain()/sync() point catches it up.
void set_threaded(bool enabled);
bool threaded();

// Queue `fn` to run on the GX worker at this point in the command stream, instead of syncing
// with the worker and running it on the game thread. For GX calls that change decoder-owned
// state (g_gxState) or record renderer work outside process(): in stream order they see exactly
// the state the game set up before them. Only valid while threaded() and not recording a display
// list; callers keep their synchronous path otherwise.
void run_in_stream(std::function<void()> fn);
// Worker side: runs the oldest queued closure (the GX_LOAD_AURORA_RUN_DEFERRED handler).
void run_next_deferred();

// Cumulative count and duration of sync()/drain() calls that actually waited for the GX worker,
// and the same per calling site (a return address). Returns how many sites were written.
struct SyncSiteStat {
  uintptr_t site;
  uint64_t waits;
  uint64_t nanos;
};
size_t sync_stats(uint64_t* totalWaits, uint64_t* totalNanos, SyncSiteStat* sites, size_t maxSites);

// Internal buffer inspection
const uint8_t* get_buffer_data();
uint32_t get_buffer_size();
void clear_buffer();

} // namespace aurora::gx::fifo
