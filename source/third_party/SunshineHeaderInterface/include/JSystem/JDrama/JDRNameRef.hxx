#pragma once

#include <Dolphin/types.h>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

namespace JDrama {

    class TNameRef {
    public:
        TNameRef(const char *);
        virtual ~TNameRef();

        virtual u32 getType() const;
        virtual void load(JSUMemoryInputStream &stream);
        virtual void save(JSUMemoryOutputStream &stream);
        virtual void loadAfter();
        virtual TNameRef *searchF(u16, const char *);

        static TNameRef *genObject(JSUMemoryInputStream &stream, JSUMemoryInputStream &);
        static u32 getType(JSUMemoryInputStream &stream, JSUMemoryInputStream &);

        TNameRef *search(const char *);

        static u16 calcKeyCode(const char *);

        const char *mKeyName;
        u16 mKeyCode;
#if __SIZEOF_POINTER__ == 8
#else
        u16 _0A;
#endif

    };

}  // namespace JDrama

constexpr u16 cexp_calcKeyCode(const char *str) {
    u32 i = 0;
    for (; *str != '\0'; i = (*str++) + i * 3)
        ;
    return i;
}