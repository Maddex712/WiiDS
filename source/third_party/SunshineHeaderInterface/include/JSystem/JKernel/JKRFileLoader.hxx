#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>
#include <JSystem/JKernel/JKRFileFinder.hxx>
#include <JSystem/JSupport/JSUList.hxx>

class JKRArchive;

class JKRFileLoader : public JKRDisposer {
public:
    JKRFileLoader();
    virtual ~JKRFileLoader();

    virtual void unmount();

    static JKRArchive *getVolume(const char *);
    static void changeDirectory(const char *dirName);
    static void *getGlbResource(const char *);
    static void *getGlbResource(const char *, JKRFileLoader *);
    static u32 getResSize(void *resource, JKRFileLoader *);
    static void *findVolume(const char **);
    static JKRArcFinder *findFirstFile(const char *);
    static u8 *fetchVolumeName(u8 *, u32, const char *);

    JSUPtrLink mPtrLink2;
#if __SIZEOF_POINTER__ == 8
#else
  // _18
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _31;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u8 _32;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u8 _33;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // ^^
    u32 _34;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[20];
#else
#endif
};