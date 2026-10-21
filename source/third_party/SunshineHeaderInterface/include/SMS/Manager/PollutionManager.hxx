#pragma once

#include <Dolphin/types.h>
#include <SMS/Manager/JointModelManager.hxx>

class TPollutionManager : public TJointModelManager {
public:
    struct TPollutionInfo {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[1];
#else
    u8 _pc32_tail[1];
#endif
};
    TPollutionManager(const char *);
    virtual ~TPollutionManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual TJointModel *newJointModel() const override;

    void clean(f32 x, f32 y, f32 z, f32 r);
    bool cleanedAll() const;
    f32 getPollutionDegree() const;
    u32 getPollutionType(f32 x, f32 y, f32 z) const;
    bool isPolluted(f32 x, f32 y, f32 z) const;
    void setDataAddress(TPollutionInfo *info);
    void stamp(u16, f32 x, f32 y, f32 z, f32 r);
    void stampGround(u16, f32 x, f32 y, f32 z, f32 r);
    void stampModel(J3DModel *model);
#if __SIZEOF_POINTER__ == 8
#else
    u32 _24[0x1EC / 4];
#endif


    static u8 mEdgeAlpha;
    static s32 mFlushTime;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[930];
#else
#endif
};

extern TPollutionManager *gpPollution;