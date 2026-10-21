#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JParticle/JPABaseEmitter.hxx>

class TSelectShine {
public:
    TSelectShine(J3DModelData *data, J3DAnmColor *acolor, void *emitterManager, TVec3f &pos,
                 s16 unk1, u8 unk2, f32 unk3, f32 unk4, f32 unk5);
    virtual ~TSelectShine();
    virtual void move();

    J3DModel *mModel;
    void *mBowEffects;
    TVec3f mOffsetPosition;
    TVec3f mOffsetPosition2;
    u32 mFlags;
    f32 mRiseOffset;
    f32 mRiseMaxOffset;
    f32 mRiseRate;
    u32 mUnkCounter;
    s8 mRotationSpeed;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C[0x20 / 4];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[51];
#else
#endif
};