#pragma once

#include <Dolphin/types.h>
#include <SMS/Graph/GraphNode.hxx>
#include <SMS/Graph/RailNode.hxx>
#include <SMS/Graph/SplineRail.hxx>

class TGraphWeb {
public:
    TGraphNode *mNodes;    // 0x0000
    TRailNode *mRailNode;  // 0x0004
    s32 mNodeCount;        // 0x0008
    char *mRailName;       // 0x000C
    s32 mNextNode;         // 0x0010 ?
    TSplineRail *mSplineRail;

    TGraphWeb(TRailNode *, const char *, int);
    virtual ~TGraphWeb();

    void calcGraphDirection(int);
    void filterRailNode(u32, const TRailNode *, const TRailNode *, TRailNode *) const;
    int findNearestNodeIndex(const TVec3f &, u32) const;
    int findNearestVisibleIndex(const TVec3f &, f32, f32, f32, u32) const;
    int getAimToDirNextIndex(int, int, const TVec3f &, const TVec3f &, u32) const;
    s16 getEscapeDirLimited(int, int, const TVec3f &, const TVec3f &, f32, u32) const;
    int getEscapeFromMarioIndex(int, int, const TVec3f &, u32) const;
    TVec3f getNearestPosOnGraphLink(const TVec3f &) const;
    int getNeighborNodeIndexByFlag(int, int, u32) const;
    int getRandomButDirLimited(int, int, const TVec3f &, const TVec3f &, f32, u32) const;
    int getRandomNextIndex(int, int, u32) const;
    int getShortestNextIndex(int, int, u32) const;
    TVec3f indexToPoint(int) const;
    void initGoalIndex(const Vec &);
    int isDummy() const;
    bool startIsEnd() const;
    void translateNodes(TRailNode *);
};