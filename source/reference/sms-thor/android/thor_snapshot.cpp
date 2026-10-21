// AYN Thor lower screen: a small snapshot of the game's own state, published once a
// frame on the game thread (port_vi_frame_wait) and copied out by the Android UI
// through smsbootstrap's JNI bridge. Nothing here writes game memory; pointers are
// only followed inside MEM1, and the UI never touches game objects.
#include <GC2D/CardLoad.hpp>
#include <GC2D/MessageUtil.hpp>
#include <GC2D/ScrnFader.hpp>
#include <GC2D/ShineTable.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <Map/MapCollisionData.hpp>
#include <Map/MapData.hpp>
#include <Player/Mario.hpp>
#include <Player/Watergun.hpp>
#include <System/Application.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <System/ScenarioArchiveName.hpp>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern u8* port_mem1_base;
extern u32 port_mem1_size;

namespace {
// The game sources build as C++03: compiler atomics, no <atomic>.
enum { kWords = 72, kAreaName = 24, kEpisodeName = 40, kNamesEnd = 64 };
uint32_t g_seq;
int32_t g_snap[kWords];
char g_scene[32];
// The game's own area and episode titles, re-read only when the area or episode changes.
int32_t g_namedKey = -1;
char g_areaName[(kEpisodeName - kAreaName) * 4];
char g_episodeName[(kNamesEnd - kEpisodeName) * 4];

bool inMem1(const void* p, size_t n)
{
	const uintptr_t a = (uintptr_t)p, base = (uintptr_t)port_mem1_base;
	return port_mem1_base && a >= base && a + n <= base + port_mem1_size;
}

// A message from the game's text bank as plain ASCII: the bank's control sequences
// (0x1A, length, payload) are skipped, anything outside printable ASCII is dropped.
void copyMessage(char* out, size_t size, void* bank, u32 index)
{
	memset(out, 0, size);
	if (!inMem1(bank, 0x20)) return;
	const char* text = SMSGetMessageData(bank, index);
	if (!inMem1(text, 1)) return;
	size_t n = 0;
	for (size_t i = 0; text[i] && n + 1 < size && i < 512; i++) {
		const unsigned char ch = (unsigned char)text[i];
		if (ch == 0x1A) {
			const unsigned char len = (unsigned char)text[i + 1];
			if (len < 2) break;
			i += len - 1;
		} else if (ch == 0x0A || ch == 0x0D) {
			if (n && out[n - 1] != ' ') out[n++] = ' ';
		} else if (ch >= 0x20 && ch < 0x7F) {
			out[n++] = (char)ch;
		}
	}
	while (n && out[n - 1] == ' ') out[--n] = 0;
}

// Area and episode titles exactly as the pause menu builds them (TPauseMenu2::load).
void nameArea(u32 shineStage, s32 episode)
{
	const int32_t key = (int32_t)(shineStage << 8 | (episode & 0xFF));
	if (key == g_namedKey && g_areaName[0]) return;
	copyMessage(g_areaName, sizeof g_areaName, JKRFileLoader::getGlbResource("/common/2d/stagename.bmg"), shineStage);
	memset(g_episodeName, 0, sizeof g_episodeName);
	const s16 shine = shineStage >= 2 && shineStage <= 8 && episode >= 0 && episode < 8
	                      ? SMS_getShineID(shineStage, episode, false) : -1;
	if (shine >= 0)
		copyMessage(g_episodeName, sizeof g_episodeName,
		            JKRFileLoader::getGlbResource("/common/2d/scenarioname.bmg"), (u16)SMS_getNormalStage(shine));
	g_namedKey = key;
}
// The HUD's swoosh colour (the coin counter's backing, J2DPicture white and black), seen by
// the HUD draw itself (zzz-thor-01-hud.patch).
uint32_t g_swooshWhite, g_swooshBlack;

// The area's walkable floor for the lower screen's map: the game's own static collision
// (TMapCollisionData's check-data pool, map triangles only, not actors'), floors and water
// surfaces, without death planes, warp triggers and other pass-through planes. Exported once
// per scene on the game thread; the UI copies it when [g_floorSeq] changes.
enum { kFloorMax = 40000, kFloorFloats = 11 }; // x1 z1 x2 z2 x3 z3, mean y, normal xyz, water
float g_floor[kFloorMax * kFloorFloats];
int g_floorCount;
uint32_t g_floorSeq;
char g_floorScene[32];
int g_floorWait;

void exportFloor(const char* scene)
{
	if (!scene[0] || !strncmp(scene, g_floorScene, sizeof g_floorScene)) { g_floorWait = 0; return; }
	// A few frames into the stage, when its collision is certainly built.
	if (++g_floorWait < 20) return;
	TMapCollisionData* data = gpMapCollisionData;
	if (!inMem1(data, sizeof *data)) return;
	const u32 used = data->unk34;
	if (!used || used > data->unk1C || !inMem1(data->unk28, used * sizeof(TBGCheckData))) return;
	int n = 0;
	for (u32 i = 0; i < used && n < kFloorMax; i++) {
		const TBGCheckData& t = data->unk28[i];
		if (t.mActor) continue;
		const bool water = t.isWaterSurface();
		if (!water && (t.isMarioThrough() || t.isIllegalData())) continue;
		if (fabsf(t.mNormal.y) < 0.35f) continue;
		float* o = &g_floor[n++ * kFloorFloats];
		o[0] = t.mPoint1.x; o[1] = t.mPoint1.z; o[2] = t.mPoint2.x; o[3] = t.mPoint2.z;
		o[4] = t.mPoint3.x; o[5] = t.mPoint3.z;
		o[6] = (t.mPoint1.y + t.mPoint2.y + t.mPoint3.y) / 3;
		o[7] = t.mNormal.x; o[8] = t.mNormal.y; o[9] = t.mNormal.z;
		o[10] = water ? 1.0f : 0.0f;
	}
	memcpy(g_floorScene, scene, sizeof g_floorScene);
	__atomic_store_n(&g_floorCount, n, __ATOMIC_RELEASE);
	__atomic_add_fetch(&g_floorSeq, 1, __ATOMIC_RELEASE);
}

// The area's photo for the lower screen (an arrival postcard where the disc has none): one
// frame of the 3D picture, taken as the HUD starts drawing so it holds no HUD, once Mario has
// stood in the area with the screen clear for two seconds.
char g_photoScene[32];
int g_photoClear;
bool g_photoWanted;
} // namespace

// The DVD layer reports every path it resolves; the last scene archive names the area.
extern "C" void port_thor_note_path(const char* path)
{
	// "/data/scene/dolpic0.szs", or "scene/..." relative to the current directory.
	const char* scene = path ? strstr(path, "scene/") : nullptr;
	if (!scene) return;
	scene += 6;
	size_t n = strcspn(scene, ".");
	if (n >= sizeof g_scene) n = sizeof g_scene - 1;
	char name[sizeof g_scene];
	memset(name, 0, sizeof name);
	memcpy(name, scene, n);
	memcpy(g_scene, name, sizeof g_scene);
}

// Layout (int32): 0 version 1, 1 frame sequence, 2 1 when a stage with Mario runs,
// 3 shines, 4 blue coins, 5 coins, 6 lives, 7 FLUDD water in permille (-1 without FLUDD),
// 8 nozzle (TWaterGun::TNozzleType, -1 without FLUDD), 9 stage, 10 scenario,
// 11 shine area (SMS_getShineStage: 0 airstrip, 1 plaza, 2-8 the seven areas, 9 Corona),
// 12..19 the scene archive name (32 bytes), 20 episode (0-7, the pause menu's flag),
// 21 Shines held in this area (bits 0-7 episodes, 8 the 100-coin Shine, 9-10 the area's two
// other Shines; -1 outside the seven areas), 22 TApplication::mAppState (2 boot, 3 Nintendo logo,
// 5 gameplay, 6 movie, 8 title, 9 menu) plus, on the title/file-select map, TCardLoad's screen
// + 1 in bits 8-15 (10, 9 and 3 the "Press Start" title, 8 leaving it, 0 the file select), 23 how much of the picture the game's fader lets
// through (0 black .. 255 clear), 24..39 the area's name and
// 40..63 the episode's name from the game's own text (ASCII, NUL-padded), 64..66 Mario's
// position (x, y, z as float bits; 0 outside a stage), 67/68 the HUD swoosh's white/black
// (RGBA), 69 the floor export's sequence, 70 the area photo's sequence, 71 1 while the top
// screen leaves the counters to the lower one.
extern "C" int port_thor_hud_split(void);
extern "C" uint32_t port_thor_photo_seq(void);
extern "C" void port_thor_publish(void)
{
	int32_t s[kWords];
	for (int i = 0; i < kWords; i++) s[i] = -1;
	s[0] = 2;
	s[2] = 0;
	TFlagManager* flags = TFlagManager::smInstance;
	if (inMem1(flags, sizeof *flags)) {
		s[3] = flags->getFlag(0x40000);
		s[4] = flags->getFlag(0x40001);
		s[5] = flags->getFlag(0x40002);
		s[6] = flags->getFlag(0x20001);
	}
	TMario* mario = gpMarioOriginal;
	bool named = false;
	if (gpMarDirector && inMem1(mario, sizeof *mario)) {
		s[2] = 1;
		const u8 map = gpMarDirector->mMap;
		if (map <= 0x3C && inMem1(flags, sizeof *flags)) {
			const u32 shineStage = SMS_getShineStage(map);
			const s32 episode = flags->getFlag(0x40003);
			s[11] = (int32_t)shineStage;
			s[20] = episode;
			if (shineStage >= 2 && shineStage <= 8) {
				int32_t held = 0;
				for (int i = 0; i < 8; i++)
					if (flags->getShineFlag((u8)SMS_getShineID(shineStage, i, false))) held |= 1 << i;
				for (int i = 0; i < 3; i++)
					if (flags->getShineFlag((u8)SMS_getShineID(shineStage, i, true))) held |= 1 << (8 + i);
				s[21] = held;
			}
			nameArea(shineStage, episode);
			named = true;
		}
		memcpy(&s[64], &mario->mPosition.x, 4);
		memcpy(&s[65], &mario->mPosition.y, 4);
		memcpy(&s[66], &mario->mPosition.z, 4);
		TWaterGun* fludd = mario->mWaterGun;
		if (mario->checkFlag(MARIO_FLAG_HAS_FLUDD) && inMem1(fludd, sizeof *fludd)) {
			const s32 max = fludd->getMaxWater();
			s[7] = max > 0 ? (s32)((int64_t)fludd->getCurrentWater() * 1000 / max) : -1;
			s[8] = fludd->getCurrentNozzleIndex();
		}
	}
	TApplication* app = SMSGetApplication();
	s[22] = app->mAppState;
	if (inMem1(gpMarDirector, sizeof *gpMarDirector) && gpMarDirector->mMap == 15
	    && inMem1(gpCardLoad, sizeof *gpCardLoad) && gpCardLoad->unk14 >= 0 && gpCardLoad->unk14 < 255)
		s[22] |= (gpCardLoad->unk14 + 1) << 8;
	s[23] = 255;
	TSMSFader* fader = app->mFader;
	if (inMem1(fader, sizeof *fader)) {
		if (fader->mFadeStatus == TSMSFader::FADE_STATUS_FULLY_FADED_OUT) s[23] = 0;
		else if (fader->mFadeStatus != TSMSFader::FADE_STATUS_FULLY_FADED_IN) s[23] = 255 - fader->mFadeColor.a;
	}
	s[9] = app->mCurrArea.getStage();
	s[10] = app->mCurrArea.getScenario();
	// The scene archive the game itself loads for this stage and scenario (the lookup in
	// TApplication's scene loader), when its table is in place.
	char scene[sizeof g_scene];
	memcpy(scene, g_scene, sizeof scene);
	TNameRefPtrAryT<TNameRefAryT<TScenarioArchiveName> >* table = app->unk30;
	if (inMem1(table, sizeof *table) && app->mCurrArea.getStage() < table->getChildren().size()) {
		TNameRefAryT<TScenarioArchiveName>* stage = table->getChildren()[app->mCurrArea.getStage()];
		if (inMem1(stage, sizeof *stage) && app->mCurrArea.getScenario() < stage->size()) {
			const char* arc = stage->getChildren()[app->mCurrArea.getScenario()].mArcName;
			if (inMem1(arc, 1)) {
				size_t n = strcspn(arc, ".");
				if (n >= sizeof scene) n = sizeof scene - 1;
				memset(scene, 0, sizeof scene);
				memcpy(scene, arc, n);
			}
		}
	}
	memcpy(&s[12], scene, sizeof scene);
	if (s[2] == 1) {
		exportFloor(scene);
		// The photo waits for two seconds of a clear screen in a new area.
		if (strncmp(scene, g_photoScene, sizeof g_photoScene)) {
			g_photoClear = s[23] == 255 ? g_photoClear + 1 : 0;
			if (g_photoClear >= 120) {
				memcpy(g_photoScene, scene, sizeof g_photoScene);
				__atomic_store_n(&g_photoWanted, true, __ATOMIC_RELEASE);
			}
		} else {
			g_photoClear = 0;
		}
	} else {
		s[64] = s[65] = s[66] = 0;
		g_floorWait = 0;
		g_photoClear = 0;
	}
	s[67] = (int32_t)g_swooshWhite;
	s[68] = (int32_t)g_swooshBlack;
	s[69] = (int32_t)__atomic_load_n(&g_floorSeq, __ATOMIC_ACQUIRE);
	s[70] = (int32_t)port_thor_photo_seq();
	s[71] = port_thor_hud_split();
	if (named) {
		memcpy(&s[kAreaName], g_areaName, sizeof g_areaName);
		memcpy(&s[kEpisodeName], g_episodeName, sizeof g_episodeName);
	} else {
		memset(&s[kAreaName], 0, (kNamesEnd - kAreaName) * 4);
	}
	// Seqlock: odd while writing.
	const uint32_t seq = __atomic_load_n(&g_seq, __ATOMIC_RELAXED);
	__atomic_store_n(&g_seq, seq + 1, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	s[1] = (int32_t)((seq + 2) / 2);
	memcpy(g_snap, s, sizeof s);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&g_seq, seq + 2, __ATOMIC_RELEASE);
}

// The title's sky for the lower screen: TCardLoad calls this as its 2D layer starts, so the
// frame holds the 3D sky, sun and clouds without the logo. Captured a few times a second on
// the "Press Start" screens (TCardLoad 10, 9 and 3) into one of two buffers; the UI
// copies the newest buffer the game is not writing.
extern "C" int GXPC_CaptureEFB(u8* rgba, int w, int h);
namespace {
enum { kSkyW = 640, kSkyH = 360, kSkyBytes = kSkyW * kSkyH * 4 };
u8 g_sky[2][kSkyBytes];
int g_skyLatest = -1, g_skyReading = -1;
uint32_t g_skySeq;
} // namespace

extern "C" void port_thor_capture_sky(int screen)
{
	static unsigned frame;
	// Not 8: leaving the title, the camera turns to the beach; the last sky frame is kept.
	if (screen != 3 && screen != 9 && screen != 10) return;
	if (++frame % 4) return;
	const int latest = __atomic_load_n(&g_skyLatest, __ATOMIC_ACQUIRE);
	const int slot = latest == 0 ? 1 : 0;
	if (__atomic_load_n(&g_skyReading, __ATOMIC_ACQUIRE) == slot) return;
	if (!GXPC_CaptureEFB(g_sky[slot], kSkyW, kSkyH)) return;
	// The EFB's alpha is the game's destination alpha, not coverage: the picture is opaque.
	for (int i = 3; i < kSkyBytes; i += 4) g_sky[slot][i] = 0xFF;
	static bool dumped;
	if (!dumped && getenv("SMS_THOR_SKY_DUMP")) {
		dumped = true;
		if (FILE* f = fopen("thor-sky.rgba", "wb")) { fwrite(g_sky[slot], 1, kSkyBytes, f); fclose(f); }
	}
	__atomic_store_n(&g_skyLatest, slot, __ATOMIC_RELEASE);
	__atomic_add_fetch(&g_skySeq, 1, __ATOMIC_RELEASE);
}

// Copies the newest sky (640x360 RGBA, rows top first as the EFB is kept; the picture is the top 448/528) when it is newer than `have`;
// returns its sequence number, or 0 when there is nothing new.
extern "C" __attribute__((visibility("default"))) uint32_t port_thor_sky(u8* out, int size, uint32_t have)
{
	if (!out || size < kSkyBytes) return 0;
	const uint32_t seq = __atomic_load_n(&g_skySeq, __ATOMIC_ACQUIRE);
	if (seq == 0 || seq == have) return 0;
	for (int tries = 0; tries < 4; tries++) {
		const int slot = __atomic_load_n(&g_skyLatest, __ATOMIC_ACQUIRE);
		if (slot < 0) return 0;
		__atomic_store_n(&g_skyReading, slot, __ATOMIC_RELEASE);
		if (__atomic_load_n(&g_skyLatest, __ATOMIC_ACQUIRE) != slot) continue;
		memcpy(out, g_sky[slot], kSkyBytes);
		__atomic_store_n(&g_skyReading, -1, __ATOMIC_RELEASE);
		return seq;
	}
	__atomic_store_n(&g_skyReading, -1, __ATOMIC_RELEASE);
	return 0;
}

// --- Thor HUD split (zzz-thor-01-hud.patch) --------------------------------------------
// SMS_THOR_SPLIT_HUD=1 (set by the Android launcher when the Thor's lower screen is there):
// the top screen leaves the Shine, blue-coin and coin counters to it, and lives move to the
// wide frame's top-left corner.
extern "C" float port_widescreen;
extern "C" int port_thor_hud_split(void)
{
	static int split = -1;
	if (split < 0) {
		const char* e = getenv("SMS_THOR_SPLIT_HUD");
		split = e && !strcmp(e, "1") ? 1 : 0;
	}
	return split;
}

// How far (game x) to move the lives pane, whose left edge is at [left], to the corner.
extern "C" int port_thor_lives_shift(int left)
{
	// SMS_WIDESCREEN_HUD=edges already places the HUD's pieces itself.
	const char* edges = getenv("SMS_WIDESCREEN_HUD");
	if (edges && !strcmp(edges, "edges")) return 0;
	const float extra = 320.0f * (port_widescreen - 1.0f);  // each side beyond 4:3, in game x
	const int dx = (int)(-extra - (float)left + 6.0f);
	return dx < 0 ? dx : 0;
}

extern "C" void port_thor_hud_swoosh(u32 white, u32 black)
{
	g_swooshWhite = white;
	g_swooshBlack = black;
}

namespace {
u8 g_photo[kSkyBytes];
uint32_t g_photoSeq;
int g_photoReading;
} // namespace

extern "C" uint32_t port_thor_photo_seq(void) { return __atomic_load_n(&g_photoSeq, __ATOMIC_ACQUIRE); }

// Called as the HUD starts drawing: the frame so far is the 3D picture alone.
extern "C" void port_thor_capture_photo(void)
{
	if (!__atomic_load_n(&g_photoWanted, __ATOMIC_ACQUIRE)) return;
	if (__atomic_load_n(&g_photoReading, __ATOMIC_ACQUIRE)) return;
	__atomic_store_n(&g_photoWanted, false, __ATOMIC_RELEASE);
	if (!GXPC_CaptureEFB(g_photo, kSkyW, kSkyH)) return;
	for (int i = 3; i < kSkyBytes; i += 4) g_photo[i] = 0xFF;
	__atomic_add_fetch(&g_photoSeq, 1, __ATOMIC_RELEASE);
}

// The newest area photo (same format as the sky) when newer than `have`; its sequence or 0.
extern "C" __attribute__((visibility("default"))) uint32_t port_thor_photo(u8* out, int size, uint32_t have)
{
	if (!out || size < kSkyBytes) return 0;
	const uint32_t seq = __atomic_load_n(&g_photoSeq, __ATOMIC_ACQUIRE);
	if (seq == 0 || seq == have) return 0;
	__atomic_store_n(&g_photoReading, 1, __ATOMIC_RELEASE);
	memcpy(out, g_photo, kSkyBytes);
	__atomic_store_n(&g_photoReading, 0, __ATOMIC_RELEASE);
	return __atomic_load_n(&g_photoSeq, __ATOMIC_ACQUIRE) == seq ? seq : 0;
}

// The area's floor (kFloorFloats per triangle) when newer than `have`: copies at most
// `capacity` floats and returns the triangle count, with the sequence in *seqOut; 0 when nothing new.
extern "C" __attribute__((visibility("default"))) int port_thor_floor(float* out, int capacity, uint32_t have, uint32_t* seqOut)
{
	if (!out || !seqOut) return 0;
	const uint32_t seq = __atomic_load_n(&g_floorSeq, __ATOMIC_ACQUIRE);
	if (seq == 0 || seq == have) return 0;
	int n = __atomic_load_n(&g_floorCount, __ATOMIC_ACQUIRE);
	if (n * kFloorFloats > capacity) n = capacity / kFloorFloats;
	memcpy(out, g_floor, (size_t)n * kFloorFloats * sizeof(float));
	if (__atomic_load_n(&g_floorSeq, __ATOMIC_ACQUIRE) != seq) return 0;
	*seqOut = seq;
	return n;
}

extern "C" __attribute__((visibility("default"))) int port_thor_snapshot(int32_t* out, int count)
{
	if (!out || count < kWords) return 0;
	for (int tries = 0; tries < 8; tries++) {
		const uint32_t before = __atomic_load_n(&g_seq, __ATOMIC_ACQUIRE);
		if (before & 1) continue;
		int32_t copy[kWords];
		memcpy(copy, g_snap, sizeof copy);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		if (__atomic_load_n(&g_seq, __ATOMIC_ACQUIRE) == before) {
			memcpy(out, copy, sizeof copy);
			return kWords;
		}
	}
	return 0;
}
