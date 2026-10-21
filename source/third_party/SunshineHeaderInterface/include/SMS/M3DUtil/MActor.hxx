#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/J3D/J3DModelLoader.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Map/BGCheck.hxx>

class MActorAnmBase {
public:
    int mAnimationID;
    J3DFrameCtrl mFrameCtrl;
    J3DModel *mModel;

    MActorAnmBase();

    virtual void setModel(J3DModel *);
    virtual void updateIn();
    virtual void updateOut();
    virtual void checkUseMaterialIDInit(u16 *);
    virtual void checkUseMaterialID(u16 *);
    virtual void setAnmFromIndex(int, u16 *) = 0;
    virtual void setAnm(const char *, u16 *) = 0;
    virtual void verifySuffix(const char *)  = 0;
};

template <typename T> class MActorAnmEach : public MActorAnmBase {
public:
    virtual void setAnm(const char *, u16 *) override;

    void setFrameCtrl(int);
};

template <typename T> class MActorAnmMatEach : public MActorAnmEach<T> {
public:
    virtual void setAnmFromIndex(int, u16 *) override;
    virtual void setAnm(const char *, u16 *) override;

    void loadAnmPtrArray(const char *, const char *);
};

class MActorAnmBlk : public MActorAnmEach<J3DAnmClusterKey> {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void setAnmFromIndex(int, u16 *) override;
    virtual void verifySuffix(const char *) override;
};

class MActorAnmBrk : public MActorAnmMatEach<J3DAnmClusterKey> {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void checkUseMaterialIDInit(u16 *) override;
    virtual void checkUseMaterialID(u16 *) override;
    virtual void verifySuffix(const char *) override;

    void setTevColorAnmKeyPtr();
    void setTevKColorAnmKeyPtr();
};

class MActorAnmBpk : public MActorAnmMatEach<J3DAnmClusterKey> {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void checkUseMaterialIDInit(u16 *) override;
    virtual void checkUseMaterialID(u16 *) override;
    virtual void verifySuffix(const char *) override;

    void setMatColorAnmKeyPtr();
};

class MActorAnmBtk : public MActorAnmMatEach<J3DAnmClusterKey> {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void checkUseMaterialIDInit(u16 *) override;
    virtual void checkUseMaterialID(u16 *) override;
    virtual void verifySuffix(const char *) override;

    void setTexMtxAnmKeyPtr();
};

class MActorAnmBtp : public MActorAnmMatEach<J3DAnmClusterKey> {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void checkUseMaterialIDInit(u16 *) override;
    virtual void checkUseMaterialID(u16 *) override;
    virtual void verifySuffix(const char *) override;

    void setTexNoAnmFullPtr();
};

class MActorAnmBck : public MActorAnmMatEach<J3DAnmClusterKey> {
public:
    virtual void setModel(J3DModel *) override;
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void setAnmFromIndex(int, u16 *) override;
    virtual void verifySuffix(const char *) override;

    f32 getMotionBlendRatio() const;
    void *getOldMotionBlendAnmPtr() const;
    f32 getOldMotionBlendFrame() const;
    void initNormalMotionBlend();
    void initSimpleMotionBlend(int);
    void setMotionBlendRatio(f32);
    void setOldMotionBlendAnmPtr(J3DAnmTransform *);
};

class MActorAnmData : public J3DModelLoader {
public:
    enum LoopType { END, BEGIN, LOOP };

    MActorAnmData();

    void addFileTable(const char *);
    void createSampleModelData(J3DModelData *);

    // Initializes animation data using all bck files in described folder
    void init(const char *path, const char **);

    s32 partsNameToIdx(const char *);

    u32 mAnimationID;
    J3DFrameCtrl mFrameCtrl;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
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
    u8 _pc64_tail[84];
#else
#endif
};

class MActor {
public:
    enum AnimationType { BCK, BLK, BPK, BTP, BTK, BRK };

    MActor(MActorAnmData *);

    void calc();
    void calcAnm();
    bool checkAnmFileExist(const char *, int);
    bool checkBckPass(f32);
    int checkCurAnm(const char *, int);
    int checkCurAnmFromIndex(int, int);
    int checkCurBckFromIndex(int);
    int curAnmEndsNext(int, char *);
    void entry();
    void entryIn();
    void entryOut();
    void frameUpdate();
    s32 getCurAnmIdx(int) const;
    J3DFrameCtrl *getFrameCtrl(int);
    void initDL();
    bool isCurAnmAlreadyEnd(int);
    void matAnmFrameUpdate();
    void offMakeDL();
    void perform(u32, JDrama::TGraphics *);
    void resetDL();
    void setAnimation(const char *, int);
    void setBck(const char *);
    void setBckFromIndex(int);
    void setBlk(const char *);
    void setBpk(const char *);
    void setBpkFromIndex(int);
    void setBrk(const char *);
    void setBrkFromIndex(int);
    void setBtk(const char *);
    void setBtkFromIndex(int);
    void setBtp(const char *);
    void setBtpFromIndex(int);
    void setFrameRate(f32, int);
    void setJointCallback(int, int (*)(J3DNode *, int));
    void setLightData(const TBGCheckData *, const TVec3f &);
    void setLightID(s16);
    void setLightType(int);
    void setModel(J3DModel *, u32);
    void unlockDLIfNeed();
    void updateIn();
    void updateInSubBck();
    void updateMatAnm();
    void updateOut();
    void updateOutSubBck();
    void viewCalc();

    MActorAnmData *mAnmData;
    J3DModel *mModel;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _08;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[8];
    MActorAnmBck *mBckInfo;
#else
    MActorAnmBck *mBckInfo;
#endif

    size_t mNumParts;  //?
    MActorAnmBpk *mBpkInfo;
    MActorAnmBtp *mBtpInfo;
    MActorAnmBtk *mBtkInfo;
    MActorAnmBrk *mBrkInfo;
    MActorAnmBlk *mBlkInfo;
    MActorAnmBase **mAnmArray;
    u16 *mPartsList;
    u16 *mDisplayList;
#if __SIZEOF_POINTER__ == 8
    u32 mDisplayListLen;
#else
    size_t mDisplayListLen;
#endif
  //?
    bool mUseDisplayList;
    bool mIsVisible;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_16[2];
    u32 mLightID;
#else
    u32 mLightID;
#endif
  // _3C
    u8 _40;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_18[3];
    u32 mLightType;
#else
    u32 mLightType;
#endif
  // _44
};