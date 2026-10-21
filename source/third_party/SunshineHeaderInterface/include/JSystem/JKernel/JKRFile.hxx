#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>

class JKRFile : JKRDisposer {
public:
    virtual ~JKRFile();

    virtual bool open(const char *fileName)                         = 0;
    virtual void close()                                            = 0;
    virtual s32 writeData(const void *addr, s32 length, s32 offset) = 0;
    virtual s32 readData(void *addr, s32 length, s32 offset)        = 0;
    virtual s32 getFileSize() const                                 = 0;
};