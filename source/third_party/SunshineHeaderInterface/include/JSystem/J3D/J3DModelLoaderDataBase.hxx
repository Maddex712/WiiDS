#pragma once

#include <Dolphin/types.h>

#include <JSystem/J3D/J3DMaterial.hxx>
#include <JSystem/J3D/J3DModel.hxx>

class J3DModelLoaderDataBase {
public:
    static J3DModelData *load(const void *, u32);
    static J3DMaterialTable *loadMaterialTable(const void *);
};