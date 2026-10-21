#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/MapObj/MapObjInit.hxx>
#include <SMS/Strategic/HitActor.hxx>
#include <SMS/Strategic/LiveActor.hxx>

class TMapObjBase : public TLiveActor {
public:
    TMapObjBase(const char *);
    virtual ~TMapObjBase();

    void load(JSUMemoryInputStream &) override;
    void loadAfter() override;
    void perform(u32, JDrama::TGraphics *) override;
    int receiveMessage(THitActor *, u32) override;
    Mtx44 *getTakingMtx() override;
    void ensureTakeSituation() override;
    f32 getRadiusAtY(f32) const override;
    Mtx44 *getRootJointMtx() const override;
    void calcRootMatrix() override;
    void setGroundCollision() override;
    void control() override;
    u32 getShadowType() override;
    void kill() override;

    virtual void appear();
    virtual void makeObjAppeared();
    virtual void makeObjDead();
    virtual void changeObjSRT(const TVec3f &, const TVec3f &, const TVec3f &);
    virtual void changeObjMtx(Mtx44);
    virtual void updateObjMtx();
    virtual void setUpCurrentMapCollision();
    virtual void setObjHitData(u16);
    virtual void setModelMtx(Mtx44);
    virtual void initMapObj();
    virtual void loadBeforeInit(JSUMemoryInputStream &);
    virtual void initMapCollisionData();
    virtual void makeMActors();
    virtual u32 getSDLModelFlag() const;
    virtual void checkIllegalAttr() const;
    virtual void calc();
    virtual void draw() const;
    virtual void dead();
    virtual void touchActor(THitActor *);
    virtual void touchPlayer(THitActor *);
    virtual void touchWater(THitActor *);
    virtual void touchEnemy(THitActor *);
    virtual void touchBoss(THitActor *);
    virtual void makeObjDefault();
    virtual u16 getHitObjNumMax();
    virtual f32 getDepthAtFloating();

    bool animIsFinished() const;
    void awake();
    void calcMap();
    void calcReflectingVelocity(const TBGCheckData *, f32, TVec3f *) const;
    void checkOnManhole();
    void concatOnlyRotFromLeft(Mtx44, Mtx44, Mtx44);
    void concatOnlyRotFromRight(Mtx44, Mtx44, Mtx44);
    void emitAndBindScale(s32, u8, const TVec3f *, const TVec3f &) const;
    void emitAndRotateScale(s32, u8, const TVec3f *) const;
    void emitAndScale(s32, u8, const TVec3f *) const;
    void emitAndScale(s32, u8, const TVec3f *, const TVec3f &) const;
    void emitAndSRT(s32, u8, const TVec3f *, const TVec3f &, const TVec3f &);
    void emitColumnWater();
    void *getBuildingJoint(int);
    void *getBuildingJointObj(int);
    f32 getDistance(const TVec3f &) const;
    f32 getDistanceXZ(const TVec3f &) const;
    Vec *getNormalVecFromTarget(f32, f32, f32, TVec3f *) const;
    Vec *getNormalVecFromTargetXZ(f32, f32, TVec3f *) const;
    f32 getObjCollisionHeightOffset() const;
    f32 getRotYFromAxisX(const TVec3f &) const;
    f32 getRotYFromAxisZ(const TVec3f &) const;
    TVec3f *getVerticalVecToTargetXZ(f32, f32, TVec3f *);
    bool hasAnim(u16) const;
    bool hasModelOrAnimData(u16) const;
    void initActorData();
    void initAndRegister(const char *);
    void initBckMoveData();
    void initMActor(const char *, const char *, u32);
    void initModelData();
    void initObjCollisionData();
    void initUnique();
    void makeObjMtxRotByAxis(const TVec3f &, f32, Mtx44) const;
    void makeRootMtxRotX(Mtx44);
    void makeRootMtxRotY(Mtx44);
    void makeRootMtxRotZ(Mtx44);
    void makeVecToLocalZ(f32, TVec3f *);
    bool marioHeadAttack() const;
    bool marioHipAttack() const;
    bool marioIsOn() const;
    void removeMapCollision();
    void sendMsg(u32, u32);
    void setRootMtxRotY();
    void setRootMtxRotZ();
    void setUpMapCollision(u16);
    void sleep();
    void soundBas(u32, f32, f32);
    void startAnim(u16);
    void startBck(const char *);
    void startControlAnim(u16);
    void startSound(u16);
    void throwObjToFront(TMapObjBase *, f32, f32) const;
    void throwObjToFrontFromPoint(TMapObjBase *, const TVec3f &, f32, f32) const;
    void updateRootMtxTrans();

    static f32 getJointScaleY(J3DJoint *);
    static f32 getJointTransX(J3DJoint *);
    static f32 getJointTransZ(J3DJoint *);
    static s32 getWaterID(THitActor *);
    static void *getWaterPlane(THitActor *);
    static TVec3f *getWaterPos(THitActor *);
    static TVec3f *getWaterSpeed(THitActor *);
    static void initPacketColor(J3DModel *, GXTevRegID, const GXColorS10 *);
    static bool isCoin(THitActor *);
    static bool isDemo();
    static bool isFruit(THitActor *);
    static bool isHideObj(THitActor *);
    static void joinToGroup(const char *, THitActor *);
    static void loadHideObjInfo(JSUMemoryInputStream &, s32 *, f32 *, f32 *, s32 *);
    static void makeLowerStr(const char *, char *);
    static void makeMtxRotByAxis(const TVec3f &, f32, Mtx44);
    static bool marioIsOn(const TLiveActor *);
    static void moveJoint(J3DJoint *, f32, f32, f32);
    static void *newAndInitBuildingCollisionMove(int, TLiveActor *);
    static void *newAndInitBuildingCollisionWarp(int, TLiveActor *);
    static void rotateVecByAxisY(TVec3f, f32);
    static void setJointScaleY(J3DJoint *, f32);
    static void setJointTransX(J3DJoint *, f32);
    static void setJointTransY(J3DJoint *, f32);
    static void setJointTransZ(J3DJoint *, f32);
    static void startAllAnim(MActor *, const char *);
    static void throwObjFromPointWithRot(TMapObjBase *, const TVec3f &, const TVec3f &, f32, f32);
    static bool waterHitPlane(THitActor *);

    const char *mRegisterName;  // 0x00F4
    u32 mModelLoadFlags;        // 0x00F8
    u16 mState;
#if __SIZEOF_POINTER__ == 8
#else
                 // 0x00FC
    u16 _01;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[2];
                    // 0x00FE
    u16 mSoundIDIndex;
#else
                    // 0x00FE
    u16 mSoundIDIndex;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[2];
          // 0x0100
    s32 mStateTimer;
#else
          // 0x0100
    s32 mStateTimer;
#endif

#if __SIZEOF_POINTER__ == 8
#else
            // 0x0104
    u32 _03;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[4];
                    // 0x0108
    TVec3f mInitialPosition;
#else
                    // 0x0108
    TVec3f mInitialPosition;
#endif
    // 0x010C
    TVec3f mInitialRotation;
#if __SIZEOF_POINTER__ == 8
#else
    // 0x0118
    u32 _124[3];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[12];
    ObjData *mObjData;
#else
    ObjData *mObjData;
#endif
  // 0x0130
    u32 mMapObjID;      // 0x0134
};