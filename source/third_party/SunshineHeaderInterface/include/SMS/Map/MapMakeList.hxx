#pragma once

#include <Dolphin/types.h>
#include <JSystem/JGeometry/JGMVec.hxx>

#include <SMS/Map/BGCheck.hxx>
#include <SMS/Strategic/LiveActor.hxx>

void addAfterPreNode(int cellx, int cellz, TBGCheckList *list, TBGCheckList *addNode, int kind);
TBGCheckList *addGroundNode(TBGCheckList *list, TBGCheckData *data);
TBGCheckList *addRoofNode(TBGCheckList *list, TBGCheckData *data);
TBGCheckList *addWallNode(TBGCheckList *list, TBGCheckData *data);