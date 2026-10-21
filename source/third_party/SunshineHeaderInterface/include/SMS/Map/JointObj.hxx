#pragma once

#include <Dolphin/types.h>
#include <JSystem/J3D/J3DJoint.hxx>

// JointObj represents joints of a model that can be slept or awoke

class TJointObj {
public:
    TJointObj();

    virtual void initJointObj(J3DJoint *);
    virtual void alive();
    virtual void awake();
    virtual void stand();
    virtual void kill();
    virtual void sleep();
    virtual void sit();
    virtual TJointObj *newJointObj() const;

    void initChildren();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _08;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _0C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[16];
    size_t mChildNum;
#else
    size_t mChildNum;
#endif

    TJointObj **mChildren;
    size_t mUnkNum;
#if __SIZEOF_POINTER__ == 8
#else
    void **_1C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};