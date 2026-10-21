#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
// Full lockstep tracing (trace.cpp) needs Linux ELF helpers. Empty stubs keep
// the weak call sites in vi.cpp / pad.cpp linking on Windows and macOS.
#include "trace.h"
extern "C" volatile uint32_t port_trace_anchor = 0x54524143;
extern "C" void port_trace_on_retrace(uint32_t) {}
extern "C" int port_trace_pad_read(PADStatus*) { return 0; }
extern "C" int port_trace_movie_active(void) { return 0; }
#endif
