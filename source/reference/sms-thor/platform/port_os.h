// Internal interfaces between the platform modules.
#ifndef SMS_PORT_OS_H
#define SMS_PORT_OS_H
#include <dolphin/types.h>
#ifdef __cplusplus
#include <functional>
extern "C" {
#endif
extern u8* port_mem1_base;
extern u32 port_mem1_size;
void port_os_init(void);
void port_dvd_init(void);
void port_vi_init(void);
void port_noaudio_init(void);
int port_vi_idle_advance(void);   /* deterministic VI: retrace on idle */
int port_vi_deterministic(void);
s64 port_vi_virtual_ticks(void);
int port_dvd_override(const char* path, const void* data, u32 size);
extern int port_no_audio;
/* Interrupt emulation: the running game thread owns the "CPU"; interrupt
 * work runs at check points (OS calls, blocking, idle). */
void port_irq_check(void);          /* deliver pending interrupts if enabled */
void port_irq_kick(void);           /* wake an idle CPU (any host thread)    */
/* Called by the running game thread while it owns the emulated CPU. */
int port_os_background_ready(void); /* lower-priority runnable work exists */
#ifdef __ANDROID__
int port_os_yield_background(u64 budget_ns); /* bounded cooperative handoff */
#endif
typedef void (*port_irq_poll_fn)(void);
void port_irq_add_source(port_irq_poll_fn fn); /* polled at each delivery   */
s64 port_time_ticks(void);
#ifdef __cplusplus
}
/* Queue work to run in interrupt context at the next check point. Caller must
 * be a game thread (holds the CPU). */
void port_irq_defer(std::function<void()> fn);
/* Run fn immediately in interrupt context (instant DMA completions). */
void port_irq_run_now(std::function<void()> fn);
#endif
#endif
