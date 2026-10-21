#pragma once

#include <Dolphin/types.h>
#include <JSystem/J2D/J2DPane.hxx>
#include <JSystem/JSupport/JSUInputStream.hxx>

class J2DScreen : public J2DPane {
public:
    inline J2DScreen() : J2DPane(), mOverlayColor({0, 0, 0, 0}), _EC(0), _EE(0) {}
    inline J2DScreen(u16 id, u32 magic, const JUTRect &rect)
        : J2DPane(id, magic, rect), mOverlayColor({0, 0, 0, 0}), _EC(0), _EE(0) {}
    ~J2DScreen() override;

    void drawSelf(int, int, Mtx *) override;
    J2DPane *search(u32 id) override;

    void makeHiearachyPanes(J2DPane *, JSURandomInputStream *, bool, bool, bool, s32 *);
    u32 makeUserPane(s16, J2DPane *, JSURandomInputStream *);
    u32 makeUserPane(u32, J2DPane *, JSURandomInputStream *);

    void stop();
    void draw(int x, int y, const J2DGrafContext *context);

    u8 _EC;
    u16 _EE;
    JUtility::TColor mOverlayColor;
};
