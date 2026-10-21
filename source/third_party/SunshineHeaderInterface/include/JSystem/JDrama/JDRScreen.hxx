#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRDStage.hxx>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JDrama/JDRViewport.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

#include <SMS/Camera/CamConnector.hxx>

namespace JDrama {

    class TScreen : public TViewConnecter {
    public:
        TScreen(const TRect &, const char *);
        ~TScreen() override;

        void assignCamera(TViewObj *);
        void assignViewObj(TViewObj *);

        TViewport *mViewport;          // 10
        TCamConnecter *mCamConnecter;  // 14
    };

}  // namespace JDrama