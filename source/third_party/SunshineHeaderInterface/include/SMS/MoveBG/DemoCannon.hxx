#pragma once

#include <SMS/MapObj/MapObjBase.hxx>

class TDemoCannon : public TMapObjBase {
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[4];
#else
#endif
};