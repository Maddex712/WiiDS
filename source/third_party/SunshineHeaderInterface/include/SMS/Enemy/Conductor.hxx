#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/List.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <SMS/Graph/GraphGroup.hxx>
#include <SMS/Graph/GraphWeb.hxx>
#include <SMS/Manager/LiveManager.hxx>
#include <SMS/Strategic/LiveActor.hxx>
#include <SMS/System/BaseParam.hxx>
#include <SMS/System/Params.hxx>

class TConductor : public JDrama::TNameRef {
public:
    class TConductorParams : public TParams {
        TConductorParams(const char *prm)
            : SMS_TPARAM_INIT(mEnemyFarClip, 5000.0f), SMS_TPARAM_INIT(mGenerateRadiusMax, 1000.0f),
              SMS_TPARAM_INIT(mGenerateRadiusMin, 200.0f), SMS_TPARAM_INIT(mGenerateTime, 300),
              SMS_TPARAM_INIT(mGenerateProp, 0.01f) {
            load(prm);
        }

        TParamT<f32> mEnemyFarClip;
        TParamT<f32> mGenerateRadiusMax;
        TParamT<f32> mGenerateRadiusMin;
        TParamT<s32> mGenerateTime;
        TParamT<f32> mGenerateProp;
    };

    struct TConductorPacket {
        size_t _00;
        void *_04;
        void *_08;
        void *_0C;
    };

    TConductor();
    virtual ~TConductor();

    virtual JDrama::TNameRef *searchF(u16, const char *) override;
    virtual void perform(u32, JDrama::TGraphics *);

    void genEnemyFromPollution();
    TGraphWeb *getGraphByName(const char *name);
    TLiveManager *getManagerByName(const char *name);
    void init();
    void initGraphGroup();
    bool isBossDefeated();
    void killEnemiesWithin(const TVec3f &pos, f32 radius);
    void *makeEnemyAppear(const TVec3f &pos, const char *name, int, int);
    void makeGraphGroup(void *);
    void *makeOneEnemyAppear(const TVec3f &pos, const char *name, int);
    void registerAloneActor(TLiveActor *actor);
    void registerAreaCylinderManager(void * /*TAreaCylinderManager*/);
    void registerEnemyInfoTable(void * /*TStageEnemyInfoTable*/);
    void registerEnemyManager(void * /*TEnemyManager*/);
    void registerGenerator(void * /*TGenerator*/);
    void registerManager(TLiveManager *);
    void registerOtherObj(JDrama::TViewObj *);
    void registerSDLModelData(void * /*SDLModelData*/);

    u32 _0C;
#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _20;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    JGadget::TList<TLiveActor *> _70;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_8[170];
    TGraphGroup *mGraphGroup;
#else
    TGraphGroup *mGraphGroup;
#endif

    TConductorParams mParams;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _F0;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_11[8];
    void *mNpcParams;
#else
    void *mNpcParams;
#endif

#if __SIZEOF_POINTER__ == 8
#else
  // TNpcParams *
    u32 _F8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _FC;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};

extern TConductor *gpConductor;
