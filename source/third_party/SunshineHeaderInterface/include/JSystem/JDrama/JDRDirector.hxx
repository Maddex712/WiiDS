#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JStage/JSGSystem.hxx>
#include <JSystem/JUtility/JUTGamePad.hxx>

namespace JDrama {

    class TDirector : public TNameRef, public JStage::TSystem {
    public:
        TDirector() : TNameRef("<TDirector>"), mViewObjRoot(nullptr), mViewObjStageGroup(nullptr) {}
        virtual ~TDirector();

        virtual TDirector *searchF(u16, const char *);
        virtual s32 direct();
        virtual JStage::TObject *JSGFindObject(const char *,
                                               JStage::TEObject) const;  // JStage::TEObject

        TViewObj *mViewObjRoot;
        TViewObj *mViewObjStageGroup;
    };

}  // namespace JDrama