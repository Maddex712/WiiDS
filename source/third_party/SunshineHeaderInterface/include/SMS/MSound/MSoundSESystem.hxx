#pragma once

#include <Dolphin/types.h>
#include <JSystem/JAudio/JAISound.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

class JAIActor {
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};

namespace MSoundSESystem {
    class MSoundSE {
    public:
        static bool checkMonoSound(u32, JAIActor *);
        static bool checkSoundArea(u32, const Vec &);
        static void construct();
        static u32 getNewIDBySurfaceCode(u32, JAIActor *);
        static u32 getRandomID(u32);
        static JAISound *startSoundActor(u32, const Vec *, u32, JAISound **, u32, u8);
        static JAISound *startSoundActorInner(u32, JAISound **, JAIActor *, u32, u8);
        static JAISound *startSoundActorWithInfo(u32, const Vec *, Vec *, f32, u32, u32,
                                                 JAISound **, u32, u8);
        static JAISound *startSoundNpcActor(u32, const Vec *, u32, JAISound **, u32, u8);
        static JAISound *startSoundSystemSE(u32 soundID, u32, JAISound **, u32);
    };
};  // namespace MSoundSESystem

using namespace MSoundSESystem;