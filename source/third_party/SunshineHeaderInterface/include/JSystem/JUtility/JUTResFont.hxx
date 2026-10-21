#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>

#include <JSystem/JKernel/JKRArchivePri.hxx>
#include <JSystem/JUtility/JUTColor.hxx>
#include <JSystem/JUtility/JUTFont.hxx>

class ResFONT {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};

class JUTResFont : public JUTFont {
public:
    JUTResFont(const ResFONT *, JKRArchive *);  // ResFont
    virtual ~JUTResFont();

    virtual void setGX();
    virtual void setGX(JUtility::TColor, JUtility::TColor);
    virtual void drawChar_scale(f32 x, f32 y, f32 w, f32 h, int ascii, bool);
    virtual int getLeading() const;
    virtual int getAscent() const;
    virtual int getDescent() const;
    virtual int getHeight() const;
    virtual int getWidth() const;
    virtual void getWidthEntry(int entry, JUTFont::TWidth *out);
    virtual int getFontType() const;
    virtual bool isLeadByte(int byte) const;

    void convertSjis(int, u16 *);
    void countBlock();
    void initiate(const u32 *, JKRArchive *);  // ResFont
    void loadFont();
    void loadImage(int, GXTexMapID tmapID);

    void protected_initiate();
#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _24[0x44 - 0x24];
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // GXTexObj
    u32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[48];
    ResFONT *mFont;
#else
    ResFONT *mFont;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // padding?
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
    u16 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _5E;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _62;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _64;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _66;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _68;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[56];
#else
#endif
};