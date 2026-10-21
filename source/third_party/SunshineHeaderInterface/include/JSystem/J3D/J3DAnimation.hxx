#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DTransform.hxx>
#include <JSystem/JUtility/JUTNameTab.hxx>

class J3DTexNoAnm {
public:
    u32 *vTable;
#if __SIZEOF_POINTER__ == 8
#else
  // _0
    u16 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _6;
#endif
  // padding?

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

class J3DAnmColor {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[9];
#else
    u8 _pc32_tail[5];
#endif
};

class J3DAnmTevRegKey {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[9];
#else
    u8 _pc32_tail[5];
#endif
};

class J3DAnmTextureSRTKey {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[9];
#else
    u8 _pc32_tail[5];
#endif
};

class J3DMatColorAnm {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class J3DTevKColorAnm {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class J3DTexNtxAnm {};

class J3DAnmKeyTableBase {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class J3DAnmClusterKey {
public:
    virtual ~J3DAnmClusterKey();
    virtual f32 getWeight(u16) const;
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_0[4];
    f32 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _08;
#endif

#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 *_10;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[24];
    J3DAnmKeyTableBase *mKeyTableBase;
#else
    J3DAnmKeyTableBase *mKeyTableBase;
#endif

};

class J3DAnmCluster : public J3DAnmClusterKey {
public:
    virtual ~J3DAnmCluster();
    virtual f32 getWeight(u16) const;
};

class J3DAnmClusterFull : J3DAnmCluster {
public:
    virtual ~J3DAnmClusterFull();
    virtual f32 getWeight(u16) const;
};

class J3DAnmTransformKey {
public:
    virtual ~J3DAnmTransformKey();
    virtual void getTransform(u16, J3DTransformInfo *) const;
    virtual void calcTransform(f32, u16, J3DTransformInfo *) const;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
    u8 _pc32_tail[4];
#endif
};

class J3DAnmTransform : J3DAnmTransformKey {
public:
    virtual ~J3DAnmTransform();
    virtual void getTransform(u16, J3DTransformInfo *) const override;
};

class J3DAnmTransformFull : J3DAnmTransform {
public:
    virtual ~J3DAnmTransformFull();
    virtual void getTransform(u16, J3DTransformInfo *) const override;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
    u8 _pc32_tail[4];
#endif
};

class J3DFrameCtrl {
public:
    enum Control { ONCE, ONCE_RESET, LOOP, OSCILLATE, OSCILLATE_LOOP };

    J3DFrameCtrl();
    virtual ~J3DFrameCtrl();

    bool checkPass(f32);
    void init(s16);
    void update();

    u8 mAnimState;  // anim state
    struct {
        u8 mIsAnmDead     : 1;
        u8 mIsAnmReversed : 1;
        u8 _00            : 6;
    } mAnimFlags;
#if __SIZEOF_POINTER__ == 8
#else
    s16 _06;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[2];
    s16 mNumFrames;
#else
    s16 mNumFrames;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    s16 _0A;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[2];
    f32 mFrameRate;
#else
    f32 mFrameRate;
#endif

    f32 mCurFrame;
};

class J3DAnmBase {
public:
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_0[4];
    u16 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif
       // guessed
    u32 *vTable;  // _C

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};

class J3DAnmTexPattern : public J3DAnmBase {
public:
#if __SIZEOF_POINTER__ == 8
#else
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
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[36];
    JUTNameTab *mNameTab;
#else
    JUTNameTab *mNameTab;
#endif
  // _20
};