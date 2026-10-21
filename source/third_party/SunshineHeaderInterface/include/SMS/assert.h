#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/printf.h>
#include <SMS/macros.h>

#define SMS_ERROR(msg, ...)                                                                        \
    char errmsg[256];                                                                              \
    snprintf(errmsg, 256, "[SME] %s: %s", (SMS_FUNC_SIG), (msg));                                  \
    OSPanic(__FILE__, __LINE__, errmsg, ##__VA_ARGS__);                                            \
    __OSUnhandledException(6, OSGetCurrentContext(), 0)

#define SMS_ASSERT(expr, msg, ...)                                                                 \
    if (!(expr)) {                                                                                 \
        SMS_ERROR(msg, ##__VA_ARGS__);                                                             \
    }

#if SMS_DEBUG
#define SMS_DEBUG_ASSERT(expr, msg, ...) SMS_ASSERT(expr, msg, ##__VA_ARGS__)
#else
#define SMS_DEBUG_ASSERT(expr, msg, ...)
#endif