#pragma once

#include <Dolphin/types.h>

#include <JSystem/type_traits.hxx>

namespace JGadget {
    template <class T1, class T2> struct TPair {
        typedef T1 first_type;
        typedef T2 second_type;

        _GLIBCXX_CONSTEXPR TPair() = default;
        TPair(const TPair &p)      = default;

#if __cplusplus >= 201103L
        TPair(TPair &&p) = default;
#endif

        _GLIBCXX14_CONSTEXPR TPair(const first_type &a, const second_type &b)
            : first(a), second(b) {}

#if __cplusplus >= 201103L
        template <class U1, class U2>
        _GLIBCXX14_CONSTEXPR TPair(U1 &&a, U2 &&b) : first(a), second(b) {}
#endif

        template <class U1, class U2>
        explicit _GLIBCXX14_CONSTEXPR TPair(const TPair<U1, U2> &pair)
            : first(pair.first), second(pair.second) {}

#if __cplusplus >= 201103L
        template <class U1, class U2>
        explicit _GLIBCXX14_CONSTEXPR TPair(TPair<U1, U2> &&pair)
            : first(pair.first), second(pair.second) {}
#endif

        TPair &operator=(const TPair &p) {
            first  = p.first;
            second = p.second;
        }

#if __cplusplus >= 201103L
        template <class U1, class U2> TPair &operator=(const TPair<U1, U2> &p) {
            first  = p.first;
            second = p.second;
        }

        TPair &operator=(TPair &&p)
            _GLIBCXX_NOEXCEPT_IF(JSystem::is_nothrow_move_assignable<T1>::value
                                     &&JSystem::is_nothrow_move_assignable<T2>::value) {
            first  = p.first;
            second = p.second;
        }
#endif

        first_type first;
        second_type second;
    };

    template <class T1, class T2>
    _GLIBCXX14_CONSTEXPR bool operator==(const TPair<T1, T2> &a, const TPair<T1, T2> &b) {
        return a.first == b.first && a.second == b.second;
    }

#if __cplusplus <= 201703L
    template <class T1, class T2>
    _GLIBCXX14_CONSTEXPR bool operator!=(const TPair<T1, T2> &a, const TPair<T1, T2> &b) {
        return a.first != b.first || a.second != b.second;
    }
#endif
}  // namespace JGadget
