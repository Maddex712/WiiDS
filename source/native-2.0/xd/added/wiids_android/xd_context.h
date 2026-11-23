#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AAPCS64 callee-saved registers; no signal context or libc symbol replacement. */
typedef struct XdContext {
    uint64_t saved[22];
    struct { void* ss_sp; size_t ss_size; } uc_stack;
    struct XdContext* uc_link;
    void (*entry)(void);
} XdContext;
int XdContext_Init(XdContext*);
void XdContext_Make(XdContext*, void (*)(void), int);
int XdContext_Swap(XdContext*, const XdContext*);
void* XdMapLow(size_t);
#ifdef __cplusplus
}
#endif
