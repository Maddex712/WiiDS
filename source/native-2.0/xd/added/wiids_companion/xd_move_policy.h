#pragma once
#include <stdint.h>
// The native move window rejects its hidden-slot sentinel before handling A.
// The battle mechanics' legality query alone does not include that UI guard.
inline bool WiidsXD_MoveSelectable(uint16_t id,bool nativeLegal) {
    return nativeLegal&&id!=0&&id!=0x176;
}
