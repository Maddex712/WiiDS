#pragma once

#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRCharacter.hxx>
#include <JSystem/JKernel/JKRArchivePri.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

class TObjChara : public JDrama::TCharacter {
public:
    virtual ~TObjChara();

    virtual void load(JSUMemoryInputStream &);
    virtual JKRArchive *getGlbResource(const char *) const;

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};