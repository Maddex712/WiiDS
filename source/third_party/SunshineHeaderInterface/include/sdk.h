#pragma once

#include <Dolphin/types.h>

#ifndef KURIBO_NO_TYPES
#define KURIBO_NO_TYPES
#endif

#ifndef __ppc__
#define __ppc__
#endif

#ifndef SMS_DEBUG
#ifdef NDEBUG
#define SMS_DEBUG 0
#else
#define SMS_DEBUG 1
#endif
#endif

#include <Kuribo/sdk/kuribo_sdk.h>
#define SMS_PATCH_B(source, target)  pp::PatchB(source, target)
#define SMS_PATCH_BL(source, target) pp::PatchBL(source, target)
#define SMS_WRITE_32(source, value)  pp::Patch32(source, value)
