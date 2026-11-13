#include <cstdlib>
#include <new>
#include <stdexcept>
void* operator new(std::size_t size) {
    if (size > 64u * 1024 * 1024) throw std::runtime_error("TEST_ALLOCATION_BUDGET");
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

