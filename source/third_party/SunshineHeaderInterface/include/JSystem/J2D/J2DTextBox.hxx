#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JSupport/JSUInputStream.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTFont.hxx>
#include <JSystem/JUtility/JUTResFont.hxx>

enum class J2DTextBoxHBinding { Center, Right, Left };

enum class J2DTextBoxVBinding { Center, Bottom, Top };

class J2DTextBox : public J2DPane {
public:
    J2DTextBox() : J2DPane() {}
    J2DTextBox(const ResFONT *, const char *);
    J2DTextBox(J2DPane *, JSURandomInputStream *, bool);
    J2DTextBox(u32, const JUTRect &, const ResFONT *, const char *, J2DTextBoxHBinding,
               J2DTextBoxVBinding);
    virtual ~J2DTextBox();

    virtual void resize(int, int);
    virtual bool setConnectParent(bool);
    virtual void drawSelf(int, int);
    virtual void drawSelf(int, int, Mtx *);

    void initiate(const ResFONT *, const char *, J2DTextBoxHBinding, J2DTextBoxVBinding);

    char *getStringPtr() const;
    size_t setString(const char *, ...);
    void setFont(JUTFont *);
    void draw(int, int);

    JUTFont *mFont;  // _EC
    JUtility::TColor mGradientTop;
    JUtility::TColor mGradientBottom;
    J2DTextBoxHBinding mHBinding;  // _F8
    J2DTextBoxVBinding mVBinding;  // _FC
    char *mStrPtr;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _104;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _108;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[8];
    u32 mCharSpacing;
#else
    size_t mCharSpacing;
#endif

#if __SIZEOF_POINTER__ == 8
    u32 mNewlineSize;
#else
    size_t mNewlineSize;
#endif

#if __SIZEOF_POINTER__ == 8
    u32 mCharSizeX;
#else
    size_t mCharSizeX;
#endif

#if __SIZEOF_POINTER__ == 8
    u32 mCharSizeY;
#else
    size_t mCharSizeY;
#endif

    JUtility::TColor mColorMask;
    JUtility::TColor mAdvanceColor;
    bool mInitialized;
};