#pragma once

typedef unsigned char u8;
typedef unsigned short u16;
#if __SIZEOF_POINTER__ == 8
typedef unsigned int u32;
#else
typedef unsigned long u32;
#endif
typedef unsigned long long u64;
typedef char s8;
typedef short s16;
#if __SIZEOF_POINTER__ == 8
typedef int s32;
#else
typedef long s32;
#endif
typedef long long s64;
typedef float f32;
typedef double f64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

#if !defined(_SIZE_T)
#define _SIZE_T
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned int size_t;
#endif
#endif

#if !defined(_PTRDIFF_T)
#define _PTRDIFF_T
#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef long ptrdiff_t;
#endif
#endif

#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL  ((void *)0)
#define bool  int
#define true  1
#define false 0
#endif