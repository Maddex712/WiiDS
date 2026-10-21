#include <Dolphin/types.h>
#include <SMS/Manager/LiveManager.hxx>
#include <SMS/MapObj/MapObjBase.hxx>

class TMapObjBaseManager : public TLiveManager {
public:
    TMapObjBaseManager(const char *);
    virtual ~TMapObjBaseManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void clipActors(JDrama::TGraphics *) override;
    virtual void createModelData() override;

    u32 getObjNumWithActorType(u32) const;
    TLiveActor *makeObjAppear(f32, f32, f32, u32, bool);
    TLiveActor *makeObjAppear(u32);
    void makeObjAppeared(u32);

    static u32 getActorTypeByEventID(u32);
    static TMapObjBase *newAndRegisterObj(const char *, const TVec3f &, const TVec3f &,
                                          const TVec3f &);
    static TMapObjBase *newAndRegisterObjByEventID(u32, const char *);
#if __SIZEOF_POINTER__ == 8
#else
    f32 _38;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _3C;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[8];
#else
#endif
};