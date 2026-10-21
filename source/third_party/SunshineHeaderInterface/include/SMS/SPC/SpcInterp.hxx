#pragma once

#include <Dolphin/types.h>
#include <SMS/SPC/SpcBinary.hxx>
#include <SMS/SPC/SpcSlice.hxx>
#include <SMS/SPC/SpcStack.hxx>

class TSpcInterp {
public:
    TSpcBinary *mSpcBinary;
#if __SIZEOF_POINTER__ == 8
#else
 // _0
    u32 _4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _8;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _10;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _14;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_6[32];
    u32 mSlicesMax;
#else
    u32 mSlicesMax;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_7[8];
  // _18
    u32 mSlicesCount;
#else
    u8 _pc32_7[4];
  // _18
    u32 mSlicesCount;
#endif

    TSpcSlice *mSlices;
#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _28;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_11[4];
    u32 _2C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _30;
#endif

#if __SIZEOF_POINTER__ == 8
#else
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_14[24];
    TSpcStack<u32> mStack;
#else
    TSpcStack<u32> mStack;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _48;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _4C;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _50;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _54;
#endif

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_19[16];
    char *mName;
#else
    char *mName;
#endif


    TSpcInterp(TSpcBinary *, void *, int, int, int, int);
    virtual ~TSpcInterp();

    virtual void update();
    virtual void dispatchBuiltin(u32, u32);

    void verifyArgNum(u32 argc, u32 *numArgs);
    void dump();
    void dispatchBuiltinDefault(u32, u32);

    /* SPC EXECUTE FUNCTIONS */
    void execadd();
    void execadr();
    void execand();
    void execass();
    void execband();
    void execbor();
    void execcall();
    void execdec();
    void execdiv();
    void execend();
    void execeq();
    void execflt();
    void execfunc();
    void execge();
    void execgt();
    void execinc();
    void execint();
    void execint0();
    void execint1();
    void execjmp();
    void execjne();
    void execle();
    void execlt();
    void execmkds();
    void execmkfr();
    void execmod();
    void execmul();
    void execne();
    void execneg();
    void execnop();
    void execnot();
    void execor();
    void execpop();
    void execret();
    void execret0();
    void execshl();
    void execshr();
    void execstr();
    void execsub();
    void execvar();

    /* FETCHERS */
    s32 fetchS32();
    u32 fetchU32();

    /* BUILTINS */
    void spcDump(u32);
    void spcExit(u32);
    void spcFloat(u32);
    void spcInt(u32);
    void spcLock(u32);
    void spcPrint(u32);
    void spcTypeof(u32);
    void spcUnlock(u32);
    void spcYield(u32);
};

void SpcTrace(const char *, ...);