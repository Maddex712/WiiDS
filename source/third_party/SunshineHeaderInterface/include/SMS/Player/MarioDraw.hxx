#pragma once

#include <Dolphin/types.h>

class TMario;

class TDrawSyncCallback {
public:
    virtual void drawSyncCallback(u16) = 0;
};

struct TMarioAnimeData {
    enum EHand { HAND_A, HAND_B, HAND_C };
    enum EBodyFlags { JIGGLE_PHYS = (1 << 1), FLUDD_GRIP = (1 << 2) };

    int isPumpOK() const;

    u16 mAnimID;
    u16 mAnimFluddID;   // Note: Goes unused except for sentinel case which disables fludd.
    u8 mAnmTexPattern;  // Note: 23 is the max value allowed
    u8 mMarioHand;
    u8 mBodyFlags;
    u8 unk_0;  // Note: Value seems to be set but is never used?
};

extern TMarioAnimeData gMarioAnimeData[336];
extern TMario *gpMarioForCallback;