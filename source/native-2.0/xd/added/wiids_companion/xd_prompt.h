#pragma once
#include <stdint.h>
extern "C" int WiidsXD_PromptGate(uintptr_t cookie,int allowCancel);
extern "C" int WiidsXD_PromptAction(uintptr_t cookie,int allowCancel);
extern "C" int WiidsXD_PromptOwnsFrame();
extern "C" uintptr_t WiidsXD_NewPromptToken();
