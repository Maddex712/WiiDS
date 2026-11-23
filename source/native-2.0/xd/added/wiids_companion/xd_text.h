#pragma once
#include <stdint.h>
extern "C" {
void WiidsXD_TextBegin(uint32_t);
void WiidsXD_TextGlyph(uint32_t,uint16_t);
uint64_t WiidsXD_TextScopeBegin(int,int);
void WiidsXD_TextScopeEnd(uint64_t);
int WiidsXD_CopyMessageLabel(uint32_t,uint16_t*,int);
int WiidsXD_CopyRowLabel(int,int,uint16_t*,int);
}
struct WiidsXDTextScope {
    uint64_t previous;
    WiidsXDTextScope(int menu,int item):previous(WiidsXD_TextScopeBegin(menu,item)){}
    ~WiidsXDTextScope(){WiidsXD_TextScopeEnd(previous);}
};
