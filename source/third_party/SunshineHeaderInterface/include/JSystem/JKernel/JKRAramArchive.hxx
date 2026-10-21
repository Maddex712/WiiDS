#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRFileLoader.hxx>
#include <JSystem/JKernel/JKRHeap.hxx>

class JKRAramArchive : public JKRFileLoader {
    JKRAramArchive(s32, int);
    virtual ~JKRAramArchive();

    virtual void *fetchResource(int *, u32 *);
    virtual void *fetchResource(void *, u32, int *, u32 *);

    u32 *fetchResource_subroutine(u32, u32, JKRHeap *, int, u8 **);
    u32 *fetchResource_subroutine(u32, u32, u8 *, u32, int);
};