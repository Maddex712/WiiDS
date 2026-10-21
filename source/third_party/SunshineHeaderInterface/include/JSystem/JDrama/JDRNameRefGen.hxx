#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRNameRefPtrListT.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

namespace JDrama {

    class TNameRefGen {
    public:
        virtual void load(JSUMemoryInputStream &stream);
        virtual TNameRef *getNameRef(const char *) const;

        TNameRef *getRootNameRef();
        static TNameRefGen *getInstance();

        static TNameRefGen *instance;
    private:
        TNameRef *mRootName;
    };

}  // namespace JDrama