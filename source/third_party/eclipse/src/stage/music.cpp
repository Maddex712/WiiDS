#include <JSystem/JParticle/JPAResourceManager.hxx>
#include <SMS/Camera/PolarSubCamera.hxx>
#include <SMS/MSound/MSBGM.hxx>
#include <SMS/MSound/MSound.hxx>
#include <SMS/MSound/MSoundSESystem.hxx>
#include <SMS/Manager/FlagManager.hxx>
#include <SMS/raw_fn.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/music.hxx>

#include "settings.hxx"
#include "stage.hxx"

class MSStage {
public:
    static MSStage *init(u8 area, u8 episode);
    static MSStage *instance() { return smMSStage; }

    virtual void setPosPtr(Vec *pos);
    virtual void stageLoop();
    virtual void proc() = 0;

protected:
    static MSStage *smMSStage;
};

class MSStageDistFade : public MSStage {
public:
    MSStageDistFade(const Vec *posptr, float radius, float height, u32, bool distfade);
    ~MSStageDistFade() {}

    void proc() override;

protected:
    u32 mFrameCounter;
    f32 mBlendRadiusEnd;
    f32 mBlendRadiusStart;
    Vec *mPosPtr;
    u32 mMinBlendFrame;
    bool mDistFade;
};

class MSStageDistFadeStream : public MSStageDistFade {
public:
    MSStageDistFadeStream(const Vec *posptr, float radius, float height, u32 unk_0, bool distfade)
        : MSStageDistFade(posptr, radius, height, unk_0, distfade), mFadedPermanently(false) {}
    ~MSStageDistFadeStream(){};

    void proc() override {
        if (Music::isPaused()) {
            return;
        }

        MSBgm *handle = (MSBgm *)MSBgm::getHandle(1);
        if (!Music::isPlaying()) {
            // The Shadow Mario theme starts with the stage at full volume, but the
            // stage song only streams from GO!. Hold the theme at its distance
            // blend until then so it doesn't blare before the crossfade takes over.
            if (handle != nullptr) {
                MSBgm::setTrackVolume(1, distanceBlend(), 0, 0);
            }
            return;
        }

        if (handle == nullptr) {
            if (mFrameCounter++ >= 150 && !mFadedPermanently) {
                Music::setVolumeFade(Music::getMaxVolume(), 0.4f);
                mFadedPermanently = true;
            }
            return;
        }

        f32 blend = distanceBlend();

        f32 *timingTable = (f32 *)sms_mod_rawdata(0x803ACA68);
        f32 *expTable    = (f32 *)sms_mod_rawdata(0x803ACAB0);

        u8 index = 0xFF;
        for (size_t i = 0; i < 0x11; ++i) {
            if (timingTable[i] <= blend && blend < timingTable[i + 1]) {
                index = i;
                break;
            }
        }

        MSBgm::setTrackVolume(1, blend, expTable[index], 0);

        f32 scaler = 1.0f - blend;
        u8 volume  = static_cast<f32>(Music::getMaxVolume()) * scaler;
        Music::setVolumeFade(volume, 0.1f);
    }

private:
    f32 distanceBlend() const {
        f32 distance = PSVECDistance(*gpMarioPos, mPosPtr);
        if (distance <= mBlendRadiusStart) {
            return 1.0f;
        }
        if (distance <= mBlendRadiusEnd) {
            return (mBlendRadiusEnd - distance) / (mBlendRadiusEnd - mBlendRadiusStart);
        }
        return 0.0f;
    }

    bool mFadedPermanently;
};

// The statics these retail addresses hold (0x8040E1EC..0x8040E204), by name.
namespace MSMainProc {
    namespace MSStageInfo {
        extern u32 demoBgm;
        extern u8 flags;
        extern u16 volOffCategory;
        extern u8 fadeEvent;
        extern u32 switchBgm;
        extern u32 switchBgm2;
    }  // namespace MSStageInfo
}  // namespace MSMainProc

static bool setMSoundEnterStageExtended(TFlagManager *manager, u32 flag) {
    if (gpMarDirector->mAreaID >= SME::STAGE_ERTO &&
        gpMarDirector->mAreaID <= SME::STAGE_LACRIMA_BACKHOUSE) {
        if (gpMarDirector->mEpisodeID == 6) {
            MSMainProc::MSStageInfo::switchBgm = -16;       // 0x8040E200
            MSMainProc::MSStageInfo::switchBgm2 = -16;      // 0x8040E204
            MSMainProc::MSStageInfo::volOffCategory += -0x83;  // 0x8040E1FA
            MSMainProc::MSStageInfo::demoBgm = BGM_KAGEMARIO;  // 0x8040E1EC
            MSMainProc::MSStageInfo::flags  = 6;            // 0x8040E1F9
            MSMainProc::MSStageInfo::fadeEvent  = 1;        // 0x8040E1FC
            OSReport("MSoundEnterStageExtended: Episode 6\n");
        }
    }
    return manager->getBool(flag);
}
SMS_PATCH_BL(0x802B7A5C, setMSoundEnterStageExtended);

static void *setInitMSStage(u8 area, u8 episode) {
    bool excluded = area == SME::STAGE_LACRIMA || area == SME::STAGE_LACRIMA_INSIDE;
    if (!excluded && area >= SME::STAGE_ERTO && area <= SME::STAGE_LACRIMA_BACKHOUSE) {
        if (episode == 6) {
            Vec *posptr   = *(Vec **)((u8 *)gpMSound + SMS_OFFSET(MSound, 0xBC));
            auto *msstage = new MSStageDistFadeStream(posptr, 6000.0f, 1600.0f, 0x5A, true);
            return (void *)msstage;
        }
    }
    return (void *)init__7MSStageFUcUc(area, episode);
}
SMS_PATCH_BL(0x8029cb4c, setInitMSStage);