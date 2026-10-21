#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUStream.hxx>

class JSUInputStream : public JSUIosBase {
public:
    JSUInputStream();
    virtual ~JSUInputStream();

    virtual s32 getAvailable() const = 0;
    virtual void skip(s32);
    virtual void readData(void *, s32) = 0;

    void read(void *, s32);
    static void swapBE(void *buf, s32 size, s32 width) {
        u8 *b = (u8 *)buf;
        if (width <= 0)
            width = size;
        for (s32 o = 0; o + width <= size; o += width)
            for (s32 i = 0; i < width / 2; i++) {
                u8 t                 = b[o + i];
                b[o + i]             = b[o + width - 1 - i];
                b[o + width - 1 - i] = t;
            }
    }
    void readBE(void *buf, s32 size, s32 width = 0) {
        swapBE(buf, size, width);
        read(buf, size);
        swapBE(buf, size, width);
    }
    void readDataBE(void *buf, s32 size, s32 width = 0) {
        swapBE(buf, size, width);
        readData(buf, size);
        swapBE(buf, size, width);
    }
    void readString(char *, u16);
    char *readString();
};

class JSURandomInputStream : public JSUInputStream {
public:
    virtual ~JSURandomInputStream();

    virtual s32 getAvailable() const override;
    virtual void skip(s32) override;
    virtual size_t getLength() const            = 0;
    virtual s32 getPosition() const             = 0;
    virtual s32 seekPos(s32, JSUStreamSeekFrom) = 0;

    u32 align(s32);
    u32 peek(s32);
    void seek(s32, JSUStreamSeekFrom);
};