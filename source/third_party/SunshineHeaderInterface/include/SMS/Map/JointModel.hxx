#pragma once

#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <SMS/M3DUtil/MActor.hxx>

#include <SMS/Map/JointObj.hxx>

class TJointModelManager;

class TJointModel : public TJointObj {
public:
    TJointModel();

    virtual void perform(u32, JDrama::TGraphics *);

    virtual void initJointModel();
    virtual u32 getJ3DModelFlag() const;

    TJointModelManager *mManager;
    J3DModelData *mModelData;
    J3DModel *mModel;
    MActor *mActor;
};