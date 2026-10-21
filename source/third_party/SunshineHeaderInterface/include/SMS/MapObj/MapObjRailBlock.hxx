#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/MapObj/MapObjRail.hxx>

class TRailBlock : public TRailMapObj {
public:
    TRailBlock(const char *);
    ~TRailBlock() override;

    void control() override;
    void calcRootMatrix() override;
    Mtx44 *getRootJointMtx() const override;
    void initMapObj() override;

    // TODO: add members

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[3];
#else
#endif
};