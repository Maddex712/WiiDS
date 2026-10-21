// Force-included into every BetterSunshineEngine and Eclipse source the port
// compiles: headers their PowerPC build reaches through its own include order.
#pragma once
#include <stddef.h>
#include <Dolphin/types.h>
#ifdef __cplusplus
#include <JSystem/memory.hxx>
#endif

// The game's plain char, which the mods (built -funsigned-char, as for the
// PowerPC) declare as the game does: signed on x86, unsigned on ARM.
#if defined(__aarch64__) || defined(__arm__)
typedef unsigned char sms_game_char;
#else
typedef signed char sms_game_char;
#endif

// The port's function for a retail address a mod calls through a cast
// (fixup_sources.py rewrites the address into a call of this).
#ifdef __cplusplus
extern "C"
#endif
void* sms_mod_rawaddr(unsigned int addr);

// The port's object for a retail data address a mod reads or writes
// (platform/mods/eclipse/rawdata.cpp; fixup_sources.py rewrites the casts).
#ifdef __cplusplus
extern "C"
#endif
void* sms_mod_rawdata(unsigned int addr);

// The port's MEM1 for an OS global's retail address, 0x80000008 and the like
// (platform/mods/eclipse/rawdata.cpp; fixup_sources.py rewrites the casts).
#ifdef __cplusplus
extern "C"
#endif
void* sms_mod_lowmem(unsigned int addr);

// SMS_OFFSET(Class, retail offset): where the port keeps a member the mods
// address by retail offset (tools/mods/shi_layout/offsets.py).
#ifdef __cplusplus
#include <sms_offsets.h>
#endif
