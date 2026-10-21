#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRFlagT.hxx>
#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRNameRef.hxx>

namespace JDrama {

    class TViewObj : public TNameRef {
    public:
        TViewObj(const char *);
        virtual ~TViewObj();

        virtual void perform(u32, TGraphics *) = 0;

        void testPerform(u32, TGraphics *);

        u16 mPerformFlags;  // 0x000C
    
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[2];
#endif
};

}  // namespace JDrama