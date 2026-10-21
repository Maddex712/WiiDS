#pragma once

#include <Dolphin/types.h>
#include <JSystem/JStage/JSGObject.hxx>

namespace JStage {

    class TSystem : public TObject {
    public:
        virtual ~TSystem();

        virtual unsigned long JSGFGetType() const;
        virtual bool JSGGetSystemData(unsigned long);
        virtual void JSGSetSystemData(unsigned long, unsigned long);
        virtual TObject *JSGFindObject(const char *, TEObject) const;
        virtual bool JSGCreateObject(const char *, TEObject, u32);
        virtual void JSGDestroyObject(TObject *);
    };

}  // namespace JStage