#pragma once

#include <Dolphin/types.h>
#include <JSystem/JUtility/JUTColor.hxx>

class JUTFont {
public:
    struct TWidth {
        char mMargin;
        char mWidth;
    };

    JUTFont();
    virtual ~JUTFont();

    virtual void setGX() = 0;
    virtual void setGX(JUtility::TColor, JUtility::TColor);
    virtual void drawChar_scale(f32 x, f32 y, f32 w, f32 h, int ascii, bool) = 0;
    virtual int getLeading() const                                          = 0;
    virtual int getAscent() const                                            = 0;
    virtual int getDescent() const                                           = 0;
    virtual int getHeight() const                                            = 0;
    virtual int getWidth() const                                             = 0;
    virtual void getWidthEntry(int entry, JUTFont::TWidth *out)              = 0;
    virtual int getFontType() const                                          = 0;
    virtual bool isLeadByte(int byte) const                                  = 0;

    void drawString_size_scale(f32, f32, f32, f32, const char *, u32, bool);
    void initiate();  // nullsub
    virtual bool isLeadByte_1Byte(int byte);
    virtual bool isLeadByte_2Byte(int byte);
    virtual bool isLeadByte_ShiftJIS(int byte);
    void setCharColor(JUtility::TColor color);
    void setGradColor(JUtility::TColor top, JUtility::TColor bottom);
#if __SIZEOF_POINTER__ == 8
#else
    u8 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_2[8];
    JUtility::TColor mGradColorTopL;
#else
    JUtility::TColor mGradColorTopL;
#endif

    JUtility::TColor mGradColorTopR;
    JUtility::TColor mGradColorBottomL;
    JUtility::TColor mGradColorBottomR;
};