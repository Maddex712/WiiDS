#pragma once

#include <Dolphin/GX.h>
#include <Dolphin/types.h>
#include <SMS/MapObj/MapObjGeneral.hxx>
#include <SMS/MoveBG/Item.hxx>

class TNozzleBox : public TMapObjGeneral {
public:
    TNozzleBox(const char *name);
    ~TNozzleBox() override;

    void load(JSUMemoryInputStream &) override;
    void loadAfter() override;
    int receiveMessage(THitActor *, u32) override;

    void makeModelValid();

    u32 mNozzleType;       // 0x0148
    TItem *mNozzleItem;    // 0x014C
    f32 mThrowLength;      // 0x0150
    f32 mThrowHeight;      // 0x0154
    const char *mBoxName;  // 0x0158
    bool mIsValid;         // 0x015C
    GXColorS10 mMatColor;  // 0x015E
    bool mBalloonShown;
};