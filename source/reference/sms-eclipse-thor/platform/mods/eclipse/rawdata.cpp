// Game data code mods reach by retail address, *(T *)0x8040E0BC and the like
// (fixup_sources.py routes those casts through sms_mod_rawdata): the port's
// object at each address. Retail addresses from the disc's map.
#include <Enemy/Graph.hpp>
#include <Enemy/RocketNerve.hpp>
#include <MSound/MSModBgm.hpp>
#include <MarioUtil/ScreenUtil.hpp>
#include <NPC/NpcNerve.hpp>
#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern TRailNode sms_port_grDummyRail[3];
extern const char* sms_port_streamMovies[20];
extern "C" GXColor sms_port_emarioWaterColor;
extern "C" f32 sms_port_sunGlassShineMax;
extern "C" f32 sms_port_yoshiTongueRadius;
extern "C" f32 sms_port_yoshiTongueTargetDist;
extern "C" const u8* const* const sms_port_selectShineConvTable;
extern "C" const u8* const* const sms_port_selectEtcShineConvTable;
extern "C" const u32* const sms_port_selectScenarioNameTable;
extern "C" const u32* const sms_port_selectNormalStageTable;
extern "C" const u8* const sms_port_shineStageTable;
extern "C" const u32 sms_port_shineStageTableSize;
extern "C" const u8* const sms_port_exShineTable;

// BetterSunshineEngine's initAreaInfo reads SelectMenu.o's scEtcShineConvTable
// with three ex scenarios per stage, but Delfino Plaza's scShineTableDolpicEtc
// has one; on the console the next two are its .sdata2 padding, zeros
// (0x80412315). The port's arrays are packed otherwise, so the table it gets
// points at the entry followed by those zeros.
static const u8** portEtcShineConvTable()
{
	static const u8* table[10];
	static u8 dolpicEtc[3];
	static bool made;
	if (!made) {
		made = true;
		memcpy(table, sms_port_selectEtcShineConvTable, sizeof(table));
		dolpicEtc[0] = table[1][0];
		table[1]     = dolpicEtc;
	}
	return table;
}

// It also reads 64 entries of StageUtil.o's shineStageTable, which has 61; on
// the console the other three are .data padding before exShineTable, zeros.
static u8* portShineStageTable()
{
	static u8 table[64];
	static bool made;
	if (!made) {
		made = true;
		memcpy(table, sms_port_shineStageTable, sms_port_shineStageTableSize);
	}
	return table;
}

extern "C" void* sms_mod_rawdata(unsigned int addr)
{
	switch (addr) {
	case 0x803ACA68: return MSBgmXFade::scTiming;                                // scTiming__10MSBgmXFade
	case 0x803ACAB0: return MSBgmXFade::scExp;                                   // scExp__10MSBgmXFade
	case 0x803AFB48: return sms_port_grDummyRail;                                // grDummyRail (graph.cpp)
	case 0x803C0CC8: return (void*)sms_port_selectShineConvTable;                // scShineConvTable (SelectMenu.o)
	case 0x803C0CF0: return portEtcShineConvTable();                             // scEtcShineConvTable (SelectMenu.o)
	case 0x803C0D18: return (void*)sms_port_selectScenarioNameTable;             // scScenarioNameTable (SelectMenu.o)
	case 0x803C0E30: return (void*)sms_port_selectNormalStageTable;              // scNormalStageTable (SelectMenu.o)
	case 0x803DF498: return portShineStageTable();                               // shineStageTable (StageUtil.o)
	case 0x803DF4D8: return (void*)sms_port_exShineTable;                        // exShineTable (StageUtil.o)
	case 0x803DFA00: return sms_port_streamMovies;                               // movies$2059 (TMovieDirector::getStreamMovieName)
	case 0x8040DAB4: return (void*)&TNerveRocketPossessedNozzle::theNerve();     // instance$2890
	case 0x8040DABC: return (void*)&TNerveRocketFly::theNerve();                 // instance$2904
	case 0x8040DFD4: return (void*)&TNerveNPCGraphWander::theNerve();            // instance$2212
	case 0x8040DFE4: return (void*)&TNerveNPCGraphWait::theNerve();              // instance$2251
	case 0x8040DFF4: return (void*)&TNerveNPCWaitMarioApproach::theNerve();      // instance$2275
	case 0x8040E03C: return (void*)&TNerveNPCMad::theNerve();                    // instance$2414
	case 0x8040E0BC: return &gpScreenTexture;                                    // gpScreenTexture
	case 0x8040FA90: return &sms_port_emarioWaterColor;                          // @3761 (TEnemyMario::drawHPMeter)
	case 0x80412548: return &sms_port_sunGlassShineMax;                          // @1863 (TSunGlass's glare, 120 shines)
	case 0x80415F4C: return &sms_port_yoshiTongueRadius;                         // @2833 (TYoshiTongue::movement)
	case 0x80415F68: return &sms_port_yoshiTongueTargetDist;                     // @2942 (TYoshiTongue::findTarget)
	}
	fprintf(stderr, "[mod] retail data address %08x has no port object (platform/mods/eclipse/rawdata.cpp)\n", addr);
	abort();
}

// The OS globals at the bottom of MEM1, which modhooks.cpp fills in (the disc
// ID, the clocks): at the port's MEM1, not at 0x80000000, which Android can
// leave to the system.
extern "C" u8* port_mem1_base;

extern "C" void* sms_mod_lowmem(unsigned int addr)
{
	return port_mem1_base + (addr - 0x80000000u);
}
