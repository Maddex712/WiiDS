#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/JParticle/JPADataBlock.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>

class JPATexture : public JPADataBlock {
public:
    JPATexture(u8 *data, JKRHeap *heap);
    virtual ~JPATexture();

    JUTTexture mTexture;
};