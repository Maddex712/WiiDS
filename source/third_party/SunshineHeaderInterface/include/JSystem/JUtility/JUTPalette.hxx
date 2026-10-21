#pragma once

#include <Dolphin/GX.h>

struct ResTLUT {
    enum EFormat { IA8 = 0, RGB565 = 1, RGB5A3 = 2 };

    u8 mFormat;
    u8 mTransparency;
    u16 mLutSize;
    void *mLut;
};

enum JUTTransparency { OPAQUE, TRANSPARENT };

class JUTPalette {
public:
    JUTPalette(GXTlut tlut, GXTlutFmt fmt, JUTTransparency transparency, u16 lutSize, void *lut);
    ~JUTPalette();

    void load();
    void storeTLUT(GXTlut tlut, ResTLUT *resource);

    GXTlutObj mTlutObj;
    GXTlut mTlut;
    GXTlutFmt mTlutFmt;
    void *mLut;
    u16 mLutSize;
    JUTTransparency mTransparency;
};