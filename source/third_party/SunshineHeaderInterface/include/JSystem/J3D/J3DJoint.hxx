#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DMaterial.hxx>
#include <JSystem/J3D/J3DNode.hxx>

class J3DJoint : public J3DNode {
public:
    virtual void updateIn() override;
    virtual void updateOut() override;
    virtual void entryIn() override;
    virtual void calcIn() override;
    virtual void calcOut() override;
    virtual u32 getType() const override;
    virtual ~J3DJoint();

    void addMesh(J3DMaterial *);
    void initialize();
#if __SIZEOF_POINTER__ == 8
#else
    u16 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _1B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _24;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _2E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    f32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _34;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3C;
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
#else
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _58;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_21[72];
    J3DMaterial *mMaterial;
#else
    J3DMaterial *mMaterial;
#endif
  // _60
};