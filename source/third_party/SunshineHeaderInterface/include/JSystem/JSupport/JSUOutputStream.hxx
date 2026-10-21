#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUStream.hxx>

class JSUOutputStream : public JSUIosBase {
public:
    virtual ~JSUOutputStream();

    virtual void skip(s32, signed char);
    virtual void writeData(void *, s32) = 0;

    void write(const void *, s32);
    void writeBE(const void *buf, s32 size) {
        u8 tmp[8];
        for (s32 i = 0; i < size; i++)
            tmp[i] = ((const u8 *)buf)[size - 1 - i];
        write(tmp, size);
    }
};

class JSURandomOutputStream : public JSUOutputStream {
public:
    virtual ~JSURandomOutputStream();

    virtual size_t getLength() const = 0;
    virtual s32 getPosition() const  = 0;
    virtual s32 seek(s32, JSUStreamSeekFrom);
    virtual s32 getAvailable() const;
    virtual s32 seekPos(s32, JSUStreamSeekFrom) = 0;
};