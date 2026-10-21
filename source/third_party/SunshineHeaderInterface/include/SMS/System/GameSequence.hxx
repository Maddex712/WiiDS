#pragma once

#include <Dolphin/types.h>
#include <JSystem/JDrama/JDRFlagT.hxx>

class TGameSequence {

public:
    enum Area {
        AREA_AIRPORT      = 0x0,
        AREA_DOLPIC       = 0x1,
        AREA_BIANCO       = 0x2,
        AREA_RICCO        = 0x3,
        AREA_MAMMA        = 0x4,
        AREA_PINNABEACH   = 0x5,
        AREA_SIRENA       = 0x6,
        AREA_DELFINO      = 0x7,
        AREA_MONTE        = 0x8,
        AREA_MARE         = 0x9,
        AREA_NONE         = 0xA,
        AREA_SCALE        = 0xB,
        AREA_TEST10       = 0xC,
        AREA_PINNAPARCO   = 0xD,
        AREA_CASINO       = 0xE,
        AREA_OPTION       = 0xF,
        AREA_MAREUNDERSEA = 0x10,
        AREA_DOLPICEX0    = 0x14,
        AREA_DOLPICEX1    = 0x15,
        AREA_DOLPICEX2    = 0x16,
        AREA_DOLPICEX3    = 0x17,
        AREA_DOLPICEX4    = 0x18,
        AREA_BIANCOEX1    = 0x1D,
        AREA_RICOEX0      = 0x1E,
        AREA_RICOEX1      = 0x1F,
        AREA_MAMMAEX0     = 0x20,
        AREA_MAMMAEX1     = 0x21,
        AREA_SIRENAEX0    = 0x28,
        AREA_SIRENAEX1    = 0x29,
        AREA_MONTEEX0     = 0x2A,
        AREA_MAREEX0      = 0x2C,
        AREA_COROEX0      = 0x2E,
        AREA_COROEX1      = 0x2F,
        AREA_COROEX2      = 0x30,
        AREA_COROEX4      = 0x32,
        AREA_COROEX5      = 0x33,
        AREA_COROEX6      = 0x34,
        AREA_BIANCOBOSS   = 0x37,
        AREA_DELFINOBOSS  = 0x38,
        AREA_MAREBOSS     = 0x39,
        AREA_PINNABOSS    = 0x3A,
        AREA_RICCOBOSS    = 0x3B,
        AREA_CORONABOSS   = 0x3C
    };

    void set(u8 area, u8 episode, JDrama::TFlagT<u16> flag);

    u8 mAreaID;
    u8 mEpisodeID;
    JDrama::TFlagT<u16> mFlag;
};