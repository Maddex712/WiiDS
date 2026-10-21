#pragma once

#include <Dolphin/types.h>

struct JAIMoveParaSet {
#if __SIZEOF_POINTER__ == 8
#else
    f32 _0;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[16];
#else
#endif
};

class JAISound {
public:
    JAISound();

    virtual void setSeDistanceParameters();
    virtual void setSeDistanceVolume(u8);
    virtual void setSeDistancePan(u8);
    virtual void setSeDistancePitch(u8);
    virtual void setSeDistanceFxmix(u8);
    virtual void setSeDistanceFir(u8);
    virtual void setSeDistanceDolby(u8);
    virtual void setSePositionDopplar();
    virtual void setPositionDopplarCommon(u32);
    virtual void setDistanceVolumeCommon(f32, u8);
    virtual void setDistancePanCommon();

    u32 checkSwBit(u32);
    void clearMainSoundPPointer();
    u8 getInfoPriority();
    u32 getSeCategoryNumber();
    u32 *getSeParameter();
    f32 getSeqInterVolume(u8);
    u32 *getSeqParameter();
    f32 getStreamInterVolume(u8);
    u32 *getStreamParameter();
    u32 getSwBit();
    u32 getTrackPortRoute(u8, u8);
    bool initMoveParameter(JAIMoveParaSet *, f32, u32);
    void release();
    void setDolby(f32, u32, u8);
    void setFxmix(f32, u32, u8);
    void setPan(f32, u32, u8);
    void setPauseMode(u8, u8);
    void setPitch(f32 pitch, u32 interpolation, u8 slot);
    void setPortData(u8, u16);
    void setSeInterDolby(u8, f32, u32, u8);
    void setSeInterFxmix(u8, f32, u32, u8);
    void setSeInterPan(u8, f32, u32, u8);
    void setSeInterPitch(u8, f32, u32, u8);
    void setSeInterVolume(u8, f32, u32, u8);
    void setSePortData(u8, u16);
    void setSeqDolby(f32, u32, u8);
    void setSeqFxmix(f32, u32, u8);
    void setSeqPan(f32, u32, u8);
    void setSeqPauseMode(u8, u8);
    void setSeqPitch(f32, u32, u8);
    void setSeqPortData(u8, u16, u32);
    void setSeqTempoProportion(f32, u32);
    void setStreamInterPan(u8, f32, u32);
    void setStreamInterPitch(u8, f32, u32);
    void setStreamInterVolume(u8, f32, u32);
    void setTempoProportion(f32, u32);
    void setTrackInterruptSwitch(u8, u8);
    void setTrackPortData(u8, u8, u16);
    void setVolume(f32, u32, u8);
    void setWaveReadMode(s32, s32);
    void stop(u32);
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_0[4];
    u8 _0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[5];
    u8 _1;
#else
    u8 _1;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _2;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _3;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[6];
    u32 mID;
#else
    u32 mID;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _8
    u8 _C[0x18 - 0xC];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _18;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _1C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _24[0x34 - 0x24];
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_34;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[80];
           // sound pointer, maybe?
    u32 *mStreamParam;
#else
           // sound pointer, maybe?
    u32 *mStreamParam;
#endif

#if __SIZEOF_POINTER__ == 8
#else
#endif

    u32 *vTable;  // _40
};