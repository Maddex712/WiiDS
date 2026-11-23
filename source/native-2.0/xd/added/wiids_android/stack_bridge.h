#pragma once
#include <type_traits>
#include <utility>
extern "C" void XdStack_Call(void (*fn)(void*), void* arg, const char* name);
extern "C" bool XdStack_IsForeign();
template<class F> auto XdStack_Invoke(const char* name, F&& fn) -> decltype(fn()) {
    using R = decltype(fn());
    if constexpr (std::is_void_v<R>) {
        XdStack_Call([](void* p) { (*static_cast<std::remove_reference_t<F>*>(p))(); }, &fn, name);
    } else {
        R result{};
        struct Work { std::remove_reference_t<F>* fn; R* result; } work{&fn, &result};
        XdStack_Call([](void* p) { auto& w = *static_cast<Work*>(p); *w.result = (*w.fn)(); }, &work, name);
        return result;
    }
}
