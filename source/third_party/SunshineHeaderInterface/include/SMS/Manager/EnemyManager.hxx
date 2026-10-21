#pragma once

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <SMS/Enemy/SpineEnemy.hxx>
#include <SMS/M3DUtil/MActor.hxx>
#include <SMS/Manager/LiveManager.hxx>
#include <SMS/Manager/ObjManager.hxx>
#include <SMS/SPC/SpcBinary.hxx>
#include <SMS/System/Params.hxx>

class TLiveActor;

class TSharedMActorSet {
    MActor **mActors;
#if __SIZEOF_POINTER__ == 8
    u32 mActorNum;
#else
    size_t mActorNum;
#endif

    s32 mCurAnmIdx;
};

class TEnemyManager : public TLiveManager {
public:
    TEnemyManager(const char *);
    virtual ~TEnemyManager();

    virtual void load(JSUMemoryInputStream &);
    virtual void perform(u32, JDrama::TGraphics *);
    virtual TSpineEnemy *createEnemyInstance();
    virtual void clipEnemies(JDrama::TGraphics *);
    virtual void restoreDrawBuffer(u32);
    virtual void createEnemies(int);
    virtual void changeDrawBuffer(u32);

    void copyAnmMtx(TSpineEnemy *enemy);
    void copyFromShared();
    s32 countLivingEnemy() const;
    void createSharedMActorSet(const char **models);
    TSpineEnemy *getDeadEnemy();
    TSpineEnemy *getFarOutEnemy();
    TSpineEnemy *getNearestEnemy(const TVec3f &pos);
    TSpineEnemyParams *getSaveParam() const;
    TSharedMActorSet *getSharedMActorSet(int);
    void killChildren();
    void killChildrenWithin(const TVec3f &pos, f32 radius);
    void performShared(u32, JDrama::TGraphics *);
    void setSharedFlags();
    void updateAnmSoundShared();

    TSpineEnemyParams *mEnemyParams;
#if __SIZEOF_POINTER__ == 8
#else
  // _38: 0
    f32 _3C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 1.0f
    u32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 0
    u32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 0
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // 0
    u32 _4c;
#endif

#if __SIZEOF_POINTER__ == 8
#else
                          // -1
    u32 _50;
#endif
                          // 0

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[40];
#else
#endif
};