#include <cstdio>

extern "C" {
const void* WiidsXD_FindInputVariable(const char*);
const void* PortXD_FindVariable(const char* path) {
    if(!path) return nullptr;
    // Concrete bindings within the original native TU replace desktop PDB lookup.
    if(const void* input=WiidsXD_FindInputVariable(path)) return input;
    std::fprintf(stderr,"[WiiDS-XD] optional desktop variable probe unavailable: %s\n",path);
    return nullptr;
}
}
