#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUInputStream.hxx>
#include <JSystem/JSupport/JSUList.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTRect.hxx>

enum J2DBasePosition { UNK_BASEPOS_4 = 4, UNK_BASEPOS_5 = 5 };

class J2DGrafContext {
public:
    J2DGrafContext(int, int, int, int);
    J2DGrafContext(const JUTRect &);
    ~J2DGrafContext();

    void place(const JUTRect &);
    void place(int, int, int, int);
    void setPort();
    void setup2D();
    void setScissor();
    void setLookat();

    void scissor(const JUTRect &);
    void fillBox(const JUTRect &);

    u32 *vTable;
#if __SIZEOF_POINTER__ == 8
#else
  // _0
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
       // padding?
    JUTRect _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUTRect _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _34;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[52];
    u8 mLineWidth;
#else
    u8 mLineWidth;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _38
    u8 _39;
#endif

#if __SIZEOF_POINTER__ == 8
#else
         // padding?
    u8 _3A;
#endif

#if __SIZEOF_POINTER__ == 8
#else
         // ^^
    u8 _3B;
#endif

#if __SIZEOF_POINTER__ == 8
#else
         // ^^
    u32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _44[0xB4 - 0x44];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // who knows what's here
    u32 _B4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _B8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _BC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _CC;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _D0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _D4;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[159];
#else
#endif
};

class J2DPane {
public:
    J2DPane();
    J2DPane(J2DPane *, s16, bool, u32, const JUTRect &);
    J2DPane(u16, u32, const JUTRect &);
    J2DPane(J2DPane *, JSURandomInputStream *, bool);
    virtual ~J2DPane();

    virtual void move(int, int);
    virtual void add(int, int);
    virtual void resize(int, int);
    virtual bool setConnectParent(bool);
    virtual void drawSelf(int, int);
    virtual void drawSelf(int, int, Mtx *);
    virtual J2DPane *search(u32 id);
    virtual void makeMatrix(int, int);

    void draw(int, int, const J2DGrafContext *, bool);
    void clip(const JUTRect &);
    void setCullBack(GXCullback cullback);
    void setBasePosition(J2DBasePosition basePos);

    u16 mTypeID;
    u32 mTypeMagic;   // _8
    bool mIsVisible;  // _C
    u32 mTag;
    JUTRect mRect;          // _14
    JUTRect mCRect;         // _24
    JUTRect mClipRect;      // _34
    JUTRect mScissorBound;  // _44
    Mtx mScreenMtx;
#if __SIZEOF_POINTER__ == 8
#else
    u8 _84[0xB4 - 0x84];
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
#else
    u8 _BC;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_13[60];
    f32 mRotation;
#else
    f32 mRotation;
#endif
                  // _C0
    J2DBasePosition mBasePosition;  // _C4
    GXCullback mCullingMode;        // _C8
    u8 mAlpha;
    u8 mAlphaCopy;
#if __SIZEOF_POINTER__ == 8
#else
    u8 _CE;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_19[1];
    bool mConnectParent;
#else
    bool mConnectParent;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_20[4];
    JSUPtrList mChildrenList;
#else
    JSUPtrList mChildrenList;
#endif

    JSUPtrLink mPtrLink;
};

void J2DFillBox(int x, int y, int w, int h, JUtility::TColor color);
void J2DFillBox(JUTRect rect, JUtility::TColor color);