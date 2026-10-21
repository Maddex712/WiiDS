#pragma once

#include <Dolphin/types.h>

class J3DModelData;

class J3DNode {
public:
    J3DNode();

    virtual void init(J3DModelData *);
    virtual void updateIn();
    virtual void updateOut();
    virtual void entryIn();
    virtual void calcIn();
    virtual void calcOut();
    virtual u32 getType() const;
    virtual ~J3DNode();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
    int (*mCallback)(J3DNode *, int);
#else
    int (*mCallback)(J3DNode *, int);
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
    u32 *_10;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[16];
    J3DNode *mChildNode;
#else
    J3DNode *mChildNode;
#endif
  // _14
};