#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Map/JointModel.hxx>

class TJointModelManager : public JDrama::TViewObj {
public:
    TJointModelManager(const char *);
    virtual ~TJointModelManager();

    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual TJointModel *newJointModel() const;

    void initJointModel();
#if __SIZEOF_POINTER__ == 8
    u32 mJointModelNum;
#else
    size_t mJointModelNum;
#endif

    TJointModel **mJointModels;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[8];
    const char *mJointModelsPath;
#else
    const char *mJointModelsPath;
#endif

    MActorAnmData *mAnmData;
};