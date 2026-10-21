#if defined(__ANDROID__)

#include "android_jni_dispatch.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace AndroidJniDispatch {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::function<void()>> g_queue;
bool g_threadStarted = false;

void DispatchThreadMain() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock{g_mutex};
            g_cv.wait(lock, [] { return !g_queue.empty(); });
            task = std::move(g_queue.front());
            g_queue.pop_front();
        }
        task();
    }
}

void EnsureThreadStarted() {
    if (g_threadStarted) {
        return;
    }
    g_threadStarted = true;
    std::thread(DispatchThreadMain).detach();
}

}  // namespace

void Post(std::function<void()> task) {
    std::lock_guard lock{g_mutex};
    EnsureThreadStarted();
    g_queue.push_back(std::move(task));
    g_cv.notify_one();
}

}  // namespace AndroidJniDispatch

#endif  // __ANDROID__
