#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/PPCArch.h>
#include <Dolphin/printf.h>

#if defined(__GNUC__) || defined(__clang__)
#define SMS_ALIGN(x) __attribute__((aligned(x)))
#elif defined(_MSC_VER)
#define SMS_ALIGN(x) __declspec(align(x))
#else
#error "Unknown compiler; can't define SMS_ALIGN"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SMS_ALIGNOF(X) __alignof__(X)
#elif defined(_MSC_VER)
#define SMS_ALIGNOF(X) __alignof(X)
#else
#error "Unknown compiler; can't define ALIGNOF"
#endif

#define SMS_STRINGIZE_(x) #x
#define SMS_STRINGIZE(x)  SMS_STRINGIZE_(x)

#define SMS_WIDE_(s) L##s
#define SMS_WIDE(s)  SMS_WIDE_(s)

#define SMS_CONCATENATE_(a, b) a##b
#define SMS_CONCATENATE(a, b)  SMS_CONCATENATE_(a, b)

#if !defined(NTSCU) && !defined(PAL) && !defined(NTSCJ) && !defined(NTSCK)
#define NTSCU
#endif

#if defined(NTSCU)
#define SMS_PORT_REGION(ntscu, pal, ntscj, ntsck) ntscu
#elif defined(PAL)
#define SMS_PORT_REGION(ntscu, pal, ntscj, ntsck) pal
#elif defined(NTSCJ)
#define SMS_PORT_REGION(ntscu, pal, ntscj, ntsck) ntscj
#elif defined(NTSCK)
#define SMS_PORT_REGION(ntscu, pal, ntscj, ntsck) ntsck
#else
#error "No region provided!"
#endif

#ifdef __MWERKS__
#define SMS_REGISTER register
#define SMS_ASM_FUNC asm
#define SMS_ASM_BLOCK
#define SMS_PACKED_CLASS(name)  class __attribute__((packed)) name
#define SMS_PACKED_STRUCT(name) struct __attribute__((packed)) name
#else
#define SMS_REGISTER
#ifdef _MSC_VER
#define SMS_ASM_FUNC            __declspec(naked)
#define SMS_ASM_BLOCK           __asm volatile
#define SMS_PACKED_CLASS(name)  __pragma(pack(push, 1)) class name __pragma(pack(pop))
#define SMS_PACKED_STRUCT(name) __pragma(pack(push, 1)) struct name __pragma(pack(pop))
#else
#define SMS_ASM_FUNC            __attribute__((naked))
#define SMS_ASM_BLOCK           asm volatile
#define SMS_PACKED_CLASS(name)  class __attribute__((packed)) name
#define SMS_PACKED_STRUCT(name) struct __attribute__((packed)) name
#endif
#endif

#define SMS_FROM_GPR(reg, var) SMS_ASM_BLOCK("mr %0, " #reg : "=r"(var))
#define SMS_TO_GPR(reg, var)   SMS_ASM_BLOCK("mr " #reg ", %0" : : "r"(var))
#define SMS_FROM_FPR(reg, var) SMS_ASM_BLOCK("fmr %0, " #reg : "=r"(var))
#define SMS_TO_FPR(reg, var)   SMS_ASM_BLOCK("fmr " #reg ", %0" : : "r"(var))

#ifdef __cplusplus
#if __cplusplus >= 201103L
#ifndef offsetof
#define offsetof(t, d) ((size_t) & (((t *)0)->d))
#endif
// #define offsetof(t, d) __builtin_offsetof(t, d)
#else
#ifndef offsetof
#define offsetof(t, d) ((size_t) & (((t *)0)->d))
#endif
#endif
#define SMS_EXTERN_C extern "C"
#else
#define SMS_EXTERN_C extern
#endif

#ifdef _MSC_VER
#define SMS_NO_INLINE __declspec(noinline)
#else
#define SMS_NO_INLINE __attribute__((noinline))
#endif

#define SMS_ATOMIC_CODE(code)                                                                      \
    u32 __atomic_interrupt_state = OSDisableInterrupts();                                          \
    code OSRestoreInterrupts(__atomic_interrupt_state);

#if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) ||                        \
    (defined(__ICC) && (__ICC >= 600))
#define SMS_FUNC_SIG __PRETTY_FUNCTION__
#elif defined(__FUNCSIG__)
#define SMS_FUNC_SIG __FUNCSIG__
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
#define SMS_FUNC_SIG __func__
#else
#define SMS_FUNC_SIG "UNDEFINED"
#endif

#define SMS_CALL_NAIVE(addr, ...) ((void (*)(...))addr)(__VA_ARGS__)