#pragma once

#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

class TCameraMapTool {
public:
    TCameraMapTool(const TCameraMapTool &);
    virtual ~TCameraMapTool();

    void calcPosAndAt(TVec3f *, TVec3f *) const;
    void load(JSUMemoryInputStream &);

    TVec3f mTranslation;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[4];
    TVec3f mRotation;
#else
    TVec3f mRotation;
#endif

    u32 mType;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};