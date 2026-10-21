#pragma once

#include <Dolphin/types.h>

class TSpcSlice {
public:
    TSpcSlice();

    u32 getDataInt() const;
    f32 getDataFloat() const;

    bool operator!=(const TSpcSlice &) const;
    bool operator==(const TSpcSlice &) const;

    u32 mType;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[4];
    u32 mValue;
#else
    u32 mValue;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};