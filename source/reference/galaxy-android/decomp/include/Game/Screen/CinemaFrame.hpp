#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CinemaFrame : public LayoutActor {
public:
    CinemaFrame(bool param1);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
#ifdef TARGET_PC
    virtual void draw() const;
    f32 getBlackRate() const;
#endif

    void tryScreenToFrame();
    void tryFrameToBlank();
    void tryBlankToFrame();
    void tryFrameToScreen();
    void forceToScreen();
    void forceToFrame();
    void forceToBlank();
    bool isStop() const;
    void exeScreen();
    void exeFrame();
    void exeBlank();
    void exeScreenToFrame();
    void exeFrameToBlank();
    void exeBlankToFrame();
    void exeFrameToScreen();
};
