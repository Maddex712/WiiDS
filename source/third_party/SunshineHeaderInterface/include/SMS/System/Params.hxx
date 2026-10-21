#pragma once

#include <Dolphin/types.h>
#include <SMS/macros.h>

#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JKernel/JKRMemArchive.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>
#include <SMS/System/BaseParam.hxx>

class TParams {
public:
    TParams() : mPrmPath(nullptr), mBaseParam(nullptr) {}
    TParams(const char *prm) : mPrmPath(prm), mBaseParam(nullptr) {}
    ~TParams() {}

    static void finalize();

    void init();
    void load(const char *);
    void load(JSUMemoryInputStream &);

    const char *mPrmPath;
    TBaseParam *mBaseParam;

    static JKRMemArchive *mArc;
    static JKRMemArchive *mSceneArc;
};

// sizeof = 0x14
template <typename T> class TParamT : public TBaseParam {
    T mValue;

public:
    TParamT(TParams *parent, T param, u16 keycode, const char *name)
        : TBaseParam(parent, keycode, name), mValue(param) {}

    inline T get() const { return mValue; }
    inline void set(T param) { mValue = param; }

    void load(JSUMemoryInputStream &stream) override {
        u32 fakeit;
        SMS_FROM_GPR(29, fakeit);  // Hack to keep r29 from being used..

        u32 buffer;
        stream.read(&buffer, 4);
        stream.read(&mValue, sizeof(T));
    }
};

template <typename T> class TParamRT : public TParamT<T> {
public:
    TParamRT(TParams *parent, T param, u16 keycode, const char *name)
        : TParamT<T>(parent, param, keycode, name) {}

    inline void set(T param){};
};

#include <JSystem/JGeometry/JGMVec.hxx>
extern template void TParamT<u8>::load(JSUMemoryInputStream &);
extern template void TParamT<s16>::load(JSUMemoryInputStream &);
extern template void TParamT<u16>::load(JSUMemoryInputStream &);
extern template void TParamT<s32>::load(JSUMemoryInputStream &);
extern template void TParamT<f32>::load(JSUMemoryInputStream &);
extern template void TParamT<JGeometry::TVec3<f32>>::load(JSUMemoryInputStream &);
template <> inline void TParamT<bool>::load(JSUMemoryInputStream &stream) {
    u32 buffer;
    stream.read(&buffer, 4);
    u8 value;
    stream.read(&value, 1);
    mValue = value != 0;
}

#define SMS_TPARAM_INIT(name, val)                                                                 \
    name(this, val, JDrama::TNameRef::calcKeyCode(SMS_STRINGIZE(name)), SMS_STRINGIZE(name))