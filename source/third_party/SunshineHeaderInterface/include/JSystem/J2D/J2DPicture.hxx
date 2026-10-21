#pragma once

#include <Dolphin/MTX.h>
#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JSupport/JSUInputStream.hxx>
#include <JSystem/JUtility/JUTTexture.hxx>

enum J2DBinding {
    BindingBottom = (1 << 0),
    BindingTop    = (1 << 1),
    BindingRight  = (1 << 2),
    BindingLeft   = (1 << 3),
};

enum J2DMirrorFlag { MirrorX = (1 << 0), MirrorY = (1 << 1) };

enum J2DWrapMode {
    WrapNone,
    WrapClamp,
    WrapRepeat,
    WrapMirror,
};

class J2DPicture : public J2DPane {
public:
    J2DPicture(u32 magic, const JUTRect &rect)
        : J2DPane(18, magic, rect), mPalette(nullptr), mColorMask({255, 255, 255, 255}),
          mColorOverlay({0, 0, 0, 0}), mTextureCount(0), mVertexColors() {
        mTypeMagic   = 'PIC1';
        mBinding     = BindingBottom | BindingTop | BindingRight | BindingLeft;
        mMirrorFlags = 0;
        mRotate90    = false;
        setBlendKonstColor();
        setBlendKonstAlpha();
    }
    J2DPicture(J2DPane *, JSURandomInputStream *, bool);
    ~J2DPicture() override;

    void drawSelf(int, int) override;
    void drawSelf(int, int, Mtx *) override;

    void changeTexture(const ResTIMG *timg, u8);
    void drawFullSet(int x, int y, int w, int h, J2DBinding binding, u32, bool, J2DWrapMode wrapH,
                     J2DWrapMode wrapV, Mtx *);
    void draw(int x, int y, int w, int h, bool invertX, bool invertY, bool rotate90);
    void drawTexCoord(int x, int y, int w, int h, f32, f32, f32, f32, f32, f32, f32, f32, Mtx *);
    void insert(JUTTexture *texture, u8 index, f32);
    void remove(u8);
    void setTevMode();
    void swap(f32 &, f32 &);
    void setBlendKonstColor();
    void setBlendKonstAlpha();

    JUTTexture *mTextures[4];
    u8 mTextureCount;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _100;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _104;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _108;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _10C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _110;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _114;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _118;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _11C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _120;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[39];
    JUTPalette *mPalette;
#else
    JUTPalette *mPalette;
#endif

    u32 mBinding;
    u32 mMirrorFlags;
    bool mRotate90;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _134;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _138;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_17[11];
    JUtility::TColor mColorMask;
#else
    JUtility::TColor mColorMask;
#endif

    JUtility::TColor mColorOverlay;
    JUtility::TColor mVertexColors[4];
#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _154;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JUtility::TColor _158;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};