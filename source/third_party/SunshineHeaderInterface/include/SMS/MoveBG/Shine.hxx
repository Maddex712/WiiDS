#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JParticle/JPABaseEmitter.hxx>
#include <SMS/MoveBG/Item.hxx>

class TShine : public TItem {
public:
    TShine(const char *);
    virtual ~TShine();

    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual void control() override;
    virtual void kill() override;
    virtual void initMapObj() override;
    virtual void loadBeforeInit(JSUMemoryInputStream &) override;
    virtual void makeMActors() override;
    virtual void calc() override;
    virtual void touchPlayer(THitActor *) override;

    void appearSimple(int);
    void appearWithDemo(const char *);
    void appearWithTime(int, int, int, int);
    void appearWithTimeCallback(u32, u32);
    void movingCircle();

    u32 mType;
#if __SIZEOF_POINTER__ == 8
#else
                     // 0x0154
    u32 _158[0x38 / 4];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[56];
            // 0x0158
    bool mIsNormalCamera;
#else
            // 0x0158
    bool mIsNormalCamera;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[3];
          // 0x0190
    JPABaseEmitter *mPromiEffect;
#else
          // 0x0190
    JPABaseEmitter *mPromiEffect;
#endif
  // 0x0194 | Light Flare
    JPABaseEmitter *mSenkoEffect;  // 0x0198 | Light rays
    JPABaseEmitter *mKiraEffect;   // 0x019C | Sparkles
    JPABaseEmitter *mBowEffect;
#if __SIZEOF_POINTER__ == 8
#else
    // 0x01A0 | Light bow
    u32 _1A4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[4];
    TVec3f mGlowSize;
#else
    TVec3f mGlowSize;
#endif
         // 0x01A8
    bool mIsAlreadyObtained;
#if __SIZEOF_POINTER__ == 8
#else
  // 0x01B4
    u32 _1B8[0x40 / 4];
#endif
       // 0x01B8

    static f32 mBowRate;
    static f32 mCircleRate;
    static f32 mKiraRate;
    static f32 mPromiLife;
    static f32 mSenkoRate;
    static f32 mSpeedDownRate;
    static f32 mUpSpeed;
};