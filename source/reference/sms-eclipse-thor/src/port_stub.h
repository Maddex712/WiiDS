#ifndef SMS_PORT_STUB_H
#define SMS_PORT_STUB_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct port_stub_rec {
	const char* name;
	unsigned long count;
	struct port_stub_rec* next;
} port_stub_rec;
void port_stub_hit(port_stub_rec* rec);
void port_stub_report(void);
#ifdef __cplusplus
}
#endif
#define SDK_WEAK __attribute__((weak))
#define SDK_STUB(fn)                                                           \
	do {                                                                       \
		static port_stub_rec rec_ = { #fn, 0, 0 };                             \
		port_stub_hit(&rec_);                                                  \
	} while (0)
#endif
