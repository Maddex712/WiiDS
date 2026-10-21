#pragma once

#include <Dolphin/types.h>

// I've only seen this used as u32 and TSpcSlice
template <typename T> class TSpcStack {
public:
#if __SIZEOF_POINTER__ == 8
    u32 mMaxSize;
#else
    size_t mMaxSize;
#endif
#if __SIZEOF_POINTER__ == 8
    u32 mCurSize;
#else
    size_t mCurSize;
#endif
    T *mStack;

    TSpcStack(size_t size) : mMaxSize(size), mCurSize(0) { mStack = new T[size]; }

    virtual ~TSpcStack();

    void push(const T &slice);
    T pop() {
        mCurSize -= 1;
        return mStack[mCurSize + 1];
    };
};