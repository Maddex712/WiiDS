#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/J3D/J3DCluster.hxx>
#include <JSystem/J3D/J3DJoint.hxx>
#include <JSystem/J3D/J3DNode.hxx>
#include <JSystem/J3D/J3DVertex.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JUtility/JUTNameTab.hxx>

class J3DModelHierarchy;
class J3DNode;

class J3DModelData {
public:
    J3DModelData();
    virtual ~J3DModelData();

    void clear();
    int entryMatColorAnimator(J3DAnmColor *);
    bool entryTevRegAnimator(J3DAnmTevRegKey *);
    bool entryTexMtxAnimator(J3DAnmTextureSRTKey *);
    bool isDeformableVertexFormat() const;
    void makeHierarchy(J3DNode *, const J3DModelHierarchy **);
    bool removeMatColorAnimator(J3DAnmColor *);
    bool removeTevRegAnimator(J3DAnmTevRegKey *);
    bool removeTexMtxAnimator(J3DAnmTextureSRTKey *);
    void setMatColorAnimator(J3DAnmColor *, J3DMatColorAnm *);
    void setTevRegAnimator(J3DAnmTevRegKey *, J3DTevKColorAnm *);
    void setTexMtxAnimator(J3DAnmTextureSRTKey *, J3DTexNtxAnm *);
    void setTexNoAnimator(J3DAnmTexPattern *, J3DTexNoAnm *);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[24];
    u32 mModelFlags;
#else
    u32 mModelFlags;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _1A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[20];
    J3DJoint **mJoints;
#else
    J3DJoint **mJoints;
#endif

    u16 mJointNum;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_10[6];
    J3DTevStage **mStages;
#else
    J3DTevStage **mStages;
#endif
     // ?
    u16 mStageNum;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[6];
             // ?
    JUTNameTab **mJointNames;
#else
             // ?
    JUTNameTab **mJointNames;
#endif
  // JUTNameTab (joints)
    u16 mJointNameNum;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_15[14];
    J3DVertexData mVertexData;
#else
    J3DVertexData mVertexData;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _3C
    u32 _80;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _84;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _86;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u32 _88;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _90;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _94;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_23[40];
    J3DDrawMtxData mDrawMtxData;
#else
    J3DDrawMtxData mDrawMtxData;
#endif
  // _98
    u16 *mCurrentJointIndex;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _A4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _A8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _AC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_28[24];
    u32 _B0;
#else
    u32 _B0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _B4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _B8;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[20];
#else
#endif
};

class J3DModel {
public:
    J3DModel(J3DModelData *, u32, u32);

    virtual void update();
    virtual void entry();
    virtual void calc();
    virtual ~J3DModel();

    void calcBBoard();
    void calcBumpMtx();
    void calcNrmMtx();
    void calcWeightEnvelopeMtx();
    void entryModelData(J3DModelData *, u32, u32);
    void initialize();
    void lock();
    void unlock();
    void makeDL();
    void prepareShapePackets();
    void setSkinDeform(J3DSkinDeform *, J3DDeformAttachFlag);
    void viewCalc();

    J3DModelData *mModelData;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[20];
    TVec3f mBaseScale;
#else
    TVec3f mBaseScale;
#endif

    Mtx mBaseMtx;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[16];
    Mtx *mJointArray;
#else
    Mtx *mJointArray;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // First is the result of concatenations
    u32 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _64;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _68;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _6C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _70;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _74;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _78;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _7C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _80;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // display list
    u32 _84;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _88;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_21[88];
    J3DSkinDeform *mSkinDeform;
#else
    J3DSkinDeform *mSkinDeform;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _90;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _94;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_24[16];
    J3DVertexBuffer *mVtxBuffer;
#else
    J3DVertexBuffer *mVtxBuffer;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _9C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

class J3DModelHierarchy {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};