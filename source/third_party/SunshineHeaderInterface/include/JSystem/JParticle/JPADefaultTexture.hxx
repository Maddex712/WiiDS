#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

class JPADefaultTexture {
public:
    void initialize();

    void *mData;
    GXTexObj mTexObj;
};