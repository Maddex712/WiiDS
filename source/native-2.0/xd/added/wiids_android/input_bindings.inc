// Included after the native InputDevice array: no copied ABI layout or guessed offset.
static bool xdInputPathEqual(const char* a,const char* b) {
    if(!a||!b)return false;
    while(*a&&*a==*b){++a;++b;}
    return *a==*b;
}
extern "C" const void* WiidsXD_FindInputVariable(const char* path) {
    if(xdInputPathEqual(path,"_inputData[0]")) return &_inputData[0];
    if(xdInputPathEqual(path,"_inputData[1]")) return &_inputData[1];
    if(xdInputPathEqual(path,"_inputData[0].raw")) return &_inputData[0].raw;
    return nullptr;
}
