#include <SMS/Camera/PolarSubCamera.hxx>
#include <SMS/Player/Mario.hxx>

#include <BetterSMS/libs/constmath.hxx>
#include <BetterSMS/module.hxx>

static bool isStaticFOV(CPolarSubCamera *cam) {
    return cam->isNormalDeadDemo() || (*(u32 *)(&cam->mStateFlags) & 0x120000) != 0;
}

static float easeInOut(float t) {
    t = clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static f32 sSlideProgress        = 0.0f;
constexpr f32 MAX_FOV_MULTIPLIER = 1.05f;

static void updateSlideFOV(CPolarSubCamera *cam) {
    if (isStaticFOV(cam)) {
        return;
    }

    f32 targetProgress = 0.0f;

    if (gpMarioAddress->mState == static_cast<u32>(TMario::STATE_DIVESLIDE)) {
        // Map speed from 100.0f - 120.0f into a 0.0f - 1.0f range
        targetProgress = (gpMarioAddress->mForwardSpeed - 100.0f) / 20.0f;
        clamp(targetProgress, 0.0f, 1.0f);

    } else {
        // Automatically drops to 0.0f when exiting the slide state
        targetProgress = 0.0f;
    }

    sSlideProgress    = lerp<f32>(sSlideProgress, targetProgress, 0.05f);
    f32 easedProgress = easeInOut(sSlideProgress);
    f32 finalFactor   = 1.0f + ((MAX_FOV_MULTIPLIER - 1.0f) * easedProgress);

    // 5. Apply the multiplier to the camera
    cam->mProjectionFovy *= finalFactor;
}

static bool updateContexts(CPolarSubCamera *cam) {
    updateSlideFOV(cam);
    return cam->isNormalDeadDemo();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80023828, 0, 0, 0), updateContexts);