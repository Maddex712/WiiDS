#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <SMS/System/MarNameRefGen.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/Vector.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

#include <SMS/Camera/CameraMapTool.hxx>

class TCubeGeneralInfo : public JDrama::TNameRef {
public:
    TCubeGeneralInfo(const char *name);
    ~TCubeGeneralInfo() override;

    void load(JSUMemoryInputStream &in) override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[2];
    TVec3f mTranslation;
#else
    TVec3f mTranslation;
#endif

    TVec3f mRotation;
    TVec3f mScale;
#if __SIZEOF_POINTER__ == 8
#else
    u16 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _32;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
 // Always 2
    u32 mDataNo;
#else
 // Always 2
    u32 mDataNo;
#endif

};

class TCubeCameraInfo : public JDrama::TNameRef {
public:
    TCubeCameraInfo(const char *name);
    ~TCubeCameraInfo() override;

    void load(JSUMemoryInputStream &in) override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[2];
    TVec3f mTranslation;
#else
    TVec3f mTranslation;
#endif

    TVec3f mRotation;
    TVec3f mScale;
#if __SIZEOF_POINTER__ == 8
#else
    u16 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _32;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[4];
  // Always 2
    u32 mDataNo;
#else
  // Always 2
    u32 mDataNo;
#endif

    TCameraMapTool *mMapTool;
};