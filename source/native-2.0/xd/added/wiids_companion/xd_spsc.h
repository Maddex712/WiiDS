#pragma once
#include <array>
#include <atomic>
#include <cstddef>
// Exactly one producer and consumer; full queues reject rather than blocking.
template<class T,std::size_t N> class XdSpsc {
    std::array<T,N> slots{};
    alignas(64) std::atomic<std::size_t> readAt{0};
    alignas(64) std::atomic<std::size_t> writeAt{0};
public:
    bool push(const T& value){auto w=writeAt.load(std::memory_order_relaxed);auto next=(w+1)%N;
        if(next==readAt.load(std::memory_order_acquire))return false;
        slots[w]=value;writeAt.store(next,std::memory_order_release);return true;}
    bool pop(T& value){auto r=readAt.load(std::memory_order_relaxed);
        if(r==writeAt.load(std::memory_order_acquire))return false;
        value=slots[r];readAt.store((r+1)%N,std::memory_order_release);return true;}
};
