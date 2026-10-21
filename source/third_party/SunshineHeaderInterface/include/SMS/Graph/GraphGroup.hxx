#pragma once

#include <Dolphin/types.h>
#include <SMS/Graph/GraphWeb.hxx>

#include <JSystem/JDrama/JDRGraphics.hxx>

class TGraphGroup {
public:
    TGraphGroup(void *nameOfsArray);

    TGraphWeb *getGraphByName(const char *);
    void initGraphGroup();
    void perform(u32, JDrama::TGraphics *);

    s32 *mNameOffsets;
    size_t mRailNum;
    TGraphWeb **mRails;
    TGraphWeb *mNullRail;
};