#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/Strategic/HitActor.hxx>

class TModelWaterManager : public JDrama::TViewObj {
public:
    virtual ~TModelWaterManager();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;

    void askDoWaterHitCheck();
    void askHitWaterParticleOnGround(const TVec3f &);
    void calcDrawVtx(Mtx44);
    void calcVMAll(Mtx44);
    void calcVMMtxGround(Mtx44, f32, const TVec3f &, const TVec3f &, Mtx44);
    void calcVMMtxWall(Mtx44, f32, const TVec3f &, const TVec3f &, Mtx44);
    void calcWorldMinMax();
    void drawMirror(Mtx44);
    void drawRefracAndSpec() const;
    void drawShineShadowVolume(Mtx44);
    void drawSilhouette(Mtx44);
    void drawTouching();
    void drawWaterVolume(Mtx44);
    u8 emitRequest(const TWaterEmitInfo &info);
    void garbageCollect();
    void makeEmit(const TWaterEmitInfo &info);
    void move();
    void wind(const TVec3f &);

    s16 mMaxEmit;         // 0x0010
    s16 mEmitCount;
#if __SIZEOF_POINTER__ == 8
#else
       // 0x0012
    u32 _03[0x5D48 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x0014
    u16 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
              // 0x5D5C
    u8 _05;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[25979];
               // 0x5D5E
    s8 mWaterCardType;
#else
               // 0x5D5E
    s8 mWaterCardType;
#endif
    // 0x5D5F

    struct {
        u16 _01            : 8;
        u16 mShowShadow  : 1;
        u16 mMaskObjects : 1;
        u16 _00            : 6;
    } LightType;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x5D60

    u16 _06;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                  // 0x5D62
    u32 _07[0xA8 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[238];
        // 0x5D64
    f32 mSize;
#else
        // 0x5D64
    f32 mSize;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                // 0x5E0C
    f32 _08[0x30 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[48];
        // 0x5E10
    f32 mSphereStep;
#else
        // 0x5E10
    f32 mSphereStep;
#endif
          // 0x5E40
    u8 mLayerCount;           // 0x5E44
    u8 mDarkLevel;
#if __SIZEOF_POINTER__ == 8
#else
            // 0x5E45
    u8 _09;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_15[1];
                   // 0x5E46
    JUtility::TColor mColor;
#else
                   // 0x5E46
    JUtility::TColor mColor;
#endif
  // 0x5E47

    static THitActor mStaticHitActor;
};

extern TModelWaterManager *gpModelWaterManager;
extern JUtility::TColor waterColor[4];
extern TVec3f gShineShadowPos;