#pragma once

#include <Dolphin/types.h>

#include <JSystem/J3D/J3DMaterial.hxx>
#include <JSystem/J3D/J3DModel.hxx>

class J3DModelLoader {
public:
    virtual J3DModel *load(const void *, u32);
    virtual J3DMaterialTable *loadMaterialTable(const void *);
    virtual void setupBBoardInfo();
    virtual ~J3DModelLoader();
    virtual void readMaterial(const J3DMaterialBlock *, u32);
    virtual void readMaterial_v21(const J3DMaterialBlock_v21 *, u32);
    virtual void readMaterialTable(const J3DMaterialBlock *, u32);
    virtual void readMaterialTable_v21(const J3DMaterialBlock_v21 *, u32);
};

class J3DModelLoader_v21 : public J3DModelLoader {
public:
    virtual ~J3DModelLoader_v21();
    virtual void readMaterial(const J3DMaterialBlock *, u32);
    virtual void readMaterialTable(const J3DMaterialBlock *, u32);
};

class J3DModelLoader_v26 : public J3DModelLoader {
public:
    virtual ~J3DModelLoader_v26();
    virtual void readMaterial(const J3DMaterialBlock *, u32);
    virtual void readMaterialTable(const J3DMaterialBlock *, u32);
};