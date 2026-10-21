#pragma once

#include <Dolphin/types.h>
#include <JSystem/JStage/JSGObject.hxx>

namespace JStage {

    class TAmbientLight : public TObject {
    public:
        virtual ~TAmbientLight();

        virtual u32 JSGFGetType() const;
    };

}  // namespace JStage