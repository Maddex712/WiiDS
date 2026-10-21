// SMS_NO_AUDIO=1: run the game silently without real audio data.
//
// JAudio is driven by mSound.aaf (in the boot archive, /audi/), an init-data
// command stream that points at the sound tables, bank lists, wave banks,
// sequence archive and stream lists. With SMS_NO_AUDIO the MSound constructor
// (decomp patch 0014) gets a synthetic stream in host byte order instead: one sound-effect category with a single silent entry and
// nothing else, which is enough for JAIBasic/JAIData to size their buffers and
// for sound requests to resolve to "nothing to play". The fake DSP never
// mixes, so the game proceeds without sound.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"

int port_no_audio;

namespace {
// Commands understood by JAIBasic::checkInitDataOnMemory.
enum { AAF_END = 0, AAF_SOUND_TABLES = 1 };

u32 g_aaf[64] __attribute__((aligned(32)));
}

extern "C" void port_noaudio_init(void)
{
	const char* e = getenv("SMS_NO_AUDIO");
	port_no_audio = e && *e && strcmp(e, "0") != 0;
	if (!port_no_audio)
		return;
	// Sound table ("BST"-style, read raw by JAIData::setInfoDataPointer):
	//   +0x03 u8 table id; +0x06 + i*4 u16 sound count of category i;
	//   +0x08 + i*4 u16 first info index; +0x50 JAISoundInfo[] (16 bytes).
	const u32 kTableOff = 0x20, kTableSize = 0x50 + 0x10;
	memset(g_aaf, 0, sizeof g_aaf);
	g_aaf[0] = AAF_SOUND_TABLES;
	g_aaf[1] = kTableOff;
	g_aaf[2] = kTableSize;
	g_aaf[3] = 0; // no separate sequence/stream tables
	g_aaf[4] = AAF_END;
	u8* table = (u8*)g_aaf + kTableOff;
	*(u16*)(table + 6) = 1; // category 0 has one (silent) entry
	*(u16*)(table + 8) = 0;
	port_log("[audio] SMS_NO_AUDIO: empty sound configuration; the game runs silently\n");
}

extern "C" u8* port_noaudio_init_data(void) { return port_no_audio ? (u8*)g_aaf : NULL; }
