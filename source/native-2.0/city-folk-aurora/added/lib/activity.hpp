#pragma once
// 0236: what Aurora's own threads are doing right now, for an application's hang watchdog
// (aurora_describe_activity, present_timing.h). Each thread kind has one slot: a string literal
// and the time it was set. Scopes nest (the outer activity comes back when an inner one ends).
// Setting a slot is two relaxed atomic stores, cheap enough for every frame.
#include <atomic>
#include <chrono>
#include <cstdint>

namespace aurora::activity {
enum class Thread : uint8_t { Host, RenderWorker, Fifo, PresentWaiter, Count };

struct Slot {
  std::atomic<const char*> what{nullptr};
  std::atomic<int64_t> sinceNs{0};
};
inline Slot g_slots[static_cast<size_t>(Thread::Count)];

inline int64_t now_ns() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

class Scope {
public:
  Scope(Thread thread, const char* what) noexcept : m_slot(g_slots[static_cast<size_t>(thread)]) {
    m_prevWhat = m_slot.what.load(std::memory_order_relaxed);
    m_prevSince = m_slot.sinceNs.load(std::memory_order_relaxed);
    m_slot.sinceNs.store(now_ns(), std::memory_order_relaxed);
    m_slot.what.store(what, std::memory_order_release);
  }
  ~Scope() {
    // The outer activity resumes; its clock restarts (how long it has been in it again).
    m_slot.sinceNs.store(m_prevWhat != nullptr ? now_ns() : m_prevSince, std::memory_order_relaxed);
    m_slot.what.store(m_prevWhat, std::memory_order_release);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

private:
  Slot& m_slot;
  const char* m_prevWhat;
  int64_t m_prevSince;
};
} // namespace aurora::activity
