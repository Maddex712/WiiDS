#pragma once

#include <SMS/Strategic/LiveActor.hxx>

#include <Dolphin/types.h>

template <typename T> class TSpineBase;

template <typename T> class TSolidStack {
#if __SIZEOF_POINTER__ == 8
    u32 mStackCapacity;
#else
    size_t mStackCapacity;
#endif
    s32 mStackIndex;
    T *mStackItems;

public:
    inline TSolidStack(size_t capacity) : mStackCapacity(capacity) {
        mStackItems = new T[capacity];
    }
    virtual ~TSolidStack();

    inline void push(T item) {
        if (mStackIndex < mStackCapacity) {
            mStackItems[mStackIndex++] = item;
        }
    }

    inline T pop() {
        if (mStackIndex > 0) {
            return mStackItems[--mStackIndex];
        }
        return nullptr;
    }

    void clear() { mStackIndex = 0; }

    size_t capacity() const { return mStackCapacity; };
    s32 depth() const { return mStackIndex; }
};

template <typename T> class TNerveBase {
public:
    virtual ~TNerveBase();
    virtual int execute(TSpineBase<T> *mSpineBase) const;
};

template <typename T> class TSpineBase {
public:
    T *mTarget;                                      // 0x0000
    TSolidStack<const TNerveBase<T> *> mNerveStack;  // 0x0004
    const TNerveBase<T> *mNerveCurrent;              // 0x0014
    const TNerveBase<T> *mNerveCopy;                 // 0x0018
    const TNerveBase<T> *mNervePrevious;             // 0x001C
    s32 mNerveTimer;                                 // 0x0020

    virtual ~TSpineBase();

    virtual void update();

    const TNerveBase<T> *getLatestNerve() { return mNerveCurrent; }
    void pushNerve(const TNerveBase<T> *nerve) { mNerveStack.push(nerve); }
    void setNerve(const TNerveBase<T> *nerve) {
        if (mNerveCurrent)
            mNervePrevious = mNerveCurrent;
        mNerveTimer   = 0;
        mNerveCurrent = nerve;
    }
};