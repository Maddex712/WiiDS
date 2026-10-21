#pragma once

#include <Dolphin/types.h>

#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Manager/LiveManager.hxx>

class TMActorKeeper {
public:
    TMActorKeeper(TLiveManager *manager);
    TMActorKeeper(TLiveManager *manager, u16);
    ~TMActorKeeper() {}

    MActor *createAndRegister(void *, u32);
    MActor *createMActor(const char *, u32);
    MActor *createMActorFromAllBmd(u32);
    MActor *createMActorFromDefaultBmd(const char *, u32);
    MActor *createMActorFromNthData(int, u32);
    MActor *getMActor(const char *) const;
    MActorAnmData *getMActorAnmData() const;

    void *mModelKeeper;
#if __SIZEOF_POINTER__ == 8
#else
    u16 _04;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 _06;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 *_08;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_4[16];
    MActorAnmData *mAnmData;
#else
    MActorAnmData *mAnmData;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u16 *_10;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[8];
    u32 mModelFlags;
#else
    u32 mModelFlags;
#endif

};