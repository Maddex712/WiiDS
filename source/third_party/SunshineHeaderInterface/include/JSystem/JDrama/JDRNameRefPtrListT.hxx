#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JGadget/List.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>

namespace JDrama {

    template <typename T, typename U>
    class TNameRefPtrListT : public TNameRef, public JGadget::TList_pointer_void {
    public:
        virtual ~TNameRefPtrListT();

        virtual void load(JSUMemoryInputStream &stream);
        virtual void loadAfter();
        T *searchF(u16, const char *);
    };

}  // namespace JDrama