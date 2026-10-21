#pragma once

#include <Dolphin/types.h>

class JKRArchive;

class JKRFileFinder {
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
    u16 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _A;
#endif


    virtual ~JKRFileFinder();

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[16];
#else
#endif
};

class JKRArcFinder : public JKRFileFinder {
public:
    JKRArcFinder(JKRArchive *, u32, u32);
    virtual ~JKRArcFinder() override;

    virtual bool findNextFile();
#if __SIZEOF_POINTER__ == 8
#else
    u8 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _11;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _12;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                // padding?
    u8 _13;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[8];
                // ^^
    JKRArchive *mArchive;
#else
                // ^^
    JKRArchive *mArchive;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _14
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};