#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRPlacement.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JStage/JSGCamera.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

namespace JDrama {

    class TCamera : public TPlacement, public JStage::TCamera {
    public:
        TCamera(f32 near, f32 far, const char *name) : TPlacement(name) {
            mFlag           = 0;
            mProjectionNear = near;
            mProjectionFar  = far;
        }
        virtual ~TCamera();

        virtual u32 getType() const;
        virtual u32 JSGGetFlag() const;
        virtual void JSGSetFlag(u32 flag);
        virtual f32 JSGGetProjectionNear() const;
        virtual void JSGSetProjectionNear(f32 projectionNear);
        virtual f32 JSGGetProjectionFar() const;
        virtual void JSGSetProjectionFar(f32 projectionFar);

        u16 mFlag;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_1[2];
            // 24
        f32 mProjectionNear;
#else
            // 24
        f32 mProjectionNear;
#endif
  // 28
        f32 mProjectionFar;   // 2C
    };

    class TLookAtCamera : public TCamera {
    public:
        virtual ~TLookAtCamera();

        virtual void perform(u32, TGraphics *);

        virtual s32 JSGGetProjectionType() const;
        virtual void JSGSetProjectionType(JStage::TECameraProjection);
        virtual f32 JSGGetProjectionFovy() const;
        virtual void JSGSetProjectionFovy(const f32 projectionFovy);
        virtual f32 JSGGetProjectionAspect() const;
        virtual void JSGSetProjectionAspect(const f32 projectionAspect);
        virtual void JSGGetViewPosition(Vec *) const;
        virtual void JSGSetViewPosition(const Vec &viewPos);
        virtual void JSGGetViewUpVector(Vec *) const;
        virtual void JSGSetViewUpVector(const Vec &upVector);
        virtual void JSGGetViewTargetPosition(Vec *) const;
        virtual void JSGSetViewTargetPosition(const Vec &targetPos);

        TVec3f mUpVector;       // 30
        TVec3f mTargetPos;      // 3C
        f32 mProjectionFovy;    // 48
        f32 mProjectionAspect;  // 4C
    };

    class TPolarCamera : public TCamera {
    public:
        TPolarCamera(const char *name)
            : TCamera(50.0f, 10000.0f, name), mProjectionFovy(45.0f), mProjectionAspect(1.3333334f),
              _38(0.0f, 0.0f, 0.0f), _44(1200.0f) {}
        virtual ~TPolarCamera();

        virtual void perform(u32, TGraphics *);
        virtual void load(JSUMemoryInputStream &stream);

        virtual s32 JSGGetProjectionType() const;
        virtual void JSGSetProjectionType(JStage::TECameraProjection);
        virtual f32 JSGGetProjectionFovy() const;
        virtual void JSGSetProjectionFovy(f32 projectionFovy);
        virtual f32 JSGGetProjectionAspect() const;
        virtual void JSGSetProjectionAspect(f32 projectionAspect);

        f32 mProjectionFovy;    // 30
        f32 mProjectionAspect;  // 34
        TVec3f _38;
        f32 _44;
    };

    class TOrthoProj : public TCamera {
    public:
        TOrthoProj() : TCamera(-100.0f, 100.0f, "<TOrthoProj>") {
            mProjectionField[0] = 0.0f;
            mProjectionField[1] = 16.0f;
            mProjectionField[2] = 600.0f;
            mProjectionField[3] = 464.0f;
        }
        virtual ~TOrthoProj();

        virtual void load(JSUMemoryInputStream &stream);
        virtual void perform(u32, TGraphics *);

        virtual s32 JSGGetProjectionType() const;
        virtual void JSGSetProjectionType(JStage::TECameraProjection);
        virtual f32 *JSGGetProjectionField() const;
        virtual void JSGSetProjectionField(const f32 *projectionField);

        f32 mProjectionField[4];  // 30
    };

}  // namespace JDrama