#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRCharacter.hxx>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JGeometry/JGMVec.hxx>
#include <JSystem/JKernel/JKRArchivePri.hxx>

namespace JDrama {

    class TSmplChara : public TCharacter {
    public:
        virtual ~TSmplChara();

        virtual void load(JSUMemoryInputStream &stream);
        virtual u32 *getRes(const char *resName);

        void mountArc(const char *resName);

        JKRArchive *mArchive;  // C
    };

}  // namespace JDrama