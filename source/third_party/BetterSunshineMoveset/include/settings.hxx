#pragma once

#include <Dolphin/CARD.h>
#include <Dolphin/types.h>

#include <BetterSMS/memory.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/settings.hxx>

using namespace BetterSMS;

extern const u8 gSaveBnr[];
extern const u8 gSaveIcon[];

class LongJumpMappingSetting final : public Settings::IntSetting {
public:
    enum Mapping { BUTTON_Z, BUTTON_L };

    LongJumpMappingSetting()
        : IntSetting("Long Jump/Crouch Button", &mLongJumpValue),
          mLongJumpValue(LongJumpMappingSetting::BUTTON_Z) {
        mValueRange     = {0, 1, 1};
        mValueChangedCB = valueChanged;
    }
    ~LongJumpMappingSetting() override {}

    void getValueName(char *dst) const override {
        switch (mLongJumpValue) {
        case Mapping::BUTTON_Z:
            strncpy(dst, "Z", 2);
            break;
        case Mapping::BUTTON_L:
            strncpy(dst, "L", 2);
            break;
        }
    }

    void load(JSUMemoryInputStream &in) override {
        IntSetting::load(in);
        mValueChangedCB(mValuePtr, mValuePtr,
                        getKind());  // We manually update here to set instructions on load
    }

    // clang-format off
    static void valueChanged(void* old, void* cur, ValueKind kind) {
        if (*reinterpret_cast<int *>(cur) == LongJumpMappingSetting::BUTTON_Z) {
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249670, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249730, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249C34, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024BF30, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024C248, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024C36C, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80252124, 0, 0, 0)), 0x540004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A857C, 0, 0, 0)), 0x57E004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8588, 0, 0, 0)), 0x60001000);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A85A8, 0, 0, 0)), 0x57E004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A85B4, 0, 0, 0)), 0x60002000);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8834, 0, 0, 0)), 0x540006F7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8840, 0, 0, 0)), 0x540006F7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A884C, 0, 0, 0)), 0x41820010);  // Allow L button meaning updates
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8860, 0, 0, 0)), 0x54000673);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A886C, 0, 0, 0)), 0x54000673);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A88C8, 0, 0, 0)), 0x54000673);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A88D4, 0, 0, 0)), 0x54000673);
        } else {
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249670, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249730, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80249C34, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024BF30, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024C248, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8024C36C, 0, 0, 0)), 0x54000427);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x80252124, 0, 0, 0)), 0x540004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A857C, 0, 0, 0)), 0x57E004A5);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8588, 0, 0, 0)), 0x60002000);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A85A8, 0, 0, 0)), 0x57E004E7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A85B4, 0, 0, 0)), 0x60001000);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8834, 0, 0, 0)), 0x54000673);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8840, 0, 0, 0)), 0x54000673);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A884C, 0, 0, 0)), 0x60000000);  // Allow L button meaning updates
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A8860, 0, 0, 0)), 0x540006F7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A886C, 0, 0, 0)), 0x540006F7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A88C8, 0, 0, 0)), 0x540006F7);
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x802A88D4, 0, 0, 0)), 0x540006F7);
        }
    }
    // clang-format on

private:
    int mLongJumpValue;
};

class LongJumpSetting final : public Settings::IntSetting {
public:
    enum Mode { MODE_OFF, MODE_NO_FLUDD, MODE_ON };

    LongJumpSetting() : IntSetting("Long Jump", &mLongJumpValue), mLongJumpValue(MODE_ON) {
        mValueRange = {0, 2, 1};
    }

    void getValueName(char *dst) const override {
        switch (mLongJumpValue) {
        case MODE_OFF:
            snprintf(dst, 4, "OFF");
            break;
        case MODE_NO_FLUDD:
            snprintf(dst, 9, "NO FLUDD");
            break;
        case MODE_ON:
            snprintf(dst, 3, "ON");
            break;
        }
    }

private:
    int mLongJumpValue;
};

class HoverSlideSetting final : public Settings::SwitchSetting {
public:
    HoverSlideSetting()
        : SwitchSetting("Hover Slide", &mHoverSlideActive), mHoverSlideActive(true) {
        mValueChangedCB = valueChanged;
    }
    ~HoverSlideSetting() override {}

    void load(JSUMemoryInputStream &in) override {
        SwitchSetting::load(in);
        mValueChangedCB(mValuePtr, mValuePtr,
                        getKind());  // We manually update here to set instructions on load
    }

    static void valueChanged(void *old, void *cur, ValueKind kind) {
        const u16 dive_anm_id = 136;
        Player::setAnimationData(dive_anm_id, reinterpret_cast<bool *>(cur), nullptr, nullptr,
                                 nullptr);
    }

private:
    bool mHoverSlideActive;
};

class FastTurboSetting final : public Settings::SwitchSetting {
public:
    FastTurboSetting() : SwitchSetting("Fast Turbo", &mFastTurboActive), mFastTurboActive(true) {
        mValueChangedCB = valueChanged;
    }
    ~FastTurboSetting() override {}

    void load(JSUMemoryInputStream &in) override {
        SwitchSetting::load(in);
        mValueChangedCB(mValuePtr, mValuePtr,
                        getKind());  // We manually update here to set instructions on load
    }

    // clang-format off
    static void valueChanged(void* old, void* cur, ValueKind kind) {
        if (*reinterpret_cast<bool *>(cur) == false) {
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8038DC70, 0, 0, 0)), 0x42C80000);
        } else {
            PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8038DC70, 0, 0, 0)), 0x43160000);
        }
    }
    // clang-format on

private:
    bool mFastTurboActive;
};

class FastDiveSetting final : public Settings::SwitchSetting {
public:
    FastDiveSetting()
        : SwitchSetting("Fast Dive/Rollout", &mFastDiveActive), mFastDiveActive(true) {}
    ~FastDiveSetting() override {}

private:
    bool mFastDiveActive;
};

class FallDamageSetting final : public Settings::IntSetting {
public:
    enum Mode { MODE_NORMAL, MODE_DYNAMIC };

    FallDamageSetting()
        : IntSetting("Fall Damage", &mFallDamageValue), mFallDamageValue(MODE_DYNAMIC) {
        mValueRange = {0, 1, 1};
    }

    void getValueName(char *dst) const override {
        switch (mFallDamageValue) {
        case MODE_NORMAL:
            snprintf(dst, 7, "NORMAL");
            break;
        case MODE_DYNAMIC:
            snprintf(dst, 8, "DYNAMIC");
            break;
        }
    }

private:
    int mFallDamageValue;
};

extern Settings::SettingsGroup gSettingsGroup;

extern LongJumpMappingSetting gLongJumpMappingSetting;
extern LongJumpSetting gLongJumpSetting;
extern Settings::SwitchSetting gBackFlipSetting;
extern Settings::SwitchSetting gPoundJumpSetting;
extern Settings::SwitchSetting gWaterPoundSetting;
extern Settings::SwitchSetting gHoverBurstSetting;
extern HoverSlideSetting gHoverSlideSetting;
extern Settings::SwitchSetting gRocketDiveSetting;
extern FastTurboSetting gFastTurboSetting;
extern FastDiveSetting gFastDiveSetting;
extern FallDamageSetting gFallDamageSetting;