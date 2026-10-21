#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/Enemy/SpineBase.hxx>
#include <SMS/M3DUtil/LodAnm.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/M3DUtil/MActorKeeper.hxx>
#include <SMS/MSound/MAnmSound.hxx>
#include <SMS/Manager/LiveManager.hxx>
#include <SMS/Manager/MapCollisionManager.hxx>
#include <SMS/Map/BGCheck.hxx>
#include <SMS/MarioUtil/RidingInfo.hxx>
#include <SMS/Strategic/TakeActor.hxx>

class TLiveActor : public TTakeActor {

public:
    TLiveActor(const char *);
    virtual ~TLiveActor();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int receiveMessage(THitActor *, u32) override;
    virtual Mtx44 *getTakingMtx() override;
    virtual int belongToGround() const;
    virtual Mtx44 *getRootJointMtx() const;
    virtual void init(TLiveManager *);
    virtual void calcRootMatrix();
    virtual void setGroundCollision();
    virtual void control();
    virtual void bind();
    virtual void moveObject();
    virtual void requestShadow();
    virtual void drawObject(JDrama::TGraphics *);
    virtual void performOnlyDraw(u32, JDrama::TGraphics *);
    virtual u32 getShadowType();
    virtual void kill();
    virtual f32 getGravityY() const;
    virtual int hasMapCollision() const;
    virtual TVec3f getFocalPoint() const;
    virtual void updateAnmSound();
    virtual const char **getBasNameTable() const;

    MActor *getMActor() const;
    void stopAnmSound();
    void setCurAnmSound();
    void setAnmSound(const char *);
    void initAnmSound();
    void getJointTransByIndex(int, TVec3f *) const;
    TVec3f calcVelocityToJumpToY(const TVec3f &, f32, f32) const;
    void initLodAnm(const TLodAnmIndex *, int, f32);
    J3DModel *getModel() const;
    void calcRideMomentum();
    void calcRidePos();

    TLiveManager *mLiveManager;   // 0x0070
    MActor *mActorData;           // 0x0074
    TMActorKeeper *mActorKeeper;  // 0x0078
    u16 mActorIndex;              // _7C
    MAnmSound *mAnmSound;         // _80
    char *mBasName;
#if __SIZEOF_POINTER__ == 8
#else
               // _84
    u32 *_88;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[8];
    TSpineBase<TLiveActor> *mSpineBase;
#else
    TSpineBase<TLiveActor> *mSpineBase;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // 0x008C
    u32 _90;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[8];
                             // 0x0090
    TVec3f mPositionalVelocity;
#else
                             // 0x0090
    TVec3f mPositionalVelocity;
#endif
          // 0x0094
    TVec3f mRotationalVelocity;          // 0x00A0
    TVec3f mSpeed;                       // 0x00AC
    f32 mShadowRadius;                   // 0x00B8
    f32 mMaxSpeed;                       // 0x00BC
    f32 _C0;                             // 0x00C0
    const TBGCheckData *mFloorBelow;     // 0x00C4
    f32 mGroundY;                        // 0x00C8
    f32 mGravity;
#if __SIZEOF_POINTER__ == 8
#else
                        // 0x00CC
    u32 _D0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_19[8];
    TRidingInfo mRidingInfo;
#else
    TRidingInfo mRidingInfo;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _D4
    f32 _E4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _E8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_22[8];
    TMapCollisionManager *mCollisionManager;
#else
    TMapCollisionManager *mCollisionManager;
#endif
  // _EC

    union {
        u32 asU32;
        struct {
            u32 mIsObjDead     : 1;
            u32 _06             : 3;
            u32 mClipFromScene : 1;
            u32 _05             : 1;
            u32 mHasPhysics    : 1;
            u32 _004           : 1;
            u32 mCullModel     : 1;
            u32 _04             : 8;
            u32 mCanTalk       : 1;
            u32 _03             : 2;
            u32 mCanBeTaken    : 1;
            u32 _02             : 1;
            u32 mIsRide        : 1;
            u32 _01             : 1;
            u32 mIsSunken      : 1;
            u32 _00             : 7;
    } asFlags;
    } mStateFlags;
};