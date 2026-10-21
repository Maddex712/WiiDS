#ifndef SMS_PORT_EXECINFO_H
#define SMS_PORT_EXECINFO_H

#ifdef _WIN32
#undef __declspec
#define __declspec(x) __attribute__((x))
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static inline int backtrace(void** frames, int capacity)
{
	return (int)CaptureStackBackTrace(0, (DWORD)capacity, frames, NULL);
}
static inline void backtrace_symbols_fd(void* const* frames, int count, int)
{
	for (int i = 0; i < count; ++i)
		fprintf(stderr, "[port] frame %d: %p\n", i, frames[i]);
}
// Like glibc: one malloc'd block holding the pointer array and the strings,
// released with a single free(). Each entry is "module+0xoffset".
static inline char** backtrace_symbols(void* const* frames, int count)
{
	enum { kLen = 96 };
	if (count <= 0)
		return NULL;
	char** out = (char**)malloc(count * (sizeof(char*) + kLen));
	if (!out)
		return NULL;
	char* text = (char*)(out + count);
	for (int i = 0; i < count; ++i) {
		out[i] = text + i * kLen;
		HMODULE mod = NULL;
		char path[MAX_PATH] = "?";
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       (LPCSTR)frames[i], &mod))
			GetModuleFileNameA(mod, path, sizeof path);
		const char* base = path;
		for (const char* p = path; *p; ++p)
			if (*p == '\\' || *p == '/')
				base = p + 1;
		snprintf(out[i], kLen, "%s+0x%lx", base,
		         (unsigned long)((char*)frames[i] - (char*)mod));
	}
	return out;
}
#else
#include_next <execinfo.h>
#endif

#endif
