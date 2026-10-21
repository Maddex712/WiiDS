#pragma once

class TSelectShineManager {
#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};