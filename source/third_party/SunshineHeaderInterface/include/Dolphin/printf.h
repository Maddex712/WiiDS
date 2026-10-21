#ifndef STDIO_H
#define STDIO_H

#include <Dolphin/stdarg.h>
#include <Dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int snprintf(char *, size_t, const char *, ...);
int sprintf(char *, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int vprintf(const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif