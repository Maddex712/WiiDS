#pragma once

#include <Dolphin/types.h>
#include <JSystem/JAudio/JAISound.hxx>

class JAIBasic {
public:
    JAIBasic();

    virtual void initStream();
    virtual void makeSound(u32);
    virtual u16 getMapInfoFxline(u32);
    virtual u32 getMapInfoGround(u32);
    virtual f32 getMapInfoFxParameter(u32);
    virtual void sendPlayingSeCommand();
    virtual void sendSeAllParameter(JAISound *);
    virtual void setSeExtParameter(JAISound *);
    virtual bool setRegisterTrackCallback();
    virtual void checkStream();
    virtual void checkNextFrameSe();
    virtual void loadGroupWave(s64);
#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[8];
    u32 *vTable;
#else
    u8 _pc32_1[4];
    u32 *vTable;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _4
    u8 _8[0x38 - 0x8];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_3[72];
    JAISound *mSound;
#else
    JAISound *mSound;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _38
    u8 _3C[0x50 - 0x3C];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_5[32];
    char *mParamWavePath;
#else
    char *mParamWavePath;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _50
    u8 _54[0x64 - 0x54];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[32];
    char *mParamSequenceArchivesPath;
#else
    char *mParamSequenceArchivesPath;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _64
    u8 _64[0x74 - 0x64];
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_9[24];
    char *mParamStreamPath;
#else
    char *mParamStreamPath;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // _74
    u32 _78;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};

static JAIBasic *MSGBasic;