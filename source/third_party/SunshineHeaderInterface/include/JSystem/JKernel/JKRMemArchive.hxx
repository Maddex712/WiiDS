#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRArchivePri.hxx>

enum JKRMemBreakFlag { UNK_0, UNK_1 };

class JKRMemArchive : public JKRArchive {
public:
    JKRMemArchive();
    JKRMemArchive(s32, JKRArchive::EMountDirection);
    JKRMemArchive(void *, size_t, JKRMemBreakFlag);
    virtual ~JKRMemArchive();

    virtual void removeResourceAll();
    virtual bool removeResource(void *);

    virtual void *fetchResource(JKRArchive::SDIFileEntry *, u32 *);
    virtual void *fetchResource(void *src, u32 len, JKRArchive::SDIFileEntry *, u32 *);
    virtual void *fetchResource_subroutine(u8 *, size_t, u8 *, size_t, int);

    bool mountFixed(void *, JKRMemBreakFlag);
    bool open(s32, JKRArchive::EMountDirection);
    bool open(void *, size_t, JKRMemBreakFlag);
    void unmountFixed();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _64;
#endif
  // related to offsets

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};