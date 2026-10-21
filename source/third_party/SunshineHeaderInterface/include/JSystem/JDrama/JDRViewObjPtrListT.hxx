#pragma once

#include <JSystem/J3D/J3DAnimation.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/List.hxx>

namespace JDrama {
    template <class _T, class _U = TViewObj> class TViewObjPtrListT : public TViewObj {
    public:
        TViewObjPtrListT() : TViewObj("<TViewObjPtrListT>"){};
        TViewObjPtrListT(const char *name) : TViewObj(name){};
        virtual ~TViewObjPtrListT();

        void load(JSUMemoryInputStream &stream) override;
        void loadAfter() override;
        TNameRef *searchF(u16, const char *) override;
        void perform(u32, TGraphics *) override;

        virtual void loadSuper(JSUMemoryInputStream &stream);
        virtual void loadAfterSuper();

        JGadget::TList_pointer_void
            mViewObjList;  // TODO: revert to TList_pointer_void when insert register bug is fixed
    };
}  // namespace JDrama