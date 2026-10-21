#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void port_android_performance_frame_end(void);
void port_android_performance_frame_begin(int64_t target_ns);
void port_android_performance_render_thread(void);
void port_android_performance_pause(void);
void port_android_performance_shutdown(void);
#ifdef __cplusplus
}
#endif
