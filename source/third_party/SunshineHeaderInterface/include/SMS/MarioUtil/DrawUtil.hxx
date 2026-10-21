#pragma once

#include <Dolphin/types.h>

#include <JSystem/J3D/J3DMaterial.hxx>
#include <JSystem/J3D/J3DModel.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

bool ViewFrustumClipCheck(JDrama::TGraphics *, Vec *, f32);
void SetViewFrustumClipCheck(f32, f32, f32, f32, f32, f32);
void SetViewFrustumClipCheckPerspective(f32, f32, f32, f32);

void SMS_AddDamageFogEffect(J3DModelData *, const TVec3f &, JDrama::TGraphics *);
void SMS_CalcMatAnmAndMakeDL(J3DModel *, u16);
size_t SMS_CountPolygonNumInShape(J3DShape *);
void SMS_DrawCube(const TVec3f &, const TVec3f &);
void SMS_DrawInit();
void SMS_DrawShape(J3DModelData *, u16);
void SMS_SettingDrawShape(J3DModelData *, u16);
void SMS_MakeDLAndLock(J3DModel *);
void SMS_ResetDamageFogEffect(J3DModelData *);
void SMS_ShowJoint(J3DMaterial *, bool show);
void SMS_UnifyMaterial(J3DModel *);