#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DTextBox.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTFont.hxx>

class J2DPrint {
public:
    J2DPrint(JUTFont *, int, int, JUtility::TColor, JUtility::TColor);
    J2DPrint(JUTFont *, int);
    virtual ~J2DPrint();

    void initiate();
    void private_initiate(JUTFont *, int, int, JUtility::TColor, JUtility::TColor);
    static void setBuffer(u32);
    void locate(int, int);
    void setFontSize();
    void print(int, int, const char *, ...);
    void print(int, int, u8, const char *, ...);
    void getWidth(const char *, ...);
    void printReturn(const char *, int, int, J2DTextBoxHBinding, J2DTextBoxVBinding, int, int, u8);

    JUTFont *mFont;
#if __SIZEOF_POINTER__ == 8
#else
  // _4
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _10[0x38];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[64];
#else
#endif
};