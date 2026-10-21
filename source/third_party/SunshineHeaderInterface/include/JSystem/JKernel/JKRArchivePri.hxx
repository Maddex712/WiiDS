#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRFileFinder.hxx>
#include <JSystem/JKernel/JKRFileLoader.hxx>

enum EMountMode {};

class JKRArchive : public JKRFileLoader {
public:
    enum EMountDirection {};

    class SDIFileEntry {
    public:
#if __SIZEOF_POINTER__ == 8
#else
        u32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
        s32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // offset to something
        u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[16];
        u32 *mEntry;
#else
        u32 *mEntry;
#endif
  // _10
    };

    JKRArchive();
    JKRArchive(u32, EMountMode);
    virtual ~JKRArchive();

    virtual bool becomeCurrent(const char *);
    virtual void *getResource(const char *);
    virtual void *getResource(u32, const char *);
    virtual u32 *readResource(void *, u32, const char *);
    virtual u32 *readResource(void *, u32, u32, const char *);
    virtual void removeResourceAll();
    virtual bool removeResource(void *);
    virtual void detachResource(void *);
    virtual s32 getResSize(const void *) const;
    virtual s16 countFile(const char *);
    virtual JKRArcFinder *getFirstFile(const char *) const;
#if __SIZEOF_POINTER__ == 8
#else
    u32 *_38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _3D;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _3E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _3F;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _58;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[68];
    int mountDirection;
#else
    int mountDirection;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_13[4];
  // _5C

    s32 **sCurrentDirID;
#else
  // _5C

    s32 **sCurrentDirID;
#endif

};