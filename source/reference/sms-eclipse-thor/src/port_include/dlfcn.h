#ifndef SMS_PORT_DLFCN_H
#define SMS_PORT_DLFCN_H

#ifdef _WIN32
#include <windows.h>
#define RTLD_NOW 0
#define RTLD_GLOBAL 0
#define RTLD_DEFAULT ((void*)0)
static inline void* dlopen(const char* name, int)
{
	return (void*)LoadLibraryA(name);
}
static inline void* dlsym(void* handle, const char* name)
{
	HMODULE module = handle ? (HMODULE)handle : GetModuleHandleA("SDL2.dll");
	return module ? (void*)GetProcAddress(module, name) : NULL;
}
#else
#include_next <dlfcn.h>
#endif

#endif
