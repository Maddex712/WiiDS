#pragma once

#include <JSystem/JKernel/JKRHeap.hxx>
#include <JSystem/JParticle/JPADefaultTexture.hxx>
#include <JSystem/JParticle/JPATexture.hxx>

class JPATextureResource {
public:
    JPATextureResource(size_t capacity, JKRHeap *heap);

    void registration(u8 *data, JKRHeap *heap);

    JPADefaultTexture mDefaultTexture;
#if __SIZEOF_POINTER__ == 8
    u32 mTextureNum;
#else
    size_t mTextureNum;
#endif

#if __SIZEOF_POINTER__ == 8
    u32 mTextureMax;
#else
    size_t mTextureMax;
#endif

    JPATexture **mTextures;
};