#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRHeap.hxx>

class JPADataBlock {
public:
    JPADataBlock(u8 *data, JKRHeap *heap);
    virtual ~JPADataBlock();

    void *mData;
};