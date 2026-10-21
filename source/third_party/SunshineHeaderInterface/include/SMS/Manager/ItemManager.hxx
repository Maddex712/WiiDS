#include <Dolphin/types.h>
#include <SMS/Manager/MapObjBaseManager.hxx>
#include <SMS/MoveBG/Item.hxx>

class TItemManager : public TMapObjBaseManager {
public:
    TItemManager(const char *);
    virtual ~TItemManager();

    virtual void load(JSUMemoryInputStream &) override;
    virtual void loadAfter() override;
    virtual void perform(u32, JDrama::TGraphics *) override;
    virtual int hasMapCollision() const override;

    void makeShineAppearWithDemo(const char *, const char *, f32, f32, f32);
    void makeShineAppearWithDemoOffset(const char *, const char *, f32, f32, f32);
    void makeShineAppearWithTime(const char *, int, f32, f32, f32, int, int, int);
    void resetNozzleBoxesModel(int);

    static TItem *newAndRegisterCoin(u32);
    static TItem *newAndRegisterCoinReal();
#if __SIZEOF_POINTER__ == 8
#else
    f32 _40;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _44;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _58;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _5C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _60;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _64;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _68;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _6C;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_12[48];
    f32 _70;
#else
    f32 _70;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _74;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    f32 _78;
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};

extern TItemManager *gpItemManager;