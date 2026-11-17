#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace std::chrono_literals;

static std::atomic<uint32_t> createdPipelines{0}, queuedPipelines{0};
static std::atomic<bool> g_gpuCachePrunePending{false};
struct ObservedCv {
  std::condition_variable value;
  std::atomic<unsigned> notifications{0};
  void notify_all() { ++notifications; value.notify_all(); }
} g_pipelineReadyCv;
static std::atomic<unsigned> prunes{0}, notificationsAtPrune{0};
static bool blockPrune = false, releasePrune = false;
static std::mutex pruneMutex;
static std::condition_variable pruneCv;
static std::promise<void> pruneEntered;
namespace webgpu {
void cache_prune() {
  ++prunes;
  notificationsAtPrune = g_pipelineReadyCv.notifications.load();
  if (blockPrune) {
    pruneEntered.set_value();
    std::unique_lock lock(pruneMutex);
    pruneCv.wait(lock, [] { return releasePrune; });
  }
}
}
#include "actual_notify.inc"

static unsigned checks = 0, failures = 0;
static void check(bool condition, const char* message) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
static void reset(uint32_t queued, bool pending) {
  createdPipelines = 0; queuedPipelines = queued;
  g_gpuCachePrunePending = pending;
  g_pipelineReadyCv.notifications = 0; prunes = 0; notificationsAtPrune = 0;
  blockPrune = false; releasePrune = false;
}
int main() {
  // Same publication and predicate synchronization as find_pipeline_impl and
  // pipeline_worker. The real disk prune boundary is replaced by a held gate.
  reset(1, true);
  blockPrune = true;
  std::mutex pipelineMutex;
  bool pipelinePublished = false;
  std::promise<void> waiting, drawDone;
  auto waitingFuture = waiting.get_future();
  auto drawFuture = drawDone.get_future();
  auto pruneFuture = pruneEntered.get_future();
  std::thread draw([&] {
    std::unique_lock lock(pipelineMutex);
    waiting.set_value();
    g_pipelineReadyCv.value.wait(lock, [&] { return pipelinePublished; });
    drawDone.set_value();
  });
  waitingFuture.wait();
  std::thread worker([&] {
    { std::lock_guard lock(pipelineMutex); pipelinePublished = true; }
    notify_pipeline_ready(true, 3);
  });
  const bool entered = pruneFuture.wait_for(5s) == std::future_status::ready;
  check(entered, "prune was reached");
  if (entered) {
    check(notificationsAtPrune.load() == 1, "ready notification precedes disk prune");
    check(drawFuture.wait_for(2s) == std::future_status::ready,
          "already-ready draw completes while prune remains blocked");
    check(createdPipelines == 3 && queuedPipelines == 0,
          "bookkeeping published before prune");
    check(!g_gpuCachePrunePending && prunes == 1, "prune claimed once");
  }
  { std::lock_guard lock(pruneMutex); releasePrune = true; }
  pruneCv.notify_all();
  worker.join(); draw.join();
  check(g_pipelineReadyCv.notifications == 1, "one ready notification");

  reset(2, true);
  notify_pipeline_ready(true, 4);
  check(createdPipelines == 4 && queuedPipelines == 1, "non-last queued accounting");
  check(prunes == 0 && g_gpuCachePrunePending, "non-last preserves pending prune");
  check(g_pipelineReadyCv.notifications == 1, "non-last wakes waiting draws");
  reset(0, true);
  notify_pipeline_ready(false, 2);
  check(createdPipelines == 2 && queuedPipelines == 0, "unqueued accounting");
  check(prunes == 0 && g_gpuCachePrunePending, "unqueued does not claim prune");
  check(g_pipelineReadyCv.notifications == 1, "unqueued wakes waiting draws");
  reset(1, false);
  notify_pipeline_ready(true, 1);
  check(createdPipelines == 1 && queuedPipelines == 0, "last queued no-prune accounting");
  check(prunes == 0 && g_pipelineReadyCv.notifications == 1, "no pending prune still wakes");
  reset(64, true);
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 64; ++i)
    workers.emplace_back([] { notify_pipeline_ready(true, 2); });
  for (auto& thread : workers) thread.join();
  check(createdPipelines == 128 && queuedPipelines == 0, "concurrent completion accounting");
  check(prunes == 1 && !g_gpuCachePrunePending, "concurrent completions prune exactly once");
  check(g_pipelineReadyCv.notifications == 64, "all concurrent completions notify");
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
