#pragma once

// C++11 <type_traits> -*- C++ -*-
// Copyright (C) 2007-2019 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.
// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.
/** @file include/type_traits
 *  This is a Standard C++ Library header.
 */

namespace JSystem {

/**
 * @defgroup metaprogramming Metaprogramming
 * @ingroup utilities
 *
 * Template utilities for compile-time introspection and modification,
 * including type classification traits, type property inspection traits
 * and type transformation traits.
 *
 * @{
 */
#include <JSystem/bits/c++config.h>

    typedef decltype(nullptr) nullptr_t;

    /// integral_constant
    template <typename _Tp, _Tp __v> struct integral_constant {
        static constexpr _Tp value = __v;
        typedef _Tp value_type;
        typedef integral_constant<_Tp, __v> type;
        constexpr operator value_type() const noexcept { return value; }
#if __cplusplus > 201103L
#define __cpp_lib_integral_constant_callable 201304
        constexpr value_type operator()() const noexcept { return value; }
#endif
    };
    template <typename _Tp, _Tp __v> constexpr _Tp integral_constant<_Tp, __v>::value;
    /// The type used as a compile-time boolean with true value.
    typedef integral_constant<bool, true> true_type;
    /// The type used as a compile-time boolean with false value.
    typedef integral_constant<bool, false> false_type;
    template <bool __v> using __bool_constant = integral_constant<bool, __v>;
#if __cplusplus > 201402L
#define __cpp_lib_bool_constant 201505
    template <bool __v> using bool_constant = integral_constant<bool, __v>;
#endif
    // Meta programming helper types.
    template <bool, typename, typename> struct conditional;
    template <typename...> struct __or_;
    template <> struct __or_<> : public false_type {};
    template <typename _B1> struct __or_<_B1> : public _B1 {};
    template <typename _B1, typename _B2>
    struct __or_<_B1, _B2> : public conditional<_B1::value, _B1, _B2>::type {};
    template <typename _B1, typename _B2, typename _B3, typename... _Bn>
    struct __or_<_B1, _B2, _B3, _Bn...>
        : public conditional<_B1::value, _B1, __or_<_B2, _B3, _Bn...>>::type {};
    template <typename...> struct __and_;
    template <> struct __and_<> : public true_type {};
    template <typename _B1> struct __and_<_B1> : public _B1 {};
    template <typename _B1, typename _B2>
    struct __and_<_B1, _B2> : public conditional<_B1::value, _B2, _B1>::type {};
    template <typename _B1, typename _B2, typename _B3, typename... _Bn>
    struct __and_<_B1, _B2, _B3, _Bn...>
        : public conditional<_B1::value, __and_<_B2, _B3, _Bn...>, _B1>::type {};
    template <typename _Pp> struct __not_ : public __bool_constant<!bool(_Pp::value)> {};
#if __cplusplus >= 201703L
    template <typename... _Bn> inline constexpr bool __or_v  = __or_<_Bn...>::value;
    template <typename... _Bn> inline constexpr bool __and_v = __and_<_Bn...>::value;
#define __cpp_lib_logical_traits 201510
    template <typename... _Bn> struct conjunction : __and_<_Bn...> {};
    template <typename... _Bn> struct disjunction : __or_<_Bn...> {};
    template <typename _Pp> struct negation : __not_<_Pp> {};
    template <typename... _Bn> inline constexpr bool conjunction_v = conjunction<_Bn...>::value;
    template <typename... _Bn> inline constexpr bool disjunction_v = disjunction<_Bn...>::value;
    template <typename _Pp> inline constexpr bool negation_v       = negation<_Pp>::value;
#endif  // C++17
        // For several sfinae-friendly trait implementations we transport both the
        // result information (as the member type) and the failure information (no
        // member type). This is very similar to enable_if, but we cannot use
        // them, because we need to derive from them as an implementation detail.
    template <typename _Tp> struct __success_type {
        typedef _Tp type;
    };
    struct __failure_type {};
    // Primary type categories.
    template <typename> struct remove_cv;
    template <typename> struct __is_void_helper : public false_type {};
    template <> struct __is_void_helper<void> : public true_type {};
    /// is_void
    template <typename _Tp>
    struct is_void : public __is_void_helper<typename remove_cv<_Tp>::type>::type {};
    template <typename> struct __is_integral_helper : public false_type {};
    template <> struct __is_integral_helper<bool> : public true_type {};
    template <> struct __is_integral_helper<char> : public true_type {};
    template <> struct __is_integral_helper<signed char> : public true_type {};
    template <> struct __is_integral_helper<unsigned char> : public true_type {};
#ifdef _GLIBCXX_USE_WCHAR_T
    template <> struct __is_integral_helper<wchar_t> : public true_type {};
#endif
#ifdef _GLIBCXX_USE_CHAR8_T
    template <> struct __is_integral_helper<char8_t> : public true_type {};
#endif
    template <> struct __is_integral_helper<char16_t> : public true_type {};
    template <> struct __is_integral_helper<char32_t> : public true_type {};
    template <> struct __is_integral_helper<short> : public true_type {};
    template <> struct __is_integral_helper<unsigned short> : public true_type {};
    template <> struct __is_integral_helper<int> : public true_type {};
    template <> struct __is_integral_helper<unsigned int> : public true_type {};
    template <> struct __is_integral_helper<long> : public true_type {};
    template <> struct __is_integral_helper<unsigned long> : public true_type {};
    template <> struct __is_integral_helper<long long> : public true_type {};
    template <> struct __is_integral_helper<unsigned long long> : public true_type {};

    /// is_integral
    template <typename _Tp>
    struct is_integral : public __is_integral_helper<typename remove_cv<_Tp>::type>::type {};
    template <typename> struct __is_floating_point_helper : public false_type {};
    template <> struct __is_floating_point_helper<float> : public true_type {};
    template <> struct __is_floating_point_helper<double> : public true_type {};
    template <> struct __is_floating_point_helper<long double> : public true_type {};

    /// is_floating_point
    template <typename _Tp>
    struct is_floating_point
        : public __is_floating_point_helper<typename remove_cv<_Tp>::type>::type {};
    /// is_array
    template <typename> struct is_array : public false_type {};
    template <typename _Tp, size_t _Size> struct is_array<_Tp[_Size]> : public true_type {};
    template <typename _Tp> struct is_array<_Tp[]> : public true_type {};
    template <typename> struct __is_pointer_helper : public false_type {};
    template <typename _Tp> struct __is_pointer_helper<_Tp *> : public true_type {};
    /// is_pointer
    template <typename _Tp>
    struct is_pointer : public __is_pointer_helper<typename remove_cv<_Tp>::type>::type {};
    /// is_lvalue_reference
    template <typename> struct is_lvalue_reference : public false_type {};
    template <typename _Tp> struct is_lvalue_reference<_Tp &> : public true_type {};
    /// is_rvalue_reference
    template <typename> struct is_rvalue_reference : public false_type {};
    template <typename _Tp> struct is_rvalue_reference<_Tp &&> : public true_type {};
    template <typename> struct is_function;
    template <typename> struct __is_member_object_pointer_helper : public false_type {};
    template <typename _Tp, typename _Cp>
    struct __is_member_object_pointer_helper<_Tp _Cp::*> : public __not_<is_function<_Tp>>::type {};
    /// is_member_object_pointer
    template <typename _Tp>
    struct is_member_object_pointer
        : public __is_member_object_pointer_helper<typename remove_cv<_Tp>::type>::type {};
    template <typename> struct __is_member_function_pointer_helper : public false_type {};
    template <typename _Tp, typename _Cp>
    struct __is_member_function_pointer_helper<_Tp _Cp::*> : public is_function<_Tp>::type {};
    /// is_member_function_pointer
    template <typename _Tp>
    struct is_member_function_pointer
        : public __is_member_function_pointer_helper<typename remove_cv<_Tp>::type>::type {};
    /// is_enum
    template <typename _Tp> struct is_enum : public integral_constant<bool, __is_enum(_Tp)> {};
    /// is_union
    template <typename _Tp> struct is_union : public integral_constant<bool, __is_union(_Tp)> {};
    /// is_class
    template <typename _Tp> struct is_class : public integral_constant<bool, __is_class(_Tp)> {};
    /// is_function
    template <typename> struct is_function : public false_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...)> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...)> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) volatile> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) volatile &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) volatile &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) volatile> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) volatile &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) volatile &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const volatile> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const volatile &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes...) const volatile &&> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const volatile> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const volatile &> : public true_type {};
    template <typename _Res, typename... _ArgTypes>
    struct is_function<_Res(_ArgTypes..., ...) const volatile &&> : public true_type {};
#define __cpp_lib_is_null_pointer 201309
    template <typename> struct __is_null_pointer_helper : public false_type {};
    template <> struct __is_null_pointer_helper<nullptr_t> : public true_type {};
    /// is_null_pointer (LWG 2247).
    template <typename _Tp>
    struct is_null_pointer : public __is_null_pointer_helper<typename remove_cv<_Tp>::type>::type {
    };
    /// __is_nullptr_t (extension).
    template <typename _Tp> struct __is_nullptr_t : public is_null_pointer<_Tp> {};
    // Composite type categories.
    /// is_reference
    template <typename _Tp>
    struct is_reference : public __or_<is_lvalue_reference<_Tp>, is_rvalue_reference<_Tp>>::type {};
    /// is_arithmetic
    template <typename _Tp>
    struct is_arithmetic : public __or_<is_integral<_Tp>, is_floating_point<_Tp>>::type {};
    /// is_fundamental
    template <typename _Tp>
    struct is_fundamental
        : public __or_<is_arithmetic<_Tp>, is_void<_Tp>, is_null_pointer<_Tp>>::type {};
    /// is_object
    template <typename _Tp>
    struct is_object
        : public __not_<__or_<is_function<_Tp>, is_reference<_Tp>, is_void<_Tp>>>::type {};
    template <typename> struct is_member_pointer;
    /// is_scalar
    template <typename _Tp>
    struct is_scalar : public __or_<is_arithmetic<_Tp>, is_enum<_Tp>, is_pointer<_Tp>,
                                    is_member_pointer<_Tp>, is_null_pointer<_Tp>>::type {};
    /// is_compound
    template <typename _Tp> struct is_compound : public __not_<is_fundamental<_Tp>>::type {};
    template <typename _Tp> struct __is_member_pointer_helper : public false_type {};
    template <typename _Tp, typename _Cp>
    struct __is_member_pointer_helper<_Tp _Cp::*> : public true_type {};
    /// is_member_pointer
    template <typename _Tp>
    struct is_member_pointer
        : public __is_member_pointer_helper<typename remove_cv<_Tp>::type>::type {};
    // Utility to detect referenceable types ([defns.referenceable]).
    template <typename _Tp>
    struct __is_referenceable : public __or_<is_object<_Tp>, is_reference<_Tp>>::type {};
    template <typename _Res, typename... _Args>
    struct __is_referenceable<_Res(_Args...)> : public true_type {};
    template <typename _Res, typename... _Args>
    struct __is_referenceable<_Res(_Args..., ...)> : public true_type {};
    // Type properties.
    /// is_const
    template <typename> struct is_const : public false_type {};
    template <typename _Tp> struct is_const<_Tp const> : public true_type {};
    /// is_volatile
    template <typename> struct is_volatile : public false_type {};
    template <typename _Tp> struct is_volatile<_Tp volatile> : public true_type {};
    /// is_trivial
    template <typename _Tp>
    struct is_trivial : public integral_constant<bool, __is_trivial(_Tp)> {};
    // is_trivially_copyable
    template <typename _Tp>
    struct is_trivially_copyable : public integral_constant<bool, __is_trivially_copyable(_Tp)> {};
    /// is_standard_layout
    template <typename _Tp>
    struct is_standard_layout : public integral_constant<bool, __is_standard_layout(_Tp)> {};
    /// is_pod
    // Could use is_standard_layout && is_trivial instead of the builtin.
    template <typename _Tp> struct is_pod : public integral_constant<bool, __is_pod(_Tp)> {};
    /// is_literal_type
    template <typename _Tp>
    struct is_literal_type : public integral_constant<bool, __is_literal_type(_Tp)> {};
    /// is_empty
    template <typename _Tp> struct is_empty : public integral_constant<bool, __is_empty(_Tp)> {};
    /// is_polymorphic
    template <typename _Tp>
    struct is_polymorphic : public integral_constant<bool, __is_polymorphic(_Tp)> {};
#if __cplusplus >= 201402L
#define __cpp_lib_is_final 201402L
    /// is_final
    template <typename _Tp> struct is_final : public integral_constant<bool, __is_final(_Tp)> {};
#endif
    /// is_abstract
    template <typename _Tp>
    struct is_abstract : public integral_constant<bool, __is_abstract(_Tp)> {};
    template <typename _Tp, bool = is_arithmetic<_Tp>::value>
    struct __is_signed_helper : public false_type {};
    template <typename _Tp>
    struct __is_signed_helper<_Tp, true> : public integral_constant<bool, _Tp(-1) < _Tp(0)> {};
    /// is_signed
    template <typename _Tp> struct is_signed : public __is_signed_helper<_Tp>::type {};
    /// is_unsigned
    template <typename _Tp>
    struct is_unsigned : public __and_<is_arithmetic<_Tp>, __not_<is_signed<_Tp>>> {};
    // Destructible and constructible type properties.
    /**
     *  @brief  Utility to simplify expressions used in unevaluated operands
     *  @ingroup utilities
     */
    template <typename _Tp, typename _Up = _Tp &&> _Up __declval(int);
    template <typename _Tp> _Tp __declval(long);
    template <typename _Tp> auto declval() noexcept -> decltype(__declval<_Tp>(0));
    template <typename, unsigned = 0> struct extent;
    template <typename> struct remove_all_extents;
    template <typename _Tp>
    struct __is_array_known_bounds : public integral_constant<bool, (extent<_Tp>::value > 0)> {};
    template <typename _Tp>
    struct __is_array_unknown_bounds : public __and_<is_array<_Tp>, __not_<extent<_Tp>>> {};
    // In N3290 is_destructible does not say anything about function
    // types and abstract types, see LWG 2049. This implementation
    // describes function types as non-destructible and all complete
    // object types as destructible, iff the explicit destructor
    // call expression is wellformed.
    struct __do_is_destructible_impl {
        template <typename _Tp, typename = decltype(declval<_Tp &>().~_Tp())>
        static true_type __test(int);
        template <typename> static false_type __test(...);
    };
    template <typename _Tp> struct __is_destructible_impl : public __do_is_destructible_impl {
        typedef decltype(__test<_Tp>(0)) type;
    };
    template <typename _Tp,
              bool = __or_<is_void<_Tp>, __is_array_unknown_bounds<_Tp>, is_function<_Tp>>::value,
              bool = __or_<is_reference<_Tp>, is_scalar<_Tp>>::value>
    struct __is_destructible_safe;
    template <typename _Tp>
    struct __is_destructible_safe<_Tp, false, false>
        : public __is_destructible_impl<typename remove_all_extents<_Tp>::type>::type {};
    template <typename _Tp> struct __is_destructible_safe<_Tp, true, false> : public false_type {};
    template <typename _Tp> struct __is_destructible_safe<_Tp, false, true> : public true_type {};
    /// is_destructible
    template <typename _Tp> struct is_destructible : public __is_destructible_safe<_Tp>::type {};
    // is_nothrow_destructible requires that is_destructible is
    // satisfied as well.  We realize that by mimicing the
    // implementation of is_destructible but refer to noexcept(expr)
    // instead of decltype(expr).
    struct __do_is_nt_destructible_impl {
        template <typename _Tp>
        static __bool_constant<noexcept(declval<_Tp &>().~_Tp())> __test(int);
        template <typename> static false_type __test(...);
    };
    template <typename _Tp> struct __is_nt_destructible_impl : public __do_is_nt_destructible_impl {
        typedef decltype(__test<_Tp>(0)) type;
    };
    template <typename _Tp,
              bool = __or_<is_void<_Tp>, __is_array_unknown_bounds<_Tp>, is_function<_Tp>>::value,
              bool = __or_<is_reference<_Tp>, is_scalar<_Tp>>::value>
    struct __is_nt_destructible_safe;
    template <typename _Tp>
    struct __is_nt_destructible_safe<_Tp, false, false>
        : public __is_nt_destructible_impl<typename remove_all_extents<_Tp>::type>::type {};
    template <typename _Tp>
    struct __is_nt_destructible_safe<_Tp, true, false> : public false_type {};
    template <typename _Tp>
    struct __is_nt_destructible_safe<_Tp, false, true> : public true_type {};
    /// is_nothrow_destructible
    template <typename _Tp>
    struct is_nothrow_destructible : public __is_nt_destructible_safe<_Tp>::type {};
    /// is_constructible
    template <typename _Tp, typename... _Args>
    struct is_constructible : public __bool_constant<__is_constructible(_Tp, _Args...)> {};
    /// is_default_constructible
    template <typename _Tp> struct is_default_constructible : public is_constructible<_Tp>::type {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_copy_constructible_impl;
    template <typename _Tp> struct __is_copy_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_copy_constructible_impl<_Tp, true> : public is_constructible<_Tp, const _Tp &> {};
    /// is_copy_constructible
    template <typename _Tp>
    struct is_copy_constructible : public __is_copy_constructible_impl<_Tp> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_move_constructible_impl;
    template <typename _Tp> struct __is_move_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_move_constructible_impl<_Tp, true> : public is_constructible<_Tp, _Tp &&> {};
    /// is_move_constructible
    template <typename _Tp>
    struct is_move_constructible : public __is_move_constructible_impl<_Tp> {};
    template <bool, typename _Tp, typename... _Args>
    struct __is_nt_constructible_impl : public false_type {};
    template <typename _Tp, typename... _Args>
    struct __is_nt_constructible_impl<true, _Tp, _Args...>
        : public __bool_constant<noexcept(_Tp(declval<_Args>()...))> {};
    template <typename _Tp, typename _Arg>
    struct __is_nt_constructible_impl<true, _Tp, _Arg>
        : public __bool_constant<noexcept(static_cast<_Tp>(declval<_Arg>()))> {};
    template <typename _Tp>
    struct __is_nt_constructible_impl<true, _Tp> : public __bool_constant<noexcept(_Tp())> {};
    template <typename _Tp, size_t _Num>
    struct __is_nt_constructible_impl<true, _Tp[_Num]>
        : public __bool_constant<noexcept(typename remove_all_extents<_Tp>::type())> {};
    template <typename _Tp, typename... _Args>
    using __is_nothrow_constructible_impl =
        __is_nt_constructible_impl<__is_constructible(_Tp, _Args...), _Tp, _Args...>;
    /// is_nothrow_constructible
    template <typename _Tp, typename... _Args>
    struct is_nothrow_constructible : public __is_nothrow_constructible_impl<_Tp, _Args...>::type {
    };
    /// is_nothrow_default_constructible
    template <typename _Tp>
    struct is_nothrow_default_constructible : public __is_nothrow_constructible_impl<_Tp>::type {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_nothrow_copy_constructible_impl;
    template <typename _Tp>
    struct __is_nothrow_copy_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_nothrow_copy_constructible_impl<_Tp, true>
        : public is_nothrow_constructible<_Tp, const _Tp &> {};
    /// is_nothrow_copy_constructible
    template <typename _Tp>
    struct is_nothrow_copy_constructible : public __is_nothrow_copy_constructible_impl<_Tp> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_nothrow_move_constructible_impl;
    template <typename _Tp>
    struct __is_nothrow_move_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_nothrow_move_constructible_impl<_Tp, true>
        : public is_nothrow_constructible<_Tp, _Tp &&> {};
    /// is_nothrow_move_constructible
    template <typename _Tp>
    struct is_nothrow_move_constructible : public __is_nothrow_move_constructible_impl<_Tp> {};
    /// is_assignable
    template <typename _Tp, typename _Up>
    struct is_assignable : public __bool_constant<__is_assignable(_Tp, _Up)> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value> struct __is_copy_assignable_impl;
    template <typename _Tp> struct __is_copy_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_copy_assignable_impl<_Tp, true> : public is_assignable<_Tp &, const _Tp &> {};
    /// is_copy_assignable
    template <typename _Tp> struct is_copy_assignable : public __is_copy_assignable_impl<_Tp> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value> struct __is_move_assignable_impl;
    template <typename _Tp> struct __is_move_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_move_assignable_impl<_Tp, true> : public is_assignable<_Tp &, _Tp &&> {};
    /// is_move_assignable
    template <typename _Tp> struct is_move_assignable : public __is_move_assignable_impl<_Tp> {};
    template <typename _Tp, typename _Up>
    struct __is_nt_assignable_impl
        : public integral_constant<bool, noexcept(declval<_Tp>() = declval<_Up>())> {};
    /// is_nothrow_assignable
    template <typename _Tp, typename _Up>
    struct is_nothrow_assignable
        : public __and_<is_assignable<_Tp, _Up>, __is_nt_assignable_impl<_Tp, _Up>> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_nt_copy_assignable_impl;
    template <typename _Tp> struct __is_nt_copy_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_nt_copy_assignable_impl<_Tp, true>
        : public is_nothrow_assignable<_Tp &, const _Tp &> {};
    /// is_nothrow_copy_assignable
    template <typename _Tp>
    struct is_nothrow_copy_assignable : public __is_nt_copy_assignable_impl<_Tp> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_nt_move_assignable_impl;
    template <typename _Tp> struct __is_nt_move_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_nt_move_assignable_impl<_Tp, true> : public is_nothrow_assignable<_Tp &, _Tp &&> {};
    /// is_nothrow_move_assignable
    template <typename _Tp>
    struct is_nothrow_move_assignable : public __is_nt_move_assignable_impl<_Tp> {};
    /// is_trivially_constructible
    template <typename _Tp, typename... _Args>
    struct is_trivially_constructible
        : public __bool_constant<__is_trivially_constructible(_Tp, _Args...)> {};
    /// is_trivially_default_constructible
    template <typename _Tp>
    struct is_trivially_default_constructible : public is_trivially_constructible<_Tp>::type {};
    struct __do_is_implicitly_default_constructible_impl {
        template <typename _Tp> static void __helper(const _Tp &);
        template <typename _Tp>
        static true_type __test(const _Tp &, decltype(__helper<const _Tp &>({})) * = 0);
        static false_type __test(...);
    };
    template <typename _Tp>
    struct __is_implicitly_default_constructible_impl
        : public __do_is_implicitly_default_constructible_impl {
        typedef decltype(__test(declval<_Tp>())) type;
    };
    template <typename _Tp>
    struct __is_implicitly_default_constructible_safe
        : public __is_implicitly_default_constructible_impl<_Tp>::type {};
    template <typename _Tp>
    struct __is_implicitly_default_constructible
        : public __and_<is_default_constructible<_Tp>,
                        __is_implicitly_default_constructible_safe<_Tp>> {};
    /// is_trivially_copy_constructible
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_trivially_copy_constructible_impl;
    template <typename _Tp>
    struct __is_trivially_copy_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_trivially_copy_constructible_impl<_Tp, true>
        : public __and_<is_copy_constructible<_Tp>,
                        integral_constant<bool, __is_trivially_constructible(_Tp, const _Tp &)>> {};
    template <typename _Tp>
    struct is_trivially_copy_constructible : public __is_trivially_copy_constructible_impl<_Tp> {};
    /// is_trivially_move_constructible
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_trivially_move_constructible_impl;
    template <typename _Tp>
    struct __is_trivially_move_constructible_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_trivially_move_constructible_impl<_Tp, true>
        : public __and_<is_move_constructible<_Tp>,
                        integral_constant<bool, __is_trivially_constructible(_Tp, _Tp &&)>> {};
    template <typename _Tp>
    struct is_trivially_move_constructible : public __is_trivially_move_constructible_impl<_Tp> {};
    /// is_trivially_assignable
    template <typename _Tp, typename _Up>
    struct is_trivially_assignable : public __bool_constant<__is_trivially_assignable(_Tp, _Up)> {};
    /// is_trivially_copy_assignable
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_trivially_copy_assignable_impl;
    template <typename _Tp>
    struct __is_trivially_copy_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_trivially_copy_assignable_impl<_Tp, true>
        : public __bool_constant<__is_trivially_assignable(_Tp &, const _Tp &)> {};
    template <typename _Tp>
    struct is_trivially_copy_assignable : public __is_trivially_copy_assignable_impl<_Tp> {};
    /// is_trivially_move_assignable
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __is_trivially_move_assignable_impl;
    template <typename _Tp>
    struct __is_trivially_move_assignable_impl<_Tp, false> : public false_type {};
    template <typename _Tp>
    struct __is_trivially_move_assignable_impl<_Tp, true>
        : public __bool_constant<__is_trivially_assignable(_Tp &, _Tp &&)> {};
    template <typename _Tp>
    struct is_trivially_move_assignable : public __is_trivially_move_assignable_impl<_Tp> {};
    /// is_trivially_destructible
    template <typename _Tp>
    struct is_trivially_destructible
        : public __and_<is_destructible<_Tp>, __bool_constant<__has_trivial_destructor(_Tp)>> {};
    /// has_virtual_destructor
    template <typename _Tp>
    struct has_virtual_destructor : public integral_constant<bool, __has_virtual_destructor(_Tp)> {
    };
    // type property queries.
    /// alignment_of
    template <typename _Tp> struct alignment_of : public integral_constant<size_t, alignof(_Tp)> {};
    /// rank
    template <typename> struct rank : public integral_constant<size_t, 0> {};
    template <typename _Tp, size_t _Size>
    struct rank<_Tp[_Size]> : public integral_constant<size_t, 1 + rank<_Tp>::value> {};
    template <typename _Tp>
    struct rank<_Tp[]> : public integral_constant<size_t, 1 + rank<_Tp>::value> {};
    /// extent
    template <typename, unsigned _Uint> struct extent : public integral_constant<size_t, 0> {};
    template <typename _Tp, unsigned _Uint, size_t _Size>
    struct extent<_Tp[_Size], _Uint>
        : public integral_constant<size_t, _Uint == 0 ? _Size : extent<_Tp, _Uint - 1>::value> {};
    template <typename _Tp, unsigned _Uint>
    struct extent<_Tp[], _Uint>
        : public integral_constant<size_t, _Uint == 0 ? 0 : extent<_Tp, _Uint - 1>::value> {};
    // Type relations.
    /// is_same
    template <typename, typename> struct is_same : public false_type {};
    template <typename _Tp> struct is_same<_Tp, _Tp> : public true_type {};
    /// is_base_of
    template <typename _Base, typename _Derived>
    struct is_base_of : public integral_constant<bool, __is_base_of(_Base, _Derived)> {};
    template <typename _From, typename _To,
              bool = __or_<is_void<_From>, is_function<_To>, is_array<_To>>::value>
    struct __is_convertible_helper {
        typedef typename is_void<_To>::type type;
    };
    template <typename _From, typename _To> class __is_convertible_helper<_From, _To, false> {
        template <typename _To1> static void __test_aux(_To1) noexcept;
        template <typename _From1, typename _To1,
                  typename = decltype(__test_aux<_To1>(declval<_From1>()))>
        static true_type __test(int);
        template <typename, typename> static false_type __test(...);

    public:
        typedef decltype(__test<_From, _To>(0)) type;
    };
    /// is_convertible
    template <typename _From, typename _To>
    struct is_convertible : public __is_convertible_helper<_From, _To>::type {};
#if __cplusplus > 201703L
    template <typename _From, typename _To,
              bool = __or_<is_void<_From>, is_function<_To>, is_array<_To>>::value>
    struct __is_nt_convertible_helper : is_void<_To> {};
    template <typename _From, typename _To> class __is_nt_convertible_helper<_From, _To, false> {
        template <typename _To1> static void __test_aux(_To1) noexcept;
        template <typename _From1, typename _To1>
        static bool_constant<noexcept(__test_aux<_To1>(declval<_From1>()))> __test(int);
        template <typename, typename> static false_type __test(...);

    public:
        using type = decltype(__test<_From, _To>(0));
    };
#define __cpp_lib_is_nothrow_convertible 201806L
    /// is_nothrow_convertible
    template <typename _From, typename _To>
    struct is_nothrow_convertible : public __is_nt_convertible_helper<_From, _To>::type {};
    /// is_nothrow_convertible_v
    template <typename _From, typename _To>
    inline constexpr bool is_nothrow_convertible_v = is_nothrow_convertible<_From, _To>::value;
#endif  // C++2a
        // Const-volatile modifications.
        /// remove_const
    template <typename _Tp> struct remove_const {
        typedef _Tp type;
    };
    template <typename _Tp> struct remove_const<_Tp const> {
        typedef _Tp type;
    };
    /// remove_volatile
    template <typename _Tp> struct remove_volatile {
        typedef _Tp type;
    };
    template <typename _Tp> struct remove_volatile<_Tp volatile> {
        typedef _Tp type;
    };
    /// remove_cv
    template <typename _Tp> struct remove_cv {
        typedef typename remove_const<typename remove_volatile<_Tp>::type>::type type;
    };
    /// add_const
    template <typename _Tp> struct add_const {
        typedef _Tp const type;
    };
    /// add_volatile
    template <typename _Tp> struct add_volatile {
        typedef _Tp volatile type;
    };
    /// add_cv
    template <typename _Tp> struct add_cv {
        typedef typename add_const<typename add_volatile<_Tp>::type>::type type;
    };
#if __cplusplus > 201103L
#define __cpp_lib_transformation_trait_aliases 201304
    /// Alias template for remove_const
    template <typename _Tp> using remove_const_t = typename remove_const<_Tp>::type;
    /// Alias template for remove_volatile
    template <typename _Tp> using remove_volatile_t = typename remove_volatile<_Tp>::type;
    /// Alias template for remove_cv
    template <typename _Tp> using remove_cv_t = typename remove_cv<_Tp>::type;
    /// Alias template for add_const
    template <typename _Tp> using add_const_t = typename add_const<_Tp>::type;
    /// Alias template for add_volatile
    template <typename _Tp> using add_volatile_t = typename add_volatile<_Tp>::type;
    /// Alias template for add_cv
    template <typename _Tp> using add_cv_t = typename add_cv<_Tp>::type;
#endif
    // Reference transformations.
    /// remove_reference
    template <typename _Tp> struct remove_reference {
        typedef _Tp type;
    };
    template <typename _Tp> struct remove_reference<_Tp &> {
        typedef _Tp type;
    };
    template <typename _Tp> struct remove_reference<_Tp &&> {
        typedef _Tp type;
    };
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __add_lvalue_reference_helper {
        typedef _Tp type;
    };
    template <typename _Tp> struct __add_lvalue_reference_helper<_Tp, true> {
        typedef _Tp &type;
    };
    /// add_lvalue_reference
    template <typename _Tp>
    struct add_lvalue_reference : public __add_lvalue_reference_helper<_Tp> {};
    template <typename _Tp, bool = __is_referenceable<_Tp>::value>
    struct __add_rvalue_reference_helper {
        typedef _Tp type;
    };
    template <typename _Tp> struct __add_rvalue_reference_helper<_Tp, true> {
        typedef _Tp &&type;
    };
    /// add_rvalue_reference
    template <typename _Tp>
    struct add_rvalue_reference : public __add_rvalue_reference_helper<_Tp> {};
#if __cplusplus > 201103L
    /// Alias template for remove_reference
    template <typename _Tp> using remove_reference_t = typename remove_reference<_Tp>::type;
    /// Alias template for add_lvalue_reference
    template <typename _Tp> using add_lvalue_reference_t = typename add_lvalue_reference<_Tp>::type;
    /// Alias template for add_rvalue_reference
    template <typename _Tp> using add_rvalue_reference_t = typename add_rvalue_reference<_Tp>::type;
#endif
    // Sign modifications.
    // Utility for constructing identically cv-qualified types.
    template <typename _Unqualified, bool _IsConst, bool _IsVol> struct __cv_selector;
    template <typename _Unqualified> struct __cv_selector<_Unqualified, false, false> {
        typedef _Unqualified __type;
    };
    template <typename _Unqualified> struct __cv_selector<_Unqualified, false, true> {
        typedef volatile _Unqualified __type;
    };
    template <typename _Unqualified> struct __cv_selector<_Unqualified, true, false> {
        typedef const _Unqualified __type;
    };
    template <typename _Unqualified> struct __cv_selector<_Unqualified, true, true> {
        typedef const volatile _Unqualified __type;
    };
    template <typename _Qualified, typename _Unqualified,
              bool _IsConst = is_const<_Qualified>::value,
              bool _IsVol   = is_volatile<_Qualified>::value>
    class __match_cv_qualifiers {
        typedef __cv_selector<_Unqualified, _IsConst, _IsVol> __match;

    public:
        typedef typename __match::__type __type;
    };
    // Utility for finding the unsigned versions of signed integral types.
    template <typename _Tp> struct __make_unsigned {
        typedef _Tp __type;
    };
    template <> struct __make_unsigned<char> {
        typedef unsigned char __type;
    };
    template <> struct __make_unsigned<signed char> {
        typedef unsigned char __type;
    };
    template <> struct __make_unsigned<short> {
        typedef unsigned short __type;
    };
    template <> struct __make_unsigned<int> {
        typedef unsigned int __type;
    };
    template <> struct __make_unsigned<long> {
        typedef unsigned long __type;
    };
    template <> struct __make_unsigned<long long> {
        typedef unsigned long long __type;
    };
    // Select between integral and enum: not possible to be both.
    template <typename _Tp, bool _IsInt = is_integral<_Tp>::value,
              bool _IsEnum = is_enum<_Tp>::value>
    class __make_unsigned_selector;
    template <typename _Tp> class __make_unsigned_selector<_Tp, true, false> {
        using __unsigned_type = typename __make_unsigned<typename remove_cv<_Tp>::type>::__type;

    public:
        using __type = typename __match_cv_qualifiers<_Tp, __unsigned_type>::__type;
    };
    class __make_unsigned_selector_base {
    protected:
        template <typename...> struct _List {};
        template <typename _Tp, typename... _Up> struct _List<_Tp, _Up...> : _List<_Up...> {
            static constexpr size_t __size = sizeof(_Tp);
        };
        template <size_t _Sz, typename _Tp, bool = (_Sz <= _Tp::__size)> struct __select;
        template <size_t _Sz, typename _Uint, typename... _UInts>
        struct __select<_Sz, _List<_Uint, _UInts...>, true> {
            using __type = _Uint;
        };
        template <size_t _Sz, typename _Uint, typename... _UInts>
        struct __select<_Sz, _List<_Uint, _UInts...>, false> : __select<_Sz, _List<_UInts...>> {};
    };
    // Choose unsigned integer type with the smallest rank and same size as _Tp
    template <typename _Tp>
    class __make_unsigned_selector<_Tp, false, true> : __make_unsigned_selector_base {
        // With -fshort-enums, an enum may be as small as a char.
        using _UInts =
            _List<unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long>;
        using __unsigned_type = typename __select<sizeof(_Tp), _UInts>::__type;

    public:
        using __type = typename __match_cv_qualifiers<_Tp, __unsigned_type>::__type;
    };
    // wchar_t, char8_t, char16_t and char32_t are integral types but are
    // neither signed integer types nor unsigned integer types, so must be
    // transformed to the unsigned integer type with the smallest rank.
    // Use the partial specialization for enumeration types to do that.
    template <> struct __make_unsigned<char16_t> {
        using __type = typename __make_unsigned_selector<char16_t, false, true>::__type;
    };
    template <> struct __make_unsigned<char32_t> {
        using __type = typename __make_unsigned_selector<char32_t, false, true>::__type;
    };
    // Given an integral/enum type, return the corresponding unsigned
    // integer type.
    // Primary template.
    /// make_unsigned
    template <typename _Tp> struct make_unsigned {
        typedef typename __make_unsigned_selector<_Tp>::__type type;
    };
    // Integral, but don't define.
    template <> struct make_unsigned<bool>;
    // Utility for finding the signed versions of unsigned integral types.
    template <typename _Tp> struct __make_signed {
        typedef _Tp __type;
    };
    template <> struct __make_signed<char> {
        typedef signed char __type;
    };
    template <> struct __make_signed<unsigned char> {
        typedef signed char __type;
    };
    template <> struct __make_signed<unsigned short> {
        typedef signed short __type;
    };
    template <> struct __make_signed<unsigned int> {
        typedef signed int __type;
    };
    template <> struct __make_signed<unsigned long> {
        typedef signed long __type;
    };
    template <> struct __make_signed<unsigned long long> {
        typedef signed long long __type;
    };
    // Select between integral and enum: not possible to be both.
    template <typename _Tp, bool _IsInt = is_integral<_Tp>::value,
              bool _IsEnum = is_enum<_Tp>::value>
    class __make_signed_selector;
    template <typename _Tp> class __make_signed_selector<_Tp, true, false> {
        using __signed_type = typename __make_signed<typename remove_cv<_Tp>::type>::__type;

    public:
        using __type = typename __match_cv_qualifiers<_Tp, __signed_type>::__type;
    };
    // Choose signed integer type with the smallest rank and same size as _Tp
    template <typename _Tp> class __make_signed_selector<_Tp, false, true> {
        typedef typename __make_unsigned_selector<_Tp>::__type __unsigned_type;

    public:
        typedef typename __make_signed_selector<__unsigned_type>::__type __type;
    };
    // wchar_t, char16_t and char32_t are integral types but are neither
    // signed integer types nor unsigned integer types, so must be
    // transformed to the signed integer type with the smallest rank.
    // Use the partial specialization for enumeration types to do that.
    template <> struct __make_signed<char16_t> {
        using __type = typename __make_signed_selector<char16_t, false, true>::__type;
    };
    template <> struct __make_signed<char32_t> {
        using __type = typename __make_signed_selector<char32_t, false, true>::__type;
    };
    // Given an integral/enum type, return the corresponding signed
    // integer type.
    // Primary template.
    /// make_signed
    template <typename _Tp> struct make_signed {
        typedef typename __make_signed_selector<_Tp>::__type type;
    };
    // Integral, but don't define.
    template <> struct make_signed<bool>;
#if __cplusplus > 201103L
    /// Alias template for make_signed
    template <typename _Tp> using make_signed_t = typename make_signed<_Tp>::type;
    /// Alias template for make_unsigned
    template <typename _Tp> using make_unsigned_t = typename make_unsigned<_Tp>::type;
#endif
    // Array modifications.
    /// remove_extent
    template <typename _Tp> struct remove_extent {
        typedef _Tp type;
    };
    template <typename _Tp, size_t _Size> struct remove_extent<_Tp[_Size]> {
        typedef _Tp type;
    };
    template <typename _Tp> struct remove_extent<_Tp[]> {
        typedef _Tp type;
    };
    /// remove_all_extents
    template <typename _Tp> struct remove_all_extents {
        typedef _Tp type;
    };
    template <typename _Tp, size_t _Size> struct remove_all_extents<_Tp[_Size]> {
        typedef typename remove_all_extents<_Tp>::type type;
    };
    template <typename _Tp> struct remove_all_extents<_Tp[]> {
        typedef typename remove_all_extents<_Tp>::type type;
    };
#if __cplusplus > 201103L
    /// Alias template for remove_extent
    template <typename _Tp> using remove_extent_t = typename remove_extent<_Tp>::type;
    /// Alias template for remove_all_extents
    template <typename _Tp> using remove_all_extents_t = typename remove_all_extents<_Tp>::type;
#endif
    // Pointer modifications.
    template <typename _Tp, typename> struct __remove_pointer_helper {
        typedef _Tp type;
    };
    template <typename _Tp, typename _Up> struct __remove_pointer_helper<_Tp, _Up *> {
        typedef _Up type;
    };
    /// remove_pointer
    template <typename _Tp>
    struct remove_pointer : public __remove_pointer_helper<_Tp, typename remove_cv<_Tp>::type> {};
    /// add_pointer
    template <typename _Tp, bool = __or_<__is_referenceable<_Tp>, is_void<_Tp>>::value>
    struct __add_pointer_helper {
        typedef _Tp type;
    };
    template <typename _Tp> struct __add_pointer_helper<_Tp, true> {
        typedef typename remove_reference<_Tp>::type *type;
    };
    template <typename _Tp> struct add_pointer : public __add_pointer_helper<_Tp> {};
#if __cplusplus > 201103L
    /// Alias template for remove_pointer
    template <typename _Tp> using remove_pointer_t = typename remove_pointer<_Tp>::type;
    /// Alias template for add_pointer
    template <typename _Tp> using add_pointer_t = typename add_pointer<_Tp>::type;
#endif
    template <size_t _Len> struct __aligned_storage_msa {
        union __type {
            unsigned char __data[_Len];
            struct __attribute__((__aligned__)) {
            } __align;
        };
    };
    /**
     *  @brief Alignment type.
     *
     *  The value of _Align is a default-alignment which shall be the
     *  most stringent alignment requirement for any C++ object type
     *  whose size is no greater than _Len (3.9). The member typedef
     *  type shall be a POD type suitable for use as uninitialized
     *  storage for any object whose size is at most _Len and whose
     *  alignment is a divisor of _Align.
     */
    template <size_t _Len,
              size_t _Align = __alignof__(typename __aligned_storage_msa<_Len>::__type)>
    struct aligned_storage {
        union type {
            unsigned char __data[_Len];
            struct __attribute__((__aligned__((_Align)))) {
            } __align;
        };
    };
    template <typename... _Types> struct __strictest_alignment {
        static const size_t _S_alignment = 0;
        static const size_t _S_size      = 0;
    };
    template <typename _Tp, typename... _Types> struct __strictest_alignment<_Tp, _Types...> {
        static const size_t _S_alignment = alignof(_Tp) >
                                                   __strictest_alignment<_Types...>::_S_alignment
                                               ? alignof(_Tp)
                                               : __strictest_alignment<_Types...>::_S_alignment;
        static const size_t _S_size      = sizeof(_Tp) > __strictest_alignment<_Types...>::_S_size
                                               ? sizeof(_Tp)
                                               : __strictest_alignment<_Types...>::_S_size;
    };
    /**
     *  @brief Provide aligned storage for types.
     *
     *  [meta.trans.other]
     *
     *  Provides aligned storage for any of the provided types of at
     *  least size _Len.
     *
     *  @see aligned_storage
     */
    template <size_t _Len, typename... _Types> struct aligned_union {
    private:
        static_assert(sizeof...(_Types) != 0, "At least one type is required");
        using __strictest          = __strictest_alignment<_Types...>;
        static const size_t _S_len = _Len > __strictest::_S_size ? _Len : __strictest::_S_size;

    public:
        /// The value of the strictest alignment of _Types.
        static const size_t alignment_value = __strictest::_S_alignment;
        /// The storage.
        typedef typename aligned_storage<_S_len, alignment_value>::type type;
    };
    template <size_t _Len, typename... _Types>
    const size_t aligned_union<_Len, _Types...>::alignment_value;
    // Decay trait for arrays and functions, used for perfect forwarding
    // in make_pair, make_tuple, etc.
    template <typename _Up, bool _IsArray = is_array<_Up>::value,
              bool _IsFunction = is_function<_Up>::value>
    struct __decay_selector;
    // NB: DR 705.
    template <typename _Up> struct __decay_selector<_Up, false, false> {
        typedef typename remove_cv<_Up>::type __type;
    };
    template <typename _Up> struct __decay_selector<_Up, true, false> {
        typedef typename remove_extent<_Up>::type *__type;
    };
    template <typename _Up> struct __decay_selector<_Up, false, true> {
        typedef typename add_pointer<_Up>::type __type;
    };
    /// decay
    template <typename _Tp> class decay {
        typedef typename remove_reference<_Tp>::type __remove_type;

    public:
        typedef typename __decay_selector<__remove_type>::__type type;
    };
    template <typename _Tp> class reference_wrapper;
    // Helper which adds a reference to a type when given a reference_wrapper
    template <typename _Tp> struct __strip_reference_wrapper {
        typedef _Tp __type;
    };
    template <typename _Tp> struct __strip_reference_wrapper<reference_wrapper<_Tp>> {
        typedef _Tp &__type;
    };
    template <typename _Tp> struct __decay_and_strip {
        typedef typename __strip_reference_wrapper<typename decay<_Tp>::type>::__type __type;
    };
    // Primary template.
    /// Define a member typedef @c type only if a boolean constant is true.
    template <bool, typename _Tp = void> struct enable_if {};
    // Partial specialization for true.
    template <typename _Tp> struct enable_if<true, _Tp> {
        typedef _Tp type;
    };
    template <typename... _Cond> using _Require = typename enable_if<__and_<_Cond...>::value>::type;
    // Primary template.
    /// Define a member typedef @c type to one of two argument types.
    template <bool _Cond, typename _Iftrue, typename _Iffalse> struct conditional {
        typedef _Iftrue type;
    };
    // Partial specialization for false.
    template <typename _Iftrue, typename _Iffalse> struct conditional<false, _Iftrue, _Iffalse> {
        typedef _Iffalse type;
    };
    /// common_type
    template <typename... _Tp> struct common_type;
    // Sfinae-friendly common_type implementation:
    struct __do_common_type_impl {
        template <typename _Tp, typename _Up>
        static __success_type<
            typename decay<decltype(true ? declval<_Tp>() : declval<_Up>())>::type>
        _S_test(int);
        template <typename, typename> static __failure_type _S_test(...);
    };
    template <typename _Tp, typename _Up>
    struct __common_type_impl : private __do_common_type_impl {
        typedef decltype(_S_test<_Tp, _Up>(0)) type;
    };
    struct __do_member_type_wrapper {
        template <typename _Tp> static __success_type<typename _Tp::type> _S_test(int);
        template <typename> static __failure_type _S_test(...);
    };
    template <typename _Tp> struct __member_type_wrapper : private __do_member_type_wrapper {
        typedef decltype(_S_test<_Tp>(0)) type;
    };
    template <typename _CTp, typename... _Args> struct __expanded_common_type_wrapper {
        typedef common_type<typename _CTp::type, _Args...> type;
    };
    template <typename... _Args> struct __expanded_common_type_wrapper<__failure_type, _Args...> {
        typedef __failure_type type;
    };
    template <> struct common_type<> {};
    template <typename _Tp> struct common_type<_Tp> : common_type<_Tp, _Tp> {};
    template <typename _Tp, typename _Up>
    struct common_type<_Tp, _Up> : public __common_type_impl<_Tp, _Up>::type {};
    template <typename _Tp, typename _Up, typename... _Vp>
    struct common_type<_Tp, _Up, _Vp...>
        : public __expanded_common_type_wrapper<
              typename __member_type_wrapper<common_type<_Tp, _Up>>::type, _Vp...>::type {};
    template <typename _Tp, bool = is_enum<_Tp>::value> struct __underlying_type_impl {
        using type = __underlying_type(_Tp);
    };
    template <typename _Tp> struct __underlying_type_impl<_Tp, false> {};
    /// The underlying type of an enum.
    template <typename _Tp> struct underlying_type : public __underlying_type_impl<_Tp> {};
    template <typename _Tp> struct __declval_protector {
        static const bool __stop = false;
    };
    template <class _Tp> _Tp &&__declval(int);
    template <class _Tp> _Tp __declval(long);
    template <typename _Tp> auto declval() noexcept -> decltype(__declval<_Tp>(0)) {
        static_assert(__declval_protector<_Tp>::__stop, "declval() must not be used!");
        return __declval<_Tp>(0);
    }
    // __remove_cvref_t (remove_cvref_t for C++11).
    template <typename _Tp>
    using __remove_cvref_t = typename remove_cv<typename remove_reference<_Tp>::type>::type;
    /// result_of
    template <typename _Signature> class result_of;
    // Sfinae-friendly result_of implementation:
#define __cpp_lib_result_of_sfinae 201210
    struct __invoke_memfun_ref {};
    struct __invoke_memfun_deref {};
    struct __invoke_memobj_ref {};
    struct __invoke_memobj_deref {};
    struct __invoke_other {};
    // Associate a tag type with a specialization of __success_type.
    template <typename _Tp, typename _Tag> struct __result_of_success : __success_type<_Tp> {
        using __invoke_type = _Tag;
    };
    // [func.require] paragraph 1 bullet 1:
    struct __result_of_memfun_ref_impl {
        template <typename _Fp, typename _Tp1, typename... _Args>
        static __result_of_success<decltype((declval<_Tp1>().*declval<_Fp>())(declval<_Args>()...)),
                                   __invoke_memfun_ref>
        _S_test(int);
        template <typename...> static __failure_type _S_test(...);
    };
    template <typename _MemPtr, typename _Arg, typename... _Args>
    struct __result_of_memfun_ref : private __result_of_memfun_ref_impl {
        typedef decltype(_S_test<_MemPtr, _Arg, _Args...>(0)) type;
    };
    // [func.require] paragraph 1 bullet 2:
    struct __result_of_memfun_deref_impl {
        template <typename _Fp, typename _Tp1, typename... _Args>
        static __result_of_success<decltype(((*declval<_Tp1>()).*
                                             declval<_Fp>())(declval<_Args>()...)),
                                   __invoke_memfun_deref>
        _S_test(int);
        template <typename...> static __failure_type _S_test(...);
    };
    template <typename _MemPtr, typename _Arg, typename... _Args>
    struct __result_of_memfun_deref : private __result_of_memfun_deref_impl {
        typedef decltype(_S_test<_MemPtr, _Arg, _Args...>(0)) type;
    };
    // [func.require] paragraph 1 bullet 3:
    struct __result_of_memobj_ref_impl {
        template <typename _Fp, typename _Tp1>
        static __result_of_success<decltype(declval<_Tp1>().*declval<_Fp>()), __invoke_memobj_ref>
        _S_test(int);
        template <typename, typename> static __failure_type _S_test(...);
    };
    template <typename _MemPtr, typename _Arg>
    struct __result_of_memobj_ref : private __result_of_memobj_ref_impl {
        typedef decltype(_S_test<_MemPtr, _Arg>(0)) type;
    };
    // [func.require] paragraph 1 bullet 4:
    struct __result_of_memobj_deref_impl {
        template <typename _Fp, typename _Tp1>
        static __result_of_success<decltype((*declval<_Tp1>()).*declval<_Fp>()),
                                   __invoke_memobj_deref>
        _S_test(int);
        template <typename, typename> static __failure_type _S_test(...);
    };
    template <typename _MemPtr, typename _Arg>
    struct __result_of_memobj_deref : private __result_of_memobj_deref_impl {
        typedef decltype(_S_test<_MemPtr, _Arg>(0)) type;
    };
    template <typename _MemPtr, typename _Arg> struct __result_of_memobj;
    template <typename _Res, typename _Class, typename _Arg>
    struct __result_of_memobj<_Res _Class::*, _Arg> {
        typedef __remove_cvref_t<_Arg> _Argval;
        typedef _Res _Class::*_MemPtr;
        typedef typename conditional<
            __or_<is_same<_Argval, _Class>, is_base_of<_Class, _Argval>>::value,
            __result_of_memobj_ref<_MemPtr, _Arg>,
            __result_of_memobj_deref<_MemPtr, _Arg>>::type::type type;
    };
    template <typename _MemPtr, typename _Arg, typename... _Args> struct __result_of_memfun;
    template <typename _Res, typename _Class, typename _Arg, typename... _Args>
    struct __result_of_memfun<_Res _Class::*, _Arg, _Args...> {
        typedef typename remove_reference<_Arg>::type _Argval;
        typedef _Res _Class::*_MemPtr;
        typedef typename conditional<
            is_base_of<_Class, _Argval>::value, __result_of_memfun_ref<_MemPtr, _Arg, _Args...>,
            __result_of_memfun_deref<_MemPtr, _Arg, _Args...>>::type::type type;
    };
    // _GLIBCXX_RESOLVE_LIB_DEFECTS
    // 2219.  INVOKE-ing a pointer to member with a reference_wrapper
    //        as the object expression
    // Used by result_of, invoke etc. to unwrap a reference_wrapper.
    template <typename _Tp, typename _Up = __remove_cvref_t<_Tp>> struct __inv_unwrap {
        using type = _Tp;
    };
    template <typename _Tp, typename _Up> struct __inv_unwrap<_Tp, reference_wrapper<_Up>> {
        using type = _Up &;
    };
    template <bool, bool, typename _Functor, typename... _ArgTypes> struct __result_of_impl {
        typedef __failure_type type;
    };
    template <typename _MemPtr, typename _Arg>
    struct __result_of_impl<true, false, _MemPtr, _Arg>
        : public __result_of_memobj<typename decay<_MemPtr>::type,
                                    typename __inv_unwrap<_Arg>::type> {};
    template <typename _MemPtr, typename _Arg, typename... _Args>
    struct __result_of_impl<false, true, _MemPtr, _Arg, _Args...>
        : public __result_of_memfun<typename decay<_MemPtr>::type,
                                    typename __inv_unwrap<_Arg>::type, _Args...> {};
    // [func.require] paragraph 1 bullet 5:
    struct __result_of_other_impl {
        template <typename _Fn, typename... _Args>
        static __result_of_success<decltype(declval<_Fn>()(declval<_Args>()...)), __invoke_other>
        _S_test(int);
        template <typename...> static __failure_type _S_test(...);
    };
    template <typename _Functor, typename... _ArgTypes>
    struct __result_of_impl<false, false, _Functor, _ArgTypes...> : private __result_of_other_impl {
        typedef decltype(_S_test<_Functor, _ArgTypes...>(0)) type;
    };
    // __invoke_result (invoke_result for C++11)
    template <typename _Functor, typename... _ArgTypes>
    struct __invoke_result
        : public __result_of_impl<
              is_member_object_pointer<typename remove_reference<_Functor>::type>::value,
              is_member_function_pointer<typename remove_reference<_Functor>::type>::value,
              _Functor, _ArgTypes...>::type {};
    template <typename _Functor, typename... _ArgTypes>
    struct result_of<_Functor(_ArgTypes...)> : public __invoke_result<_Functor, _ArgTypes...> {};
#if __cplusplus >= 201402L
    /// Alias template for aligned_storage
    template <size_t _Len,
              size_t _Align = __alignof__(typename __aligned_storage_msa<_Len>::__type)>
    using aligned_storage_t = typename aligned_storage<_Len, _Align>::type;
    template <size_t _Len, typename... _Types>
    using aligned_union_t = typename aligned_union<_Len, _Types...>::type;
    /// Alias template for decay
    template <typename _Tp> using decay_t = typename decay<_Tp>::type;
    /// Alias template for enable_if
    template <bool _Cond, typename _Tp = void>
    using enable_if_t = typename enable_if<_Cond, _Tp>::type;
    /// Alias template for conditional
    template <bool _Cond, typename _Iftrue, typename _Iffalse>
    using conditional_t = typename conditional<_Cond, _Iftrue, _Iffalse>::type;
    /// Alias template for common_type
    template <typename... _Tp> using common_type_t = typename common_type<_Tp...>::type;
    /// Alias template for underlying_type
    template <typename _Tp> using underlying_type_t = typename underlying_type<_Tp>::type;
    /// Alias template for result_of
    template <typename _Tp> using result_of_t = typename result_of<_Tp>::type;
#endif  // C++14
    // __enable_if_t (enable_if_t for C++11)
    template <bool _Cond, typename _Tp = void>
    using __enable_if_t = typename enable_if<_Cond, _Tp>::type;
    // __void_t (void_t for C++11)
    template <typename...> using __void_t = void;
#if __cplusplus >= 201703L || !defined(__STRICT_ANSI__)  // c++17 or gnu++11
#define __cpp_lib_void_t 201411
    /// A metafunction that always yields void, used for detecting valid types.
    template <typename...> using void_t = void;
#endif
    /// Implementation of the detection idiom (negative case).
    template <typename _Default, typename _AlwaysVoid, template <typename...> class _Op,
              typename... _Args>
    struct __detector {
        using value_t = false_type;
        using type    = _Default;
    };
    /// Implementation of the detection idiom (positive case).
    template <typename _Default, template <typename...> class _Op, typename... _Args>
    struct __detector<_Default, __void_t<_Op<_Args...>>, _Op, _Args...> {
        using value_t = true_type;
        using type    = _Op<_Args...>;
    };
    // Detect whether _Op<_Args...> is a valid type, use _Default if not.
    template <typename _Default, template <typename...> class _Op, typename... _Args>
    using __detected_or = __detector<_Default, void, _Op, _Args...>;
    // _Op<_Args...> if that is a valid type, otherwise _Default.
    template <typename _Default, template <typename...> class _Op, typename... _Args>
    using __detected_or_t = typename __detected_or<_Default, _Op, _Args...>::type;
    /// @} group metaprogramming
}  // namespace JSystem
