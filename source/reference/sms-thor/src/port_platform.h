#ifndef SMS_PORT_PLATFORM_H
#define SMS_PORT_PLATFORM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void port_init(int argc, char** argv);
void port_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
extern const char* port_disc_root;
// 1 when the command line or SMS_DISC_ROOT chose the game source.
extern int port_disc_explicit;
// A whole disc file (malloc'd, caller frees), or NULL. After port_dvd_init.
void* port_dvd_read_file(const char* path, size_t* size);
// Gives the window the game's memory-card icon (platform/misc/window_icon.cpp).
void port_window_icon_init(void);
// Memory the game can hold as a 32-bit address (below 2 GiB where the host
// allows it): 64-bit builds put every stack game code runs on here, since
// the game keeps pointers in u32 slots. Returns NULL on failure.
void* port_low_alloc(unsigned long size);
#ifdef _WIN64
// winpthreads ignores a thread's stack address, so 64-bit Windows moves each
// thread that runs game code onto its low stack itself. port_run_on_stack
// calls fn(arg) on [stack, stack + size) and returns its result.
// port_leave_stack, called from inside, abandons that stack and makes
// port_run_on_stack return NULL: a thread exit (pthread_exit unwinds, which
// must not happen across the switched stack). Outside one it is pthread_exit.
void* port_run_on_stack(void* stack, size_t size, void* (*fn)(void*), void* arg);
void port_leave_stack(void) __attribute__((noreturn));
#endif
#ifdef __cplusplus
}
#endif
#endif
