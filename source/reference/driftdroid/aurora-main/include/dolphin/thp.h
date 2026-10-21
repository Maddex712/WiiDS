#ifndef _DOLPHIN_THP_H_
#define _DOLPHIN_THP_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL THPInit(void);
s32 THPVideoDecode(const void* file, void* tileY, void* tileU, void* tileV, void* work /* unused */);
// aurora extension: the frame's luma dimensions, read from its headers. A host caller that decodes
// straight into guest RAM needs them to report which bytes it wrote.
BOOL THPVideoFrameSize(const void* file, u16* width, u16* height);
u32 THPAudioDecode(s16* audioBuffer, const u8* audioFrame, s32 flag);

#ifdef __cplusplus
}
#endif

#endif // _DOLPHIN_THP_H_
