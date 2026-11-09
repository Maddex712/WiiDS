/* Portable spellings of the compiler builtins and attributes used by the runtime
   and by the generated code. Must stay C: recomp.h includes it.

   Clang and GCC provide these natively. MSVC needs intrinsics, so keep every use
   behind one of these macros rather than naming a __builtin_ directly. */
#pragma once
#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#include <stdlib.h>

#define GCN_BSWAP16(x) _byteswap_ushort((unsigned short)(x))
#define GCN_BSWAP32(x) _byteswap_ulong((unsigned long)(x))
#define GCN_BSWAP64(x) _byteswap_uint64((unsigned __int64)(x))

/* MSVC has no branch-probability hint; the condition still has to evaluate. */
#define GCN_LIKELY(x) (!!(x))
#define GCN_UNLIKELY(x) (!!(x))

#define GCN_PRINTF_FMT(fmt_idx, first_arg)
/* COFF has no weak symbols in the ELF sense. Nothing overrides these today, so a
   plain definition is correct; see GCN_WEAK's use in gx/fifo.cpp. */
#define GCN_WEAK

static __inline uint32_t GCN_CLZ32(uint32_t v) {
    unsigned long i;
    return _BitScanReverse(&i, (unsigned long)v) ? (uint32_t)(31 - i) : 32u;
}

#else /* Clang / GCC */

#define GCN_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#define GCN_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#define GCN_BSWAP64(x) __builtin_bswap64((uint64_t)(x))

#define GCN_LIKELY(x) __builtin_expect(!!(x), 1)
#define GCN_UNLIKELY(x) __builtin_expect(!!(x), 0)

#define GCN_PRINTF_FMT(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))

/* Clang on COFF only emulates weak via /alternatename, which we don't need. */
#if defined(_WIN32)
#define GCN_WEAK
#else
#define GCN_WEAK __attribute__((weak))
#endif

static inline uint32_t GCN_CLZ32(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32u; }

#endif

/* Guest context switches: setjmp at an OSSaveContext call site, longjmp to resume it. POSIX
   setjmp/longjmp save and restore the signal mask, a syscall per switch, so the mask-free
   _setjmp/_longjmp are used there. Windows' pair never touches a signal mask, and its CRT
   has no _longjmp. Expand where <setjmp.h> is included. */
#if defined(_WIN32)
#define GCN_SETJMP(b) setjmp(b)
#define GCN_LONGJMP(b, v) longjmp(b, v)
#else
#define GCN_SETJMP(b) _setjmp(b)
#define GCN_LONGJMP(b, v) _longjmp(b, v)
#endif
