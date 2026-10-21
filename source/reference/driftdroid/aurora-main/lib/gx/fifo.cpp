#include "fifo.hpp"
#include "command_processor.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "dolphin/gx/GXAurora.h"
#include "dolphin/gx/GXCommandList.h"
#include "tracy/Tracy.hpp"

#ifdef __SWITCH__
extern "C" void SwitchConfigureGxWorkerThread() __attribute__((weak));
#endif

namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

namespace detail {
uint8_t* sBufferData = nullptr;
uint32_t sBufferSize = 0;
uint32_t sBufferCapacity = 0;
bool sInDisplayList = false;
uint8_t* sDlBuffer = nullptr;
uint32_t sDlSize = 0;
uint32_t sDlWritePos = 0;
} // namespace detail

void init() {
  constexpr uint32_t initialCapacity = 64 * 1024;
  reset_cp_register_cache();
  free(detail::sBufferData);
  detail::sBufferData = static_cast<uint8_t*>(malloc(initialCapacity));
  detail::sBufferSize = 0;
  detail::sBufferCapacity = initialCapacity;
  detail::sInDisplayList = false;
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
}

void write_data_grow(const void* data, uint32_t length) {
  uint32_t needed = detail::sBufferSize + length;
  uint32_t newCap = std::max(detail::sBufferCapacity * 2, needed);
  detail::sBufferData = static_cast<uint8_t*>(realloc(detail::sBufferData, newCap));
  std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
  detail::sBufferSize = needed;
  detail::sBufferCapacity = newCap;
}

void begin_display_list(uint8_t* buf, uint32_t size) {
  detail::sInDisplayList = true;
  detail::sDlBuffer = buf;
  detail::sDlSize = size;
  detail::sDlWritePos = 0;
}

uint32_t end_display_list() {
  detail::sInDisplayList = false;
  uint32_t bytesWritten = detail::sDlWritePos;
  uint32_t padded = (bytesWritten + 31) & ~31u;
  while (detail::sDlWritePos < padded && detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = 0;
  }
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
  return padded;
}

bool in_display_list() { return detail::sInDisplayList; }

// How much of the producer's frame is spent blocked before it may decode the next batch of GX commands.
static void note_drain_wait(uint64_t nanos) noexcept {
  ZoneScopedN("FIFO drain wait");
  TracyPlot("aurora: fifoDrainWaitUs", static_cast<int64_t>(nanos / 1000));
}

namespace {
// One command stream handed to the worker. The producer's FIFO buffer is swapped out rather than
// copied, so a drain costs a pointer exchange; buffers are recycled through sPool.
struct Batch {
  uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
  bool bigEndian = true;
};

// Roughly a frame's worth of commands on this game; small enough that the worker still overlaps
// with the game thread, large enough to amortise the handoff.
constexpr uint32_t kAsyncFlushThresholdBytes = 32u * 1024u;

std::atomic_bool sThreaded{false};
// Set by the producer on submit, cleared by the worker once the inbox is empty and it is idle.
std::atomic_bool sPending{false};
std::mutex sMutex;
std::condition_variable sWorkCv;
std::condition_variable sIdleCv;
std::deque<Batch> sQueue;
std::vector<Batch> sPool;
bool sWorkerBusy = false;
bool sWorkerStop = false;
std::thread sWorker;
std::atomic<std::thread::id> sWorkerId{};

void worker_main() {
  sWorkerId.store(std::this_thread::get_id(), std::memory_order_release);
  aurora::pin_calling_thread_to_core_tier(aurora::CoreTier::Fast);
  aurora::name_calling_thread("GxWorker");
#ifdef __SWITCH__
  // Supplied by the Switch runtime (main.cpp): dedicated core and raised priority.
  if (SwitchConfigureGxWorkerThread != nullptr) {
    SwitchConfigureGxWorkerThread();
  }
#endif
  // GXEnd drains after every primitive, so batches arrive in the thousands per frame. Take the
  // whole queue per wakeup: one lock round-trip and one frame-worker join for the lot.
  std::vector<Batch> batches;
  std::unique_lock lock(sMutex);
  while (true) {
    sWorkCv.wait(lock, [] { return sWorkerStop || !sQueue.empty(); });
    if (sQueue.empty()) {
      break;
    }
    batches.assign(sQueue.begin(), sQueue.end());
    sQueue.clear();
    sWorkerBusy = true;
    lock.unlock();
    {
      ZoneScopedN("GX worker batch");
      // The producer-side drain waited here; recording the next frame must still follow SEALED.
      aurora::wait_for_frame_worker_sealed_quiet();
      for (const auto& batch : batches) {
        process(batch.data, batch.size, batch.bigEndian);
      }
    }
    lock.lock();
    for (auto& batch : batches) {
      batch.size = 0;
      sPool.push_back(batch);
    }
    batches.clear();
    sWorkerBusy = false;
    if (sQueue.empty()) {
      sPending.store(false, std::memory_order_release);
      sIdleCv.notify_all();
    }
  }
}

// Caller holds sMutex. Returns a recycled buffer with at least `capacity` bytes, or a new one.
Batch take_pooled_locked(uint32_t capacity) {
  if (!sPool.empty()) {
    // Newest first: the buffer most likely still hot in cache, and all pooled buffers grow to the
    // same working size within a frame or two.
    Batch batch = sPool.back();
    sPool.pop_back();
    if (batch.capacity < capacity) {
      batch.data = static_cast<uint8_t*>(realloc(batch.data, capacity));
      batch.capacity = capacity;
    }
    batch.size = 0;
    return batch;
  }
  return Batch{static_cast<uint8_t*>(malloc(capacity)), 0, capacity, true};
}

// Hands the producer's own FIFO buffer to the worker and installs a recycled one in its place.
void enqueue_buffer_locked() {
  if (detail::sBufferSize == 0) {
    return;
  }
  Batch replacement = take_pooled_locked(detail::sBufferCapacity);
  sQueue.push_back(Batch{detail::sBufferData, detail::sBufferSize, detail::sBufferCapacity, true});
  detail::sBufferData = replacement.data;
  detail::sBufferCapacity = replacement.capacity;
  detail::sBufferSize = 0;
}

// Display lists live in memory the caller may reuse, so these bytes are copied.
void enqueue_copy_locked(const uint8_t* data, uint32_t size, bool bigEndian) {
  Batch batch = take_pooled_locked(size);
  std::memcpy(batch.data, data, size);
  batch.size = size;
  batch.bigEndian = bigEndian;
  sQueue.push_back(batch);
}

void flush_buffer_to_worker() {
  bool wake;
  {
    std::lock_guard lock(sMutex);
    if (detail::sBufferSize == 0) {
      return;
    }
    enqueue_buffer_locked();
    sPending.store(true, std::memory_order_release);
    wake = !sWorkerBusy;
  }
  if (wake) {
    sWorkCv.notify_one();
  }
}

void drain_inline() {
  // SEALED, not DONE.
  const auto waited = aurora::wait_for_frame_worker_sealed();
  if (waited.count() > 0) UNLIKELY {
    note_drain_wait(static_cast<uint64_t>(waited.count()));
  }
  if (detail::sBufferSize == 0) {
    return;
  }
  process(detail::sBufferData, detail::sBufferSize, true);
  detail::sBufferSize = 0;
}
} // namespace

namespace {
// Which game-thread call sites force a catch-up with the GX worker, and how long each one stands
// the game thread still. Only calls that actually had to wait are counted. Read by the Switch
// profiler; a handful of atomics per real wait is noise next to the wait itself.
constexpr size_t kSyncSiteSlots = 64;
struct SyncSiteSlot {
  std::atomic<uintptr_t> site{0};
  std::atomic<uint64_t> waits{0};
  std::atomic<uint64_t> nanos{0};
};
SyncSiteSlot sSyncSites[kSyncSiteSlots];
std::atomic<uint64_t> sSyncWaits{0};
std::atomic<uint64_t> sSyncNanos{0};

void note_sync_site(uintptr_t site, uint64_t nanos) noexcept {
  sSyncWaits.fetch_add(1, std::memory_order_relaxed);
  sSyncNanos.fetch_add(nanos, std::memory_order_relaxed);
  for (size_t i = 0; i < kSyncSiteSlots; ++i) {
    SyncSiteSlot& slot = sSyncSites[(site / 4 + i) % kSyncSiteSlots];
    uintptr_t expected = 0;
    if (slot.site.load(std::memory_order_relaxed) == site ||
        slot.site.compare_exchange_strong(expected, site, std::memory_order_relaxed) ||
        expected == site) {
      slot.waits.fetch_add(1, std::memory_order_relaxed);
      slot.nanos.fetch_add(nanos, std::memory_order_relaxed);
      return;
    }
  }
}

[[gnu::noinline]] void sync_from(uintptr_t site) {
  if (!sPending.load(std::memory_order_acquire)) {
    return;
  }
  if (std::this_thread::get_id() == sWorkerId.load(std::memory_order_acquire)) {
    // Reached from inside command processing; the worker cannot wait for itself.
    return;
  }
  ZoneScopedN("GX worker sync");
  constexpr auto kServiceInterval = std::chrono::milliseconds(1);
  const auto start = std::chrono::steady_clock::now();
  bool waited = false;
  std::unique_lock lock(sMutex);
  while (!sQueue.empty() || sWorkerBusy) {
    waited = true;
    if (!sIdleCv.wait_for(lock, kServiceInterval, [] { return sQueue.empty() && !sWorkerBusy; })) {
      // Keep the guest's alarm/retrace pump alive, as the inline drain's frame-worker wait does.
      lock.unlock();
      aurora::service_producer_wait();
      lock.lock();
    }
  }
  if (waited) {
    note_sync_site(site, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count()));
  }
}
} // namespace

[[gnu::noinline]] void sync() { sync_from(reinterpret_cast<uintptr_t>(__builtin_return_address(0))); }

[[gnu::noinline]] void drain() {
  if (!sThreaded.load(std::memory_order_relaxed)) {
    drain_inline();
    return;
  }
  flush_buffer_to_worker();
  sync_from(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
}

size_t sync_stats(uint64_t* totalWaits, uint64_t* totalNanos, SyncSiteStat* sites, size_t maxSites) {
  *totalWaits = sSyncWaits.load(std::memory_order_relaxed);
  *totalNanos = sSyncNanos.load(std::memory_order_relaxed);
  size_t count = 0;
  for (const auto& slot : sSyncSites) {
    const uintptr_t site = slot.site.load(std::memory_order_relaxed);
    if (site != 0 && count < maxSites) {
      sites[count++] = {site, slot.waits.load(std::memory_order_relaxed),
                        slot.nanos.load(std::memory_order_relaxed)};
    }
  }
  return count;
}

void drain_async() {
  if (!sThreaded.load(std::memory_order_relaxed)) {
    drain_inline();
    return;
  }
  // GXEnd calls this after every primitive. Handing over that often costs a lock round-trip and a
  // futex wake per primitive on the game thread, which is exactly the thread being protected, so
  // accumulate until there is a worthwhile batch. Ordering is unaffected: display lists, frame
  // boundaries and every sync point flush whatever is pending first.
  if (detail::sBufferSize < kAsyncFlushThresholdBytes) {
    return;
  }
  flush_buffer_to_worker();
}

void submit_stream(const uint8_t* data, uint32_t size, bool bigEndian) {
  if (!sThreaded.load(std::memory_order_relaxed)) {
    drain_inline();
    process(data, size, bigEndian);
    return;
  }
  if (bigEndian && !detail::sInDisplayList) LIKELY {
    // A race frame calls ~1150 display lists. Handing each to the worker on its own cost a queue
    // lock, a pooled-batch copy and often a futex wake per list, and every wake handed the queue
    // mutex to the time-sliced worker. The producer buffer is already a big-endian command
    // stream, so append the list to it and let the usual 32 KiB threshold and sync points hand it
    // over: same bytes, same order, far fewer hand-offs.
    if (detail::sBufferSize + size <= detail::sBufferCapacity) LIKELY {
      std::memcpy(detail::sBufferData + detail::sBufferSize, data, size);
      detail::sBufferSize += size;
    } else {
      write_data_grow(data, size);
    }
    if (detail::sBufferSize >= kAsyncFlushThresholdBytes) {
      flush_buffer_to_worker();
    }
    return;
  }
  bool wake;
  {
    std::lock_guard lock(sMutex);
    // Keep stream order: anything still in the FIFO buffer was written before this list.
    enqueue_buffer_locked();
    enqueue_copy_locked(data, size, bigEndian);
    sPending.store(true, std::memory_order_release);
    wake = !sWorkerBusy;
  }
  if (wake) {
    sWorkCv.notify_one();
  }
}

void set_threaded(bool enabled) {
  if (enabled && (webgpu::g_backendType == wgpu::BackendType::OpenGL ||
                  webgpu::g_backendType == wgpu::BackendType::OpenGLES)) {
    // Decoding commands off the main thread means mapping vertex buffers there, and Dawn's GL
    // backend keeps its one EGL context current on the thread that last used the device, so the
    // second thread's eglMakeCurrent fails with EGL_BAD_ACCESS. Same reason the asynchronous
    // frame submission worker stays off for these backends (see frame_worker_requested).
    static const bool logged = [] {
      Log.info("Threaded GX command processing is unavailable on the OpenGL backend");
      return true;
    }();
    (void)logged;
    return;
  }
  if (enabled == sThreaded.load(std::memory_order_relaxed)) {
    return;
  }
  if (enabled) {
    {
      std::lock_guard lock(sMutex);
      sWorkerStop = false;
    }
    sWorker = std::thread(worker_main);
    sThreaded.store(true, std::memory_order_relaxed);
    Log.info("Enabled threaded GX command processing");
    return;
  }
  drain();
  sThreaded.store(false, std::memory_order_relaxed);
  {
    std::lock_guard lock(sMutex);
    sWorkerStop = true;
  }
  sWorkCv.notify_all();
  if (sWorker.joinable()) {
    sWorker.join();
  }
  Log.info("Disabled threaded GX command processing");
}

bool threaded() { return sThreaded.load(std::memory_order_relaxed); }

namespace {
std::mutex sDeferredMutex;
std::deque<std::function<void()>> sDeferred;
} // namespace

void run_in_stream(std::function<void()> fn) {
  {
    std::lock_guard lock(sDeferredMutex);
    sDeferred.push_back(std::move(fn));
  }
  // Written after the push, so the worker can never reach the marker before its closure exists.
  write_u8(GX_LOAD_AURORA);
  write_u16(GX_LOAD_AURORA_RUN_DEFERRED);
  drain_async();
}

void run_next_deferred() {
  std::function<void()> fn;
  {
    std::lock_guard lock(sDeferredMutex);
    if (sDeferred.empty()) {
      Log.warn("GX_LOAD_AURORA_RUN_DEFERRED with no queued closure");
      return;
    }
    fn = std::move(sDeferred.front());
    sDeferred.pop_front();
  }
  fn();
}

const uint8_t* get_buffer_data() { return detail::sBufferData; }
uint32_t get_buffer_size() { return detail::sBufferSize; }
void clear_buffer() {
  detail::sBufferSize = 0;
}

} // namespace aurora::gx::fifo
