#pragma once

#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>

class JUTConsole : public JKRDisposer {
public:
    enum EConsoleType {};

    JUTConsole(unsigned int, unsigned int, bool);
    ~JUTConsole();

    void clear();
    JUTConsole *create(unsigned int, void *, u32);
    void doDraw(JUTConsole::EConsoleType) const;
    u32 getLineFromObjectSize(u32, unsigned int);
    u32 getLineOffset() const;
    u32 getUsedLine() const;
    void print(const char *);
    void print_f(const char *, ...);
    void scroll(int);
#if __SIZEOF_POINTER__ == 8
#else
    u8 _00[0x4E];
#endif


#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[96];
#else
    u8 _pc32_tail[2];
#endif
};

void JUTConsole_print_f_va_();