/* Native-vs-retail lockstep testing (platform/trace): .dtm movie playback into
 * PADRead and a per-field memory trace in dolphin-oracle's format. See
 * README.md. Everything is off unless SMS_MOVIE / SMS_TRACE_OUT are set. */
#ifndef SMS_PORT_TRACE_H
#define SMS_PORT_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PADStatus;

/* Call once per vertical retrace, right after the retrace counter is
 * incremented (platform/vi retrace_irq), before the game's retrace callbacks.
 * Advances the movie's poll position and writes one trace field. */
void port_trace_on_retrace(uint32_t retrace_count);

/* Call at the top of PADRead. If a movie is playing, fills status[0..3] from
 * the movie (port 1; ports 2-4 report no controller) and returns 1; the
 * caller then returns without reading the keyboard/controller. Returns 0
 * when no movie is active. */
int port_trace_pad_read(struct PADStatus* status);

/* 1 when SMS_MOVIE is playing (the lead may want to disable other input). */
int port_trace_movie_active(void);

/* Static-address anchor used by tools/trace_resolve.py to relocate host
 * symbols in a PIE binary. */
extern volatile uint32_t port_trace_anchor;

#ifdef __cplusplus
}
#endif

#endif
