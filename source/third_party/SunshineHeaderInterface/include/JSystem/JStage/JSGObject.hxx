#pragma once

#include <Dolphin/types.h>

namespace JStage {

    enum TEObject {};

    class TObject {
    public:
        virtual ~TObject();

        virtual bool JSGGetName() const;
        virtual u32 JSGGetFlag() const;
        virtual void JSGSetFlag(u32);
        virtual bool JSGGetData(u32, void *, u32) const;
        virtual void JSGSetData(u32, const void *, u32);
        virtual void JSGGetParent(TObject **, u32 *) const;
        virtual void JSGSetParent(TObject *, u32);
        virtual void JSGSetRelation(bool, TObject *, u32);
    };

}  // namespace JStage
