#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/JSupport/JSUList.hxx>

enum JKRExpandSwitch { NOP = 0, EXPAND = 1 };

class JKRDvdRipper {
public:
    enum EAllocDirection { TAIL, HEAD };

    static void *loadToMainRAM(const char *path, u8 *, JKRExpandSwitch expand, u32, JKRHeap *heap,
                               EAllocDirection direction, u32, int *);

    static JSUPtrList sDvdAsyncList;
    static s32 szpBufferSize;
};