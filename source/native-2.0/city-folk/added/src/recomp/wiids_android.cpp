#include "recomp/wiids_android.h"
#include "recomp/wiimote.h"
#include "game/cf_features.h"
#include "recomp/frame.h"
#include "recomp/game.h"
#include "recomp/hle.h"
#include "recomp/menu_nav.h"
#include "recomp/runtime.h"
#include "recomp/version.h"
#include "port/settings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <aurora/aurora.h>
#include <aurora/pipeline_cache.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

extern "C" int aurora_main(int, char**);
namespace aurora::window { void set_surface_ready(bool ready) noexcept; }

namespace {
using namespace recomp::wiimote;

std::atomic<bool> quit{false};
std::atomic<bool> pauseGame{false};
std::atomic<int> aspectChoice{-1}, volumeChoice{-1};
std::atomic<int> pendingAspect{-1}, pendingVolume{-1};
std::mutex inputMutex;
u32 keyButtons = 0;
u32 touchButtons = 0;
float moveX = 0;
float moveY = 0;
float aimX = 0;
float aimY = 0;
bool aimValid = false;
}

namespace wiids {
bool quitRequested() { return quit.load(); }
bool paused() { return pauseGame.load(); }

void syncSettings() {
    auto& config = port::settings();
    const int aspect = pendingAspect.exchange(-1);
    const int volume = pendingVolume.exchange(-1);
    if (aspect >= 0) config.video.aspect = static_cast<port::AspectMode>(aspect);
    if (volume >= 0) config.audio.masterVolume = static_cast<float>(volume) / 100.0f;
    aspectChoice.store(static_cast<int>(config.video.aspect));
    volumeChoice.store(static_cast<int>(std::lround(config.audio.masterVolume * 100)));
    if (aspect >= 0 || volume >= 0) port::saveSettings();
}

void applyInput(State& state) {
    std::lock_guard lock(inputMutex);
    state.connected = true;
    state.ext = kDevNunchuk;
    state.buttons |= keyButtons | touchButtons;
    if (moveX != 0.0f || moveY != 0.0f) {
        state.stickX = moveX;
        state.stickY = moveY;
    }
    if (aimValid) {
        state.pointerValid = true;
        state.px = aimX;
        state.py = aimY;
    }
}
}

extern "C" SDL_DECLSPEC int SDLCALL WiiDsCityFolkMain(int argc, char** argv) {
    if (argc != 5 || SDL_setenv_unsafe("CF_WIIDS_ROOT", argv[2], 1) != 0 ||
        SDL_setenv_unsafe("CF_NAND_DIR", argv[3], 1) != 0 ||
        SDL_setenv_unsafe("CF_CACHE_DIR", argv[4], 1) != 0 ||
        SDL_setenv_unsafe("CF_UPDATE_CHECK", "0", 1) != 0)
        return 1;
    // The shipped pipeline precache list (data/pipelines.bin; precache.cpp looks for it beside the
    // executable, which an APK does not have): written to the cache directory once and named by
    // CF_PIPELINE_LIST, so the known pipelines are compiled at start-up instead of at their first draw.
    {
        static const unsigned char kPipelines[] = {
#include "recomp/wiids_pipelines.inc"
        };
        const std::string path = std::string(argv[4]) + "/pipelines.bin";
        bool have = false;
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            have = std::fseek(f, 0, SEEK_END) == 0 && std::ftell(f) == static_cast<long>(sizeof kPipelines);
            std::fclose(f);
        }
        if (!have)
            if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
                have = std::fwrite(kPipelines, 1, sizeof kPipelines, f) == sizeof kPipelines;
                have = std::fclose(f) == 0 && have;
            }
        if (have) SDL_setenv_unsafe("CF_PIPELINE_LIST", path.c_str(), 1);
    }
    char executable[] = "WiiDS City Folk";
    char discFlag[] = "--disc";
    char* gameArgs[] = {executable, discFlag, argv[1], nullptr};
    return aurora_main(3, gameArgs);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setButtons(JNIEnv*, jclass, jint mask, jboolean held, jboolean touch) {
    std::lock_guard lock(inputMutex);
    auto& bits = touch ? touchButtons : keyButtons;
    const auto buttons = static_cast<u32>(mask) & kBtnGameMask;
    if (held) bits |= buttons;
    else bits &= ~buttons;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setAxes(JNIEnv*, jclass, jfloat x, jfloat y) {
    std::lock_guard lock(inputMutex);
    moveX = std::clamp(x, -1.0f, 1.0f);
    moveY = std::clamp(y, -1.0f, 1.0f);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setPointer(JNIEnv*, jclass, jfloat x, jfloat y, jboolean valid) {
    std::lock_guard lock(inputMutex);
    aimX = std::clamp(x, -1.0f, 1.0f);
    aimY = std::clamp(y, -1.0f, 1.0f);
    aimValid = valid;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_clearInput(JNIEnv*, jclass) {
    std::lock_guard lock(inputMutex);
    keyButtons = 0;
    touchButtons = 0;
    moveX = 0;
    moveY = 0;
    aimValid = false;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setPaused(JNIEnv*, jclass, jboolean paused) {
    pauseGame.store(paused);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_requestQuit(JNIEnv*, jclass) {
    quit.store(true);
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_menuValue(JNIEnv*, jclass, jboolean aspect) {
    const int pending = (aspect ? pendingAspect : pendingVolume).load();
    return pending >= 0 ? pending : (aspect ? aspectChoice : volumeChoice).load();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setMenuValue(JNIEnv*, jclass, jboolean aspect, jint value) {
    if (aspect && value >= 0 && value <= 2) pendingAspect.store(value);
    if (!aspect && value >= 0 && value <= 100) pendingVolume.store(value);
}
extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkSurface_setRendererReady(JNIEnv*, jclass, jboolean ready) {
    aurora::window::set_surface_ready(ready);
}

// ---- Live state feed (dev.wiids.cityfolk.CityFolkState.read) ------------------------------------
// A snapshot of guest state published once per guest frame on the game thread and copied out by
// Java under a mutex. Everything is read from addresses the runtime already documents and uses
// (USA addresses through VA(), all present in version_addrs.inc); nothing is written to the guest.
// The one guest call is the game's own player-data getter fn_801017B8(0) (as cf_skin.cpp and
// online.cpp call it), made with the registers saved and restored.
//   stage byte            VA(0x8074B5BC) >> 24, flags table VA(0x80479E60)   (cf_warp.cpp)
//   acre block objects    VA(0x80574A0C), 49 x 0x74: +0x5C model, +0x60 column, +0x64 row (cf_warp.cpp)
//   acres                 16 tiles x 32 units = 512 units, acre (c, r) at x = 512 c, z = 512 r
//                         (cf_grass.cpp tiles; cf_warp.cpp river mouth)
//   player manager        VA(0x805D26C0): +0x24 + 4 n = actor of player number n (PAL main.dol
//                         0x80101C28 stores it, 0x80101CAC returns it), +0x34 + 4 n its save slot
//   player actor          +0x60 vtable (d_a_playerNP = module 80, data section 5), +0x64 / +0x6C
//                         x / z, +0x7E yaw (s16), +0x2240 player number (cf_region.cpp, cf_skin.cpp)
//   pockets               player data + 0x7F22, 15 x u16, 0xFFF1 = empty (online.cpp)
//   layouts               nw4r::lyt::Pane: +0x14 children, +0xB8 alpha, +0xBB bit 0 visible, +0xBC
//                         name (cf_hud.cpp, cf_menu_nav.cpp); Pockets = Layout/item_win.arc
//                         (N_itmWin_all), town map = Layout/map.arc map_win_00 (N_all > N_map_00),
//                         pointer = Layout/pointer.arc (N_Trans > N_hand00/01) or cursor.arc
//                         (N_Trans > N_Rot); other pointer screens = a visible Bounding pane 'B*'
//                         that is not the field HUD's, as menu_nav defines a menu.
namespace {
using recomp::rd16;
using recomp::rd32;
using recomp::rd8;
using recomp::rdf32;

constexpr int kStateInts = 128, kStateIntsOld = 96, kStateFloats = 8;  // ints[96..] only for callers that pass 128
constexpr u32 kStageCount = 0x44, kStageField = 0x00, kStageCity = 0x2D;
constexpr u32 kFlagRoom = 0x10, kFlagDemo = 0x20;
constexpr u32 kBlocks = 49, kBlockSize = 0x74, kGrid = 7;
constexpr u32 kPaneChildren = 0x14, kPaneAlpha = 0xB8, kPaneFlags = 0xBB, kPaneName = 0xBC, kPaneBytes = 0xD0;
constexpr u32 kPlayerModule = 80, kActorBytes = 0x2280;
constexpr int kGridSettle = 30;  // guest frames the 49 blocks must stay the same before they are reported

std::mutex stateMutex;
jint stateInts[kStateInts];
jfloat stateFloats[kStateFloats];
bool statePublished = false;

// Layouts drawn since the last publish (guest thread only).
struct Seen {
    bool pockets = false, map = false, other = false, cursor = false;
    int skipped = 0;  // Map screen layouts not drawn (setLowerOwnsMap)
    int pocketsSkipped = 0;  // Pockets screen layouts not drawn (setLowerOwnsPockets)
    int wipe = 0;            // mWipeFader_c drawn: 1 partly open, 2 fully closed (black)
    bool message = false;    // the text box (msg_win: a visible B_msgWin) drawn
} seen;
int lastGrid[kBlocks];
int gridStable = 0;

bool guestRange(u32 a, u32 size) {
    return (a & 3) == 0 && ((a >= 0x80004000u && a <= 0x81800000u - size) || (a >= 0x90000000u && a <= 0x94000000u - size));
}
bool paneOk(u32 p) { return guestRange(p, kPaneBytes); }
bool paneVisible(u32 p) { return (rd8(p + kPaneFlags) & 1) != 0; }
bool paneNamed(u32 p, const char* name) {
    for (u32 i = 0; i < 16; i++) {
        const u8 want = static_cast<u8>(name[i]);
        if (static_cast<u8>(rd8(p + kPaneName + i)) != want) return false;
        if (!want) return true;
    }
    return name[16] == 0;
}
bool paneNameStarts(u32 p, const char* prefix) {
    for (u32 i = 0; prefix[i] && i < 16; i++)
        if (static_cast<u8>(rd8(p + kPaneName + i)) != static_cast<u8>(prefix[i])) return false;
    return true;
}
// The visible child of `pane` with that name, or 0 (at most `limit` children looked at).
u32 visibleChild(u32 pane, const char* name, int limit) {
    const u32 head = pane + kPaneChildren;
    u32 node = rd32(head);
    for (int i = 0; node && node != head && i < limit; i++) {
        const u32 child = node - 4;
        if (!paneOk(child)) return 0;
        if (paneVisible(child) && paneNamed(child, name)) return child;
        node = rd32(node);
    }
    return 0;
}
// A visible Bounding pane named 'B*' that is not the field HUD's (toolbar, action buttons) nor the
// text box: what cf_menu_nav.cpp / menu_nav.cpp take as "a pointer screen is up".
bool menuBounding(u32 pane, int depth, int& budget) {
    if (depth > 32 || budget <= 0 || !paneOk(pane) || !paneVisible(pane)) return false;
    budget--;
    if (rd8(pane + kPaneName) == 'B' && rd32(pane) == VA(0x805231B0) && !paneNameStarts(pane, "B_tb") &&
        !paneNameStarts(pane, "B_actnBtn") && !paneNamed(pane, "B_msgWin"))
        return true;
    const u32 head = pane + kPaneChildren;
    u32 node = rd32(head);
    for (int i = 0; node && node != head && i < 1024; i++) {
        if (!paneOk(node - 4)) return false;
        if (menuBounding(node - 4, depth + 1, budget)) return true;
        node = rd32(node);
    }
    return false;
}

// ---- Town map raster, markers, Map screen suppression, layout log --------------------------------
// The game paints the town map into the texture of pane map_00 (Layout/map.arc map_win_00:
// map_240_CI4_00, 256 x 256 CI4) when the Map screen opens: PAL d_menu_mapNP.rel .text 0xC70 takes
// FindPane("map_00") -> GetMaterial (pane + 0x28, vtable + 0x68 = 0x8029E638) -> TexMap array
// (material + 0x40, 0x802A545C) -> image pointer (TexMap + 0) and calls main.dol 0x800CE494(image,
// step) for steps 0..4, one per frame. 0x800CE07C paints acre column step + 1, rows 1..5: for every
// tile the 3 x 3 colour indices of its collision type (0x80073434), at pixel ((tileX - 16) * 3 + i,
// (tileZ - 16) * 3 + j) through the 8 x 8-block nibble writer 0x802B608C (even pixel = high nibble).
// So: 240 x 240, 3 pixels per tile, pixel (0, 0) = the north-west corner of acre (1, 1), x east,
// y south; terrain only (buildings are the marker layouts). The raster is read back from that
// texture while the map layout is drawn, once it has stayed the same for kMapSettle frames.
constexpr int kMapSide = 240, kMapSettle = 12, kMarkers = 12;
// Marker slots of ints[72..95]: the map's own structure ids (d_menu_mapNP.rel data + 0xB0).
constexpr u32 kMarkerIds[kMarkers] = {0xD001, 0xD002, 0xD003, 0xD004,  // player houses 1-4
                                      0xD015, 0xD016, 0xD013, 0xD017, 0xD014, 0xD041,
                                      0xD009, 0xD00A};  // villager houses 1-2

std::atomic<bool> lowerOwnsMap{false};
std::atomic<bool> layoutLog{[] {
    const char* v = std::getenv("CF_WIIDS_LYT_LOG");
    return v && *v && *v != '0';
}()};

std::mutex mapMutex;
u8 mapPublished[kMapSide * kMapSide];
jint mapPalette[16];
int mapRevision = 0;
// Guest thread only.
u8 mapLast[kMapSide * kMapSide], mapNow[kMapSide * kMapSide];
int mapStable = 0;
int pointerScreen = 0;  // ints[1] of the last published frame
std::unordered_set<std::string> loggedLayouts;
int loggedScreen = 0;

void decodeMap(u32 image, u8* out) {
    for (int y = 0; y < kMapSide; y++)
        for (int x = 0; x < kMapSide; x++) {
            const u32 n = static_cast<u32>(((y >> 3) * 32 + (x >> 3)) * 64 + (y & 7) * 8 + (x & 7));
            const u8 b = static_cast<u8>(rd8(image + (n >> 1)));
            out[y * kMapSide + x] = (n & 1) ? (b & 0xF) : (b >> 4);
        }
}
// A new revision when the picture or its palette changed.
void publishMap(const u8* pixels, const jint* argb) {
    std::lock_guard lock(mapMutex);
    if (mapRevision && std::memcmp(mapPublished, pixels, sizeof mapPublished) == 0 &&
        std::memcmp(mapPalette, argb, sizeof mapPalette) == 0)
        return;
    std::memcpy(mapPublished, pixels, sizeof mapPublished);
    std::memcpy(mapPalette, argb, sizeof mapPalette);
    mapRevision++;
}

// Game functions that are not in version_addrs.inc, per region {USA, PAL, JPN, KOR}: from
// config/<id>/us_map.txt, where each is matched to the USA function by its code hash. One is used
// only if the running game's code at the address starts with the function's first instructions;
// otherwise 0 and the feature stays off.
u32 regionFn(u32 usa, u32 pal, u32 jpn, u32 kor, u32 w0, u32 w1, u32 w2) {
    u32 a = usa;
    switch (recomp::ver::id()) {
    case recomp::ver::Id::PAL: a = pal; break;
    case recomp::ver::Id::JPN: a = jpn; break;
    case recomp::ver::Id::KOR: a = kor; break;
    default: break;
    }
    return rd32(a) == w0 && rd32(a + 4) == w1 && rd32(a + 8) == w2 ? a : 0;
}
// fn_800CE334(image, step): the town map painter (stwu; mflr; cmplwi r4, 5).
u32 painterFn() {
    static const u32 a = regionFn(0x800CE334, 0x800CE494, 0x800CE5EC, 0x800CE384, 0x9421FFF0u, 0x7C0802A6u, 0x28040005u);
    return a;
}
// fn_800C1CF8(item record): the record's name in the game's language (stwu; mflr; stw r0).
u32 itemNameFn() {
    static const u32 a = regionFn(0x800C1CF8, 0x800C1D18, 0x800C1C80, 0x800C1CDC, 0x9421FFF0u, 0x7C0802A6u, 0x90010014u);
    return a;
}
// Map at town load. The game's own painter (PAL
// main.dol 0x800CE494(image, step) (see above), run exactly as the Map screen runs it: steps 0..4 on
// five consecutive frames, into a 256 x 256 CI4 buffer of the port's own (hleAlloc: outside the
// game's heaps). It reads the loaded town's collision and pairs its collision-context push / pop
// (0x800755A0(1) ... (0)) itself on every path past the push; the registers are saved and restored
// around each call. Run at the frame boundary, in the FIELD stage, once its 49 blocks are settled,
// the player exists, no stage change is pending and no pointer screen has been up for kGenIdle frames.
constexpr int kGenIdle = 30;
u32 genBuffer = 0;
int genStep = 0, genWait = 0, idleFrames = 0;
bool genDone = false, haveGenerated = false;
u8 mapGenerated[kMapSide * kMapSide];
jint lastPalette[16];  // of the last game raster captured (0 = not seen yet)

bool generatorPresent() { return painterFn() != 0; }
void generateMapStep(recomp::Ctx& c) {
    if (genWait > 0) {
        genWait--;
        return;
    }
    if (!genBuffer) genBuffer = recomp::hleAlloc(0x8000, 32);
    const recomp::Ctx saved = c;
    const u32 ok = recomp::guestCall(c, painterFn(), genBuffer, static_cast<u32>(genStep));
    c = saved;
    if (!ok) {  // the game's own "no field" answer
        genStep = 0;
        genWait = 120;
        return;
    }
    if (++genStep < 5) return;
    genStep = 0;
    genDone = true;
    decodeMap(genBuffer, mapGenerated);
    haveGenerated = true;
    publishMap(mapGenerated, lastPalette);
    __android_log_print(ANDROID_LOG_INFO, "WiiDsCityFolk", "map: painted at town load by the game's painter");
}

void captureMap(u32 mapGroup) {
    const u32 pane = visibleChild(mapGroup, "map_00", 16);
    if (!pane) return;
    const u32 material = rd32(pane + 0x28);
    if (!guestRange(material, 0x44)) return;
    const u32 texMap = rd32(material + 0x40);
    if (!guestRange(texMap, 0x1C)) return;
    const u32 image = rd32(texMap), palette = rd32(texMap + 4);
    if (rd16(texMap + 8) != 256 || rd16(texMap + 0xA) != 256 || !guestRange(image, 0x8000)) return;
    decodeMap(image, mapNow);
    if (std::memcmp(mapNow, mapLast, sizeof mapNow) != 0) {
        std::memcpy(mapLast, mapNow, sizeof mapNow);
        mapStable = 0;
        return;
    }
    if (++mapStable != kMapSettle) return;
    jint argb[16] = {};  // 0 = palette not readable
    if (guestRange(palette & ~3u, 32) && (palette & 1) == 0)
        for (u32 i = 0; i < 16; i++) {  // RGB565, the TPL's palette format
            const u32 v = rd16(palette + 2 * i);
            const u32 r = v >> 11, g = (v >> 5) & 0x3F, b = v & 0x1F;
            argb[i] = static_cast<jint>(0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2));
        }
    if (haveGenerated) {  // debug check: the town-load picture against the Map screen's own
        int differ = 0;
        for (int i = 0; i < kMapSide * kMapSide; i++) differ += mapNow[i] != mapGenerated[i];
        __android_log_print(ANDROID_LOG_INFO, "WiiDsCityFolk", "map: Map screen raster vs town-load raster: %d of %d pixels differ%s",
                            differ, kMapSide * kMapSide, differ ? "" : " (identical)");
    }
    std::memcpy(lastPalette, argb, sizeof lastPalette);
    publishMap(mapNow, argb);
}

// ---- Markers, Pockets ---------------------------------------------------------------------------
// The Map screen's 20 markers in its own order (d_menu_mapNP.rel data + 0xB0) and the icon frame
// each gets (.text 0x1528): houses of residents 6, the player's own 7, villager houses 8, then
// Nook 0, Able 1, town hall 2, museum 3, gate 4, D041 5.
constexpr u32 kMapIds[20] = {0xD001, 0xD002, 0xD003, 0xD004, 0xD009, 0xD00A, 0xD00B, 0xD00C, 0xD00D, 0xD00E,
                             0xD00F, 0xD010, 0xD011, 0xD012, 0xD015, 0xD016, 0xD013, 0xD017, 0xD014, 0xD041};
// A villager's name as the Map screen's list shows it (PAL d_menu_mapNP.rel). The list (fn_6_11E4)
// adds entry 4 + n for villager slot n < 10 when fn_80135DE4(record) holds (.text 0x134C-0x1390),
// and that entry is the marker index: the markers loop (.text 0x43C-0x5D4) takes id = data + 0xB0
// [entry] (kMapIds above: entries 4..13 = D009..D012, the villager houses by slot) and the icon
// frame fn_6_1528(entry) = 8 for 4..13. fn_6_13C0 (.text 0x1468-0x14AC) writes the entry's name:
//   villager = fn_80129B64(fn_8010DFB8() + 0x21B20, n) = save + 0x21B20 + n * 0x3040 (fn_8011B6E4,
//              n < 10); save = fn_8010DFB8(), the base the player data (+ 0x20 + s * 0x86C0,
//              fn_801018A8 / fn_8013AFEC) and the structure table (+ 0x5EB04, fn_8014B49C) use;
//   record   = villager + 0x224C; fn_80135DE4(record): u16 + 0xBC != 0xFFFF, u8 + 0xBE != 6,
//              u16 + 0x16 != 0;
//   name     = fn_80135F34(record, 10) (what fn_80135F80(record, text, 10) copies into the row):
//              10 = the running language (fn_80106B38: SCGetLanguage mapped), whose name index
//              (u32 table 0x80476A98: 0,1,1,1,2,3,4,5,6,7) picks one of eight 0x12-byte UTF-16 names
//              at record + 0x2C + 0x12 i (fn_80135CF4 stores them through fn_80135CD8).
// Both game functions only read; they are called with the registers saved and restored. PAL only:
// the record offsets are proven on the PAL disc alone (the matched functions on the other discs,
// config/*/us_map.txt: USA 0x80136134 / 0x80136284, JPN 0x80135D24 / 0x80135E74, KOR 0x80135C28 /
// 0x80135D78, are not checked), so elsewhere villager houses keep no name.
constexpr u32 kVillagers = 10, kVillagerTable = 0x21B20, kVillagerBytes = 0x3040, kVillagerRecord = 0x224C;
constexpr u32 kVillagerNames = 0x2C, kVillagerNameBytes = 0x12, kVillagerNameCount = 8;
u32 palFn(u32 pal, u32 w0, u32 w1, u32 w2) {
    if (recomp::ver::id() != recomp::ver::Id::PAL) return 0;
    return rd32(pal) == w0 && rd32(pal + 4) == w1 && rd32(pal + 8) == w2 ? pal : 0;
}
// fn_80135DE4(record): does the slot hold a villager (stwu; mflr; stw r0).
u32 villagerValidFn() {
    static const u32 a = palFn(0x80135DE4, 0x9421FFF0u, 0x7C0802A6u, 0x90010014u);
    return a;
}
// fn_80135F34(record, language): the name's address (stwu; mflr; cmpwi r4, 10).
u32 villagerNameFn() {
    static const u32 a = palFn(0x80135F34, 0x9421FFF0u, 0x7C0802A6u, 0x2C04000Au);
    return a;
}
bool villagerName(recomp::Ctx& c, u32 save, u32 slot, std::u16string& out) {
    const u32 validFn = villagerValidFn(), nameFn = villagerNameFn();
    if (!validFn || !nameFn || slot >= kVillagers) return false;
    const u32 record = save + kVillagerTable + slot * kVillagerBytes + kVillagerRecord;
    if (!guestRange(record, 0xC0)) return false;
    const recomp::Ctx saved = c;
    const u32 valid = recomp::guestCall(c, validFn, record);
    const u32 text = valid ? recomp::guestCall(c, nameFn, record, 10) : 0;
    c = saved;
    if (!valid || text < record + kVillagerNames) return false;
    const u32 at = text - (record + kVillagerNames);
    if (at % kVillagerNameBytes != 0 || at >= kVillagerNameBytes * kVillagerNameCount) return false;
    out.clear();
    for (u32 k = 0; k < kVillagerNameBytes / 2; k++) {
        const u32 ch = rd16(text + 2 * k);
        if (!ch) break;
        out += static_cast<char16_t>(ch);
    }
    return true;
}
std::atomic<bool> lowerOwnsPockets{false};
// Under stateMutex.
jint markerOut[60];
int markerCount = 0;
jint pocketCodes[15], pocketIcons[15], pocketExtra[8];
bool pocketsValid = false;
jfloat pocketsLayout[72];
bool pocketsLayoutValid = false;
// Guest thread: this frame's Pockets slot centres and choice rows (KPAD pointer units).
float slotXY[30];
bool slotsSeen = false;
float rowXY[10];
int rowCount = 0;
std::unordered_map<u32, int> iconCache;
std::mutex nameMutex;
std::unordered_map<u32, std::u16string> itemNames;  // under nameMutex
// Under stateMutex.
jint pocketKinds[15];
std::u16string markerNames[20];
bool markerHasName[20];

u32 childNamed(u32 pane, const char* name, int limit) {
    const u32 head = pane + kPaneChildren;
    u32 node = rd32(head);
    for (int i = 0; node && node != head && i < limit; i++) {
        const u32 child = node - 4;
        if (!paneOk(child)) return 0;
        if (paneNamed(child, name)) return child;
        node = rd32(node);
    }
    return 0;
}
// A pane's centre (Pane::GetPaneRect through its global matrix, as cf_menu_nav.cpp paneBox) in
// KPAD pointer units, by the scale the game's hit tests were last seen to use (304 x -228 default).
void paneCentre(u32 p, float& px, float& py) {
    const float w = rdf32(p + 0x4C), h = rdf32(p + 0x50);
    const u8 base = static_cast<u8>(rd8(p + 0xBA));
    const float lx = ((base % 3 == 0) ? 0.0f : (base % 3 == 1) ? -w * 0.5f : -w) + w * 0.5f;
    const float ly = ((base / 3 == 0) ? 0.0f : (base / 3 == 1) ? h * 0.5f : h) - h * 0.5f;
    const float gx = rdf32(p + 0x84) * lx + rdf32(p + 0x88) * ly + rdf32(p + 0x90);
    const float gy = rdf32(p + 0x94) * lx + rdf32(p + 0x98) * ly + rdf32(p + 0xA0);
    float sx = 304.0f, sy = -228.0f;
    recomp::cf::navWalkScale(sx, sy);
    px = gx / sx;
    py = gy / sy;
}
// Text of a nw4r::lyt::TextBox (PAL main.dol: built at 0x8029FFA8, size 0x104; +0xD8 UTF-16BE
// buffer, +0xFC its size in bytes, +0xFE the text length in characters, as its Draw 0x802A0778 reads
// them), without control characters and tags (0x1A, then a byte with the tag's length in bytes).
std::u16string rowText[5], dialogText;  // guest thread: this frame's
bool dialogSeen = false;
// Under stateMutex.
std::u16string shownRows[5], shownDialog;
int shownRowCount = 0;
bool shownDialogUp = false;

std::u16string paneText(u32 pane) {
    std::u16string out;
    if (!pane || !guestRange(pane, 0x104)) return out;
    const u32 buf = rd32(pane + 0xD8), bytes = rd16(pane + 0xFC), len = rd16(pane + 0xFE);
    if (len == 0 || len > 512 || len * 2 > bytes || (buf & 1) || !guestRange(buf & ~3u, bytes + 4)) return out;
    for (u32 i = 0; i < len; i++) {
        const u32 ch = rd16(buf + 2 * i);
        if (ch == 0) break;
        if (ch == 0x1A) {
            const u32 size = i + 1 < len ? rd16(buf + 2 * i + 2) >> 8 : 0;
            i += (size >= 4 ? size / 2 : 1) - 1;
            continue;
        }
        if (ch == '\n') out += u' ';
        else if (ch >= 0x20 && ch != 0x7F) out += static_cast<char16_t>(ch);
    }
    return out;
}
// The visible pane of that name under `pane` (every ancestor up to `pane` visible too), or 0.
u32 findVisible(u32 pane, const char* name, int depth, int& budget) {
    if (depth > 8 || budget <= 0 || !paneOk(pane) || !paneVisible(pane)) return 0;
    budget--;
    if (paneNamed(pane, name)) return pane;
    const u32 head = pane + kPaneChildren;
    u32 node = rd32(head);
    for (int i = 0; node && node != head && i < 64; i++) {
        if (!paneOk(node - 4)) return 0;
        if (const u32 hit = findVisible(node - 4, name, depth + 1, budget)) return hit;
        node = rd32(node);
    }
    return 0;
}
u32 findVisible(u32 root, const char* name) {
    int budget = 256;
    return findVisible(root, name, 0, budget);
}
// A choice box (chc_win_00): rows B_msg_00..04 with their texts T_msg_00..04.
void noteChoiceRows(u32 all) {
    if (dialogSeen) return;  // a message dialog in front owns the rows
    const u32 lines = childNamed(all, "N_msgLine", 8);
    const u32 rows = lines ? childNamed(lines, "N_b_msg_all", 8) : 0;
    const u32 texts = lines ? childNamed(lines, "N_msg_all", 8) : 0;
    if (!rows || !paneVisible(lines) || !paneVisible(rows)) return;
    rowCount = 0;
    for (int i = 0; i < 5; i++) {
        char name[16];
        std::snprintf(name, sizeof name, "B_msg_%02d", i);
        if (const u32 b = visibleChild(rows, name, 8)) {
            paneCentre(b, rowXY[2 * rowCount], rowXY[2 * rowCount + 1]);
            std::snprintf(name, sizeof name, "T_msg_%02d", i);
            rowText[rowCount] = paneText(texts ? childNamed(texts, name, 8) : 0);
            rowCount++;
        }
    }
}
// A message dialog (cmn_btn.arc cmn_dialog_A/B/C_00): message T_dialg_00, buttons B_dialgBtn_00..02
// with labels T_dialgBtn_00..02; its buttons become the rows, left to right.
void noteDialog(u32 root) {
    dialogSeen = true;
    dialogText = paneText(findVisible(root, "T_dialg_00"));
    rowCount = 0;
    for (int i = 0; i < 3; i++) {
        char name[20];
        std::snprintf(name, sizeof name, "B_dialgBtn_%02d", i);
        const u32 b = findVisible(root, name);
        if (!b) continue;
        float x, y;
        paneCentre(b, x, y);
        std::snprintf(name, sizeof name, "T_dialgBtn_%02d", i);
        std::u16string text = paneText(findVisible(root, name));
        int at = rowCount;
        while (at > 0 && rowXY[2 * (at - 1)] > x) {  // keep them left to right
            rowXY[2 * at] = rowXY[2 * (at - 1)];
            rowXY[2 * at + 1] = rowXY[2 * (at - 1) + 1];
            rowText[at] = std::move(rowText[at - 1]);
            at--;
        }
        rowXY[2 * at] = x;
        rowXY[2 * at + 1] = y;
        rowText[at] = std::move(text);
        rowCount++;
    }
}
// A pane's centre in layout coordinates.
void paneLayoutCentre(u32 p, float& gx, float& gy) {
    const float w = rdf32(p + 0x4C), h = rdf32(p + 0x50);
    const u8 base = static_cast<u8>(rd8(p + 0xBA));
    const float lx = ((base % 3 == 0) ? 0.0f : (base % 3 == 1) ? -w * 0.5f : -w) + w * 0.5f;
    const float ly = ((base / 3 == 0) ? 0.0f : (base / 3 == 1) ? h * 0.5f : h) - h * 0.5f;
    gx = rdf32(p + 0x84) * lx + rdf32(p + 0x88) * ly + rdf32(p + 0x90);
    gy = rdf32(p + 0x94) * lx + rdf32(p + 0x98) * ly + rdf32(p + 0xA0);
}
// Guest thread: the Pockets' slot circles in layout units (centre, radius), its other hit boxes in
// pointer units (BC_player, B_btnL_00, B_btnL_01, BC_ltrBtn_00, B_ltrWin_00..09; NaN = not shown),
// and the number on its Bells button (T_btnL_00) as last shown.
float slotL[30], slotRad[15];
float extraXY[28];
int letterSlots = 0;
int walletShown = -1;

void clearExtraBoxes() {
    for (float& v : extraXY) v = std::numeric_limits<float>::quiet_NaN();
    letterSlots = 0;
}
void noteLetterSlots(u32 root) {
    int count = 0;
    for (int i = 0; i < 10; i++) {
        char name[16];
        std::snprintf(name, sizeof name, "B_ltrWin_%02d", i);
        if (const u32 b = findVisible(root, name)) {
            paneCentre(b, extraXY[8 + 2 * i], extraXY[9 + 2 * i]);
            count++;
        }
    }
    if (count) letterSlots = count;
}
void notePocketSlots(u32 root, u32 window) {
    const u32 holes = childNamed(window, "N_hole_all", 16);
    if (!holes) return;
    for (int i = 0; i < 15; i++) {
        char hole[16], circle[16];
        std::snprintf(hole, sizeof hole, "N_hole_%02d", i);
        std::snprintf(circle, sizeof circle, "BC_%02d", i);
        const u32 h = childNamed(holes, hole, 20);
        const u32 b = h ? childNamed(h, circle, 8) : 0;
        if (!b) return;
        paneCentre(b, slotXY[2 * i], slotXY[2 * i + 1]);
        paneLayoutCentre(b, slotL[2 * i], slotL[2 * i + 1]);
        slotRad[i] = 0.5f * rdf32(b + 0x4C) * std::fabs(rdf32(b + 0x84));
    }
    slotsSeen = true;
    const char* const boxes[3] = {"BC_player", "B_btnL_00", "B_btnL_01"};
    for (int i = 0; i < 3; i++)
        if (const u32 b = findVisible(root, boxes[i])) paneCentre(b, extraXY[2 * i], extraXY[2 * i + 1]);
    noteLetterSlots(root);
    // The Bells button's number, as the window shows it.
    const std::u16string text = paneText(findVisible(root, "T_btnL_00"));
    long long value = 0;
    int digits = 0;
    for (const char16_t ch : text)
        if (ch >= u'0' && ch <= u'9' && digits < 10) {
            value = value * 10 + (ch - u'0');
            digits++;
        }
    if (digits > 0 && digits < 10) walletShown = static_cast<int>(value);
}
// The icon the Pockets show for an item code: the frame of item_icon_00_item.brlan the game sets
// (PAL d_menu_itemNP.rel .text 0x1584-0x15E0): record = fn_800C27E0(fn_800C24CC(), &code), frame =
// s16 at record + 0xA, 0 when not below 0x174. -1: the game has no record for the code.
int itemIcon(recomp::Ctx& c, u32 code) {
    if (const auto it = iconCache.find(code); it != iconCache.end()) return it->second;
    static const u32 scratch = recomp::hleAlloc(16);
    const u32 mgrFn = VA(0x800C24CC), recFn = VA(0x800C27E0);
    if (!mgrFn || !recFn) return -1;
    recomp::wr16(scratch, code);
    const recomp::Ctx saved = c;
    const u32 mgr = recomp::guestCall(c, mgrFn);
    const u32 rec = recomp::guestCall(c, recFn, mgr, scratch);
    c = saved;
    if (!guestRange(rec & ~3u, 0x18C) || (rec & 1)) return -1;
    const u32 raw = static_cast<u32>(static_cast<s32>(static_cast<s16>(rd16(rec + 0xA))));
    const int icon = raw >= 0x174 ? 0 : static_cast<int>(raw);
    if (const u32 nameFn = itemNameFn()) {
        const recomp::Ctx saved2 = c;
        const u32 text = recomp::guestCall(c, nameFn, rec);
        c = saved2;
        std::u16string name;
        if (!(text & 1) && guestRange(text & ~3u, 0x28))
            for (u32 i = 0; i < 17; i++) {
                const u32 ch = rd16(text + 2 * i);
                if (!ch) break;
                name += static_cast<char16_t>(ch);
            }
        std::lock_guard lock(nameMutex);
        if (itemNames.size() < 4096) itemNames[code] = std::move(name);
    }
    if (iconCache.size() < 4096) iconCache.emplace(code, icon);
    return icon;
}

void appendName(std::string& out, u32 pane) {
    for (u32 i = 0; i < 16; i++) {
        const u8 ch = static_cast<u8>(rd8(pane + kPaneName + i));
        if (!ch) break;
        out += (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?';
    }
    if (!paneVisible(pane)) out += '!';  // hidden
}
// Diagnostic (CF_WIIDS_LYT_LOG=1 / setLayoutLog): each distinct layout root drawn while a pointer
// screen is up, once per screen, as "root>child[grandchildren]..." ('!' = hidden pane).
void logLayout(u32 root, const char* kind) {
    if (pointerScreen != loggedScreen) {
        loggedScreen = pointerScreen;
        loggedLayouts.clear();
        if (pointerScreen) __android_log_print(ANDROID_LOG_INFO, "WiiDsCityFolk", "lyt: pointer screen %d up", pointerScreen);
    }
    if (!pointerScreen || loggedLayouts.size() >= 96) return;
    std::string sig;
    appendName(sig, root);
    const u32 head = root + kPaneChildren;
    u32 node = rd32(head);
    for (int i = 0; node && node != head && i < 6 && paneOk(node - 4); i++, node = rd32(node)) {
        const u32 child = node - 4;
        sig += '>';
        appendName(sig, child);
        sig += '[';
        const u32 head2 = child + kPaneChildren;
        u32 node2 = rd32(head2);
        for (int j = 0; node2 && node2 != head2 && j < 10 && paneOk(node2 - 4); j++, node2 = rd32(node2)) {
            if (j) sig += ',';
            appendName(sig, node2 - 4);
        }
        sig += ']';
    }
    if (!loggedLayouts.insert(sig).second) return;
    __android_log_print(ANDROID_LOG_INFO, "WiiDsCityFolk", "lyt: screen %d %s root %08X %s", pointerScreen, kind, root, sig.c_str());
}

void stateDefaults(jint* ints, jfloat* floats) {
    for (int i = 0; i < kStateInts; i++) ints[i] = -1;
    for (int i = 0; i < kStateFloats; i++) floats[i] = std::numeric_limits<float>::quiet_NaN();
    ints[0] = ints[1] = ints[2] = ints[3] = 0;
    ints[68] = ints[69] = ints[70] = 0;
    ints[71] = 1;
}
}  // namespace

namespace wiids {
bool noteLayoutDrawn(unsigned root, unsigned) {
    if (!paneOk(root)) return false;
    const bool logging = layoutLog.load(std::memory_order_relaxed);
    if (!paneVisible(root)) {
        if (logging) logLayout(root, "hidden");
        return false;
    }
    const char* kind = "";
    bool mapScreen = false;      // a layout only the Map screen has (Layout/map.arc)
    bool pocketsScreen = false;  // a layout the Pockets screen draws (item_win.arc, icon.arc, ltr_win.arc, chc_win.arc)
    bool hand = false, classified = false;
    const u32 anyAll = childNamed(root, "N_all", 8);
    if (anyAll && (childNamed(anyAll, "N_mapIcn_all", 8) || childNamed(anyAll, "player_00", 8))) {
        // map_icon_00: a marker or the player's marker, whichever of its panes is shown
        mapScreen = classified = true;
        kind = "map-icon";
    } else if (anyAll && childNamed(anyAll, "N_cmnBtnA_00", 8) && (seen.map || pointerScreen == 2)) {
        // cmn_btn_A_00 (the OK / close button) while the Map screen is up
        mapScreen = true;
        kind = "map-button";
    } else if (const u32 window = visibleChild(root, "N_itmWin_all", 8)) {
        seen.pockets = true;
        pocketsScreen = classified = true;
        kind = "pockets";
        notePocketSlots(root, window);
    } else if (childNamed(root, "N_ltrBtn_all", 8)) {  // ltr_btn_00: the Pockets' letters button
        pocketsScreen = true;
        kind = "pockets-letters";
        if (const u32 b = findVisible(root, "BC_ltrBtn_00")) paneCentre(b, extraXY[6], extraXY[7]);
    } else if (visibleChild(root, "N_dialg", 8)) {  // cmn_dialog_A / B: a message dialog
        pocketsScreen = true;
        kind = "dialog";
        noteDialog(root);
    } else if (childNamed(root, "N_gift", 8)) {  // gift_icon_00
        pocketsScreen = true;
        kind = "gift-icon";
    } else if (const u32 all = visibleChild(root, "N_all", 8)) {
        if (const u32 group = visibleChild(all, "N_map_00", 32)) {  // map_win_00
            seen.map = true;
            mapScreen = classified = true;
            kind = "map";
            captureMap(group);
        } else if (visibleChild(all, "N_mapIcn_all", 8)) {  // map_icon_00: one marker
            mapScreen = classified = true;
            kind = "map-icon";
        } else if (visibleChild(all, "N_mapCursor", 8) || visibleChild(all, "N_houseCursor", 8)) {  // map_cursor_00
            mapScreen = classified = true;
            kind = "map-cursor";
        } else if (childNamed(all, "icnBase", 8) && childNamed(all, "icnFc", 8)) {  // item_icon_00: one item
            pocketsScreen = true;
            kind = "item-icon";
        } else if (childNamed(all, "N_check", 8)) {  // item_mark_00 / ltr_icon_00
            pocketsScreen = true;
            kind = "item-mark";
        } else if (visibleChild(all, "N_dialg_all", 8)) {  // cmn_dialog_C
            pocketsScreen = true;
            kind = "dialog";
            noteDialog(root);
        } else if (childNamed(all, "N_ltrWin_all", 8)) {  // ltr_win_00: the letters panel
            pocketsScreen = true;
            kind = "letters";
            noteLetterSlots(root);
        } else if (childNamed(all, "blnTailSdw_00", 8)) {  // bal.arc balloon_00: the name balloon
            pocketsScreen = true;
            kind = "balloon";
        } else if (childNamed(all, "N_chcWin_all", 8)) {  // chc_win_00: a choice box
            pocketsScreen = true;
            kind = "choice";
            noteChoiceRows(all);
        }
    }
    if (!classified) {
        const u32 trans = visibleChild(root, "N_Trans", 8);
        if (trans && (visibleChild(trans, "N_hand00", 8) || visibleChild(trans, "N_hand01", 8) || visibleChild(trans, "N_Rot", 8))) {
            if (rd8(root + kPaneAlpha) && rd8(trans + kPaneAlpha)) seen.cursor = true;
            hand = true;
            kind = "cursor";
        } else {
            int budget = 2048;
            if (menuBounding(root, 0, budget)) {
                seen.other = true;
                if (!*kind) kind = "menu";
            }
        }
    }
    if (!seen.message && findVisible(root, "B_msgWin")) seen.message = true;
    if (logging) logLayout(root, *kind ? kind : "-");
    // Opt-in: the lower screen owns the Map / the Pockets. Their layouts are not drawn while that
    // screen is up (this frame so far, or the last frame: parts are drawn before their window).
    const bool mapUp = seen.map || pointerScreen == 2, pocketsUp = seen.pockets || pointerScreen == 1;
    const bool ownMap = mapUp && lowerOwnsMap.load(std::memory_order_relaxed);
    const bool ownPockets = pocketsUp && lowerOwnsPockets.load(std::memory_order_relaxed);
    if (mapScreen && ownMap) {
        seen.skipped++;
        return true;
    }
    if (pocketsScreen && ownPockets) {
        seen.pocketsSkipped++;
        return true;
    }
    return hand && (ownMap || ownPockets);  // the pointer belongs to the lower screen meanwhile
}

void publishState(recomp::Ctx& c) {
    jint ints[kStateInts];
    jfloat floats[kStateFloats];
    stateDefaults(ints, floats);

    // Scene, from the game's current stage and its flags.
    const u32 stage = rd32(VA(0x8074B5BC)) >> 24;
    const u32 flags = stage < kStageCount ? rd32(VA(0x80479E60) + 4 * stage) : 0;
    int scene;
    if (stage >= kStageCount) scene = 0;  // 0x44 until the first stage is loaded
    else if (stage == kStageField) scene = 2;
    else if (stage == kStageCity) scene = 4;
    else if (flags & kFlagDemo)  // DM_TITLE, DM_LOAD, DM_PL_SEL, DM_BUS_PL_CRT = the front end
        scene = (stage == 0x38 || stage == 0x3A || stage == 0x3B || stage == 0x3C) ? 1 : 5;
    else if ((flags & kFlagRoom) && stage != 0x43) scene = 3;
    else scene = 5;
    ints[0] = scene;
    const bool playing = scene == 2 || scene == 3 || scene == 4;

    ints[1] = seen.pockets ? 1 : seen.map ? 2 : seen.other ? 3 : 0;
    ints[69] = seen.cursor ? 1 : 0;
    ints[70] = !lowerOwnsMap.load(std::memory_order_relaxed) ? 0 : seen.skipped ? 2 : 1;
    floats[7] = !lowerOwnsPockets.load(std::memory_order_relaxed) ? 0.0f : seen.pocketsSkipped ? 2.0f : 1.0f;
    // ints[96..100] (callers that pass 128 ints): the circle wipe (cf_hud.cpp fn_802B6B48, frame byte
    // fader + 0x44: 0 not drawn, 1 partly open, 2 fully closed), the text box (0 none, 1 up and no
    // pointer screen, 2 up with one), a demo stage (flag 0x20: title, save, load, bus rides, gate
    // scenes), the upper picture's luminance (as topLevel), a stage change requested (manager + 0x1F).
    ints[96] = seen.wipe;
    ints[97] = !seen.message ? 0 : ints[1] ? 2 : 1;
    ints[98] = (flags & kFlagDemo) ? 1 : 0;
    ints[99] = aurora_luma_probe_level();
    ints[100] = stage < kStageCount && (rd32(VA(0x805FB218) + 0x1C) & 0xFF) ? 1 : 0;
    pointerScreen = ints[1];
    idleFrames = ints[1] ? 0 : idleFrames + 1;
    if (!seen.map) mapStable = 0;
    jint extra[8] = {-1, -1, -1, seen.pockets ? 1 : 0, static_cast<jint>(floats[7]), rowCount, -1, -1};
    jfloat layout[72];
    for (int i = 0; i < 72; i++) layout[i] = std::numeric_limits<float>::quiet_NaN();
    const bool layoutValid = seen.pockets && slotsSeen;
    if (layoutValid) std::memcpy(layout, slotXY, sizeof slotXY);
    layout[30] = static_cast<float>(rowCount);
    for (int i = 0; i < 2 * rowCount; i++) layout[31 + i] = rowXY[i];
    {
        float sx = 304.0f, sy = -228.0f;
        recomp::cf::navWalkScale(sx, sy);
        layout[41] = sx;
        layout[42] = sy;
        // The slot under the guest pointer: computed here from the pointer the game was given and
        // the slots' circles (the screen hit-tests on the fly; no field of its own was found).
        float px, py;
        if (layoutValid && recomp::menunav::lastPointer(0, px, py))
            for (int i = 0; i < 15; i++) {
                const float dx = px * sx - slotL[2 * i], dy = py * sy - slotL[2 * i + 1];
                if (dx * dx + dy * dy <= slotRad[i] * slotRad[i]) extra[2] = i;
            }
    }
    if (!playing) walletShown = -1;
    extra[0] = walletShown;
    for (int i = 0; i < 28; i++) layout[43 + i] = extraXY[i];
    layout[71] = static_cast<float>(letterSlots);
    clearExtraBoxes();
    jint kinds[15] = {};
    std::u16string names[20];
    bool hasName[20] = {};
    std::u16string texts[5], dialog;
    const int rowsShown = rowCount;
    const bool dialogUp = dialogSeen;
    for (int i = 0; i < rowsShown; i++) texts[i] = std::move(rowText[i]);
    if (dialogUp) dialog = std::move(dialogText);
    dialogSeen = false;
    slotsSeen = false;
    rowCount = 0;
    jint markers[60];
    int markersFound = 0;
    jint icons[15];
    for (int i = 0; i < 15; i++) icons[i] = -1;
    seen = Seen{};

    // The town's acres: reported once the 49 block objects have held one whole 7 x 7 grid for
    // kGridSettle frames (indoors they hold the room; a stage change rewrites them over frames).
    bool townReady = false;
    if (scene == 2) {
        ints[2] = ints[3] = kGrid;
        floats[3] = 512.0f;
        floats[4] = 0.0f;
        floats[5] = 0.0f;
        int grid[kBlocks];
        for (u32 i = 0; i < kBlocks; i++) grid[i] = -1;
        const u32 blocks = VA(0x80574A0C);
        bool whole = blocks != 0;
        for (u32 i = 0; i < kBlocks && whole; i++) {
            const u32 o = blocks + kBlockSize * i;
            const u32 model = rd32(o + 0x5C), col = rd32(o + 0x60), row = rd32(o + 0x64);
            if (col >= kGrid || row >= kGrid || model >= 1000 || grid[row * kGrid + col] >= 0) whole = false;
            else grid[row * kGrid + col] = static_cast<int>(model);
        }
        if (whole && std::memcmp(grid, lastGrid, sizeof grid) == 0) {
            if (gridStable < kGridSettle) gridStable++;
        } else {
            gridStable = 0;
            for (u32 i = 0; i < kBlocks; i++) lastGrid[i] = whole ? grid[i] : -1;
        }
        if (whole && gridStable >= kGridSettle) {
            townReady = true;
            for (u32 i = 0; i < kBlocks; i++) ints[4 + i] = grid[i];
        }
    } else {
        gridStable = 0;
    }

    if (playing) {
        // The actor of player number 0 (the player being played).
        const u32 mgr = VA(0x805D26C0);
        const u32 actor = mgr ? rd32(mgr + 0x24) : 0;
        const u32 data = recomp::relBase(kPlayerModule, 5), dataSize = recomp::relSectionSize(kPlayerModule, 5);
        if (guestRange(actor, kActorBytes) && data && dataSize) {
            const u32 vtable = rd32(actor + 0x60);
            if (vtable >= data && vtable < data + dataSize && rd32(actor + 0x2240) == 0) {
                const float x = rdf32(actor + 0x64), z = rdf32(actor + 0x6C);
                if (std::isfinite(x) && std::isfinite(z)) {
                    floats[0] = x;
                    floats[1] = z;
                    floats[2] = static_cast<float>(static_cast<s16>(rd16(actor + 0x7E))) * (3.14159265358979f / 32768.0f);
                }
            }
        }
        // Pockets of player number 0, through the game's own getter.
        if (const u32 getter = VA(0x801017B8)) {
            const recomp::Ctx saved = c;
            const u32 pd = recomp::guestCall(c, getter, 0);
            c = saved;
            if (guestRange(pd, 0x86C0)) {
                for (u32 i = 0; i < 15; i++) {
                    const u32 code = rd16(pd + 0x7F22 + 2 * i);
                    ints[53 + i] = code == 0xFFF1 ? -1 : static_cast<jint>(code);
                    if (code != 0xFFF1) icons[i] = itemIcon(c, code);
                    // Wrapped items: the slot's state byte (fn_801381B0: pd + 0x863C + slot).
                    const u32 state = rd8(pd + 0x863C + i);
                    kinds[i] = code == 0xFFF1 ? 0 : state == 1 ? 1 : (state == 2 || state == 4) ? 2 : state == 6 ? 3 : 0;
                }
                ints[68] = 1;
                // Building markers: the save's structure table as the game's map reads it (PAL
                // main.dol 0x8014AEC4: s8 tile x, z at table + 4 + 2 * (id & 0xFFF), both non-zero
                // when the town has it; table = save + 0x5EB04, 0x8014B49C). The save holds player
                // slot s at save + 0x20 + s * 0x86C0 (0x801018DC, 0x8013B030), so it is found from
                // the player data of a resident (slot < 4, manager + 0x34).
                const u32 slot = rd32(VA(0x805D26C0) + 0x34);
                if (slot < 4 && pd >= 0x90000000u + 0x20 + slot * 0x86C0) {
                    const u32 table = pd - 0x20 - slot * 0x86C0 + 0x5EB04;
                    if (guestRange(table, 4 + 2 * 0x45)) {
                        floats[6] = static_cast<float>(slot);
                        for (u32 m = 0; m < 20; m++) {
                            const u32 e = table + 4 + 2 * (kMapIds[m] & 0xFFF);
                            const int x = static_cast<s8>(rd8(e)), z = static_cast<s8>(rd8(e + 1));
                            if (x == 0 || z == 0) continue;
                            markers[3 * markersFound] = m < 4 ? (m == slot ? 7 : 6) : m < 14 ? 8 : static_cast<jint>(m - 14);
                            markers[3 * markersFound + 1] = x;
                            markers[3 * markersFound + 2] = z;
                            if (m < 4) {  // the resident's name: 8 UTF-16 characters at pd + 0x7EDA
                                const u32 name = pd - slot * 0x86C0 + m * 0x86C0 + 0x7EDA;
                                hasName[markersFound] = true;
                                for (u32 k = 0; k < 8; k++) {
                                    const u32 ch = rd16(name + 2 * k);
                                    if (!ch) break;
                                    names[markersFound] += static_cast<char16_t>(ch);
                                }
                            } else if (m < 14) {  // villager house D009 + n: villager slot n (villagerName)
                                hasName[markersFound] =
                                    villagerName(c, table - 0x5EB04, m - 4, names[markersFound]);
                            }
                            markersFound++;
                        }
                        for (int m = 0; m < kMarkers; m++) {
                            const u32 e = table + 4 + 2 * (kMarkerIds[m] & 0xFFF);
                            const int x = static_cast<s8>(rd8(e)), z = static_cast<s8>(rd8(e + 1));
                            if (x != 0 && z != 0) {
                                ints[72 + 2 * m] = x;
                                ints[73 + 2 * m] = z;
                            }
                        }
                    }
                }
            }
        }
    }

    // The town map without opening the Map screen (see generateMapStep).
    if (scene != 2) genDone = false;
    if (!genDone && generatorPresent() && townReady && std::isfinite(floats[0]) && idleFrames >= kGenIdle &&
        (rd32(VA(0x805FB218) + 0x1C) & 0xFF) == 0)
        generateMapStep(c);
    else
        genStep = 0;

    std::lock_guard lock(stateMutex);
    markerCount = markersFound;
    std::memcpy(markerOut, markers, sizeof(jint) * 3 * static_cast<size_t>(markersFound));
    pocketsValid = ints[68] == 1;
    std::memcpy(pocketCodes, ints + 53, sizeof pocketCodes);
    std::memcpy(pocketIcons, icons, sizeof pocketIcons);
    std::memcpy(pocketExtra, extra, sizeof pocketExtra);
    std::memcpy(pocketsLayout, layout, sizeof pocketsLayout);
    pocketsLayoutValid = layoutValid;
    std::memcpy(pocketKinds, kinds, sizeof pocketKinds);
    for (int i = 0; i < markersFound; i++) {
        markerHasName[i] = hasName[i];
        markerNames[i] = std::move(names[i]);
    }
    shownRowCount = rowsShown;
    for (int i = 0; i < rowsShown; i++) shownRows[i] = std::move(texts[i]);
    shownDialogUp = dialogUp;
    shownDialog = std::move(dialog);
    std::memcpy(stateInts, ints, sizeof ints);
    std::memcpy(stateFloats, floats, sizeof floats);
    statePublished = true;
}
}  // namespace wiids

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_cityfolk_CityFolkState_read(JNIEnv* env, jclass, jintArray ints, jfloatArray floats) {
    if (!ints || !floats || env->GetArrayLength(ints) < kStateIntsOld || env->GetArrayLength(floats) < kStateFloats)
        return JNI_FALSE;
    const jsize n = env->GetArrayLength(ints) >= kStateInts ? kStateInts : kStateIntsOld;
    jint i[kStateInts];
    jfloat f[kStateFloats];
    {
        std::lock_guard lock(stateMutex);
        if (!statePublished) return JNI_FALSE;
        std::memcpy(i, stateInts, sizeof i);
        std::memcpy(f, stateFloats, sizeof f);
    }
    env->SetIntArrayRegion(ints, 0, n, i);
    env->SetFloatArrayRegion(floats, 0, kStateFloats, f);
    return JNI_TRUE;
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readMap(JNIEnv* env, jclass, jint knownRevision, jbyteArray indices, jintArray argbPalette) {
    if (!indices || !argbPalette || env->GetArrayLength(indices) < kMapSide * kMapSide || env->GetArrayLength(argbPalette) < 16)
        return 0;
    std::lock_guard lock(mapMutex);
    if (mapRevision == 0 || mapRevision == knownRevision) return mapRevision;
    env->SetByteArrayRegion(indices, 0, kMapSide * kMapSide, reinterpret_cast<const jbyte*>(mapPublished));
    env->SetIntArrayRegion(argbPalette, 0, 16, mapPalette);
    return mapRevision;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setLowerOwnsMap(JNIEnv*, jclass, jboolean owns) {
    lowerOwnsMap.store(owns);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setLayoutLog(JNIEnv*, jclass, jboolean on) {
    layoutLog.store(on);
}

// Every marker of the game's Map screen the town has, in the map's order, as (kind, tileX, tileZ);
// kind = the marker's icon frame (0 Nook, 1 Able, 2 town hall, 3 museum, 4 gate, 5 D041, 6 another
// resident's house, 7 the player's own house, 8 a villager's house). Returns the count (out >= 60).
extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readMarkers(JNIEnv* env, jclass, jintArray out) {
    if (!out || env->GetArrayLength(out) < 60) return 0;
    jint copy[60];
    int count;
    {
        std::lock_guard lock(stateMutex);
        count = markerCount;
        std::memcpy(copy, markerOut, sizeof copy);
    }
    if (count > 0) env->SetIntArrayRegion(out, 0, count * 3, copy);
    return count;
}

// Pockets: 15 item codes (-1 empty), the 15 icon frames of item_icon_00_item.brlan (-1 empty /
// unknown), extra (>= 8): [0] bells in the wallet (-1: not known), [1] held item code (-1: not
// known), [2] slot under the pointer / selected (-1: not known), [3] Pockets screen up (0 / 1),
// [4] setLowerOwnsPockets status (0 off, 1 on, 2 on and layouts skipped this frame), [5] choice
// rows shown, [6], [7] -1. False while the pockets are not valid (as ints[68]).
extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readPockets(JNIEnv* env, jclass, jintArray codes, jintArray icons, jintArray extra) {
    if (!codes || !icons || !extra || env->GetArrayLength(codes) < 15 || env->GetArrayLength(icons) < 15 ||
        env->GetArrayLength(extra) < 8)
        return JNI_FALSE;
    jint c[15], i[15], e[8];
    bool valid;
    {
        std::lock_guard lock(stateMutex);
        valid = pocketsValid;
        std::memcpy(c, pocketCodes, sizeof c);
        std::memcpy(i, pocketIcons, sizeof i);
        std::memcpy(e, pocketExtra, sizeof e);
    }
    env->SetIntArrayRegion(extra, 0, 8, e);
    if (!valid) return JNI_FALSE;
    env->SetIntArrayRegion(codes, 0, 15, c);
    env->SetIntArrayRegion(icons, 0, 15, i);
    return JNI_TRUE;
}

// Where the guest pointer must be to act on the Pockets (KPAD units, x -1..1 left to right, y -1..1
// top to bottom, as setPointer): out[0..29] the 15 slot centres (x, y), out[30] the number of
// choice rows shown, out[31..40] their centres top to bottom, out[41], out[42] the layout scale
// used (layout units per pointer unit, x and y). Returns 1 while the Pockets layout is drawn (the
// slot centres are current), else 0 (only out[30..42] are written).
extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readPocketsLayout(JNIEnv* env, jclass, jfloatArray out) {
    // out[43..70] (when the array has room): centres of BC_player, B_btnL_00 (Bells), B_btnL_01,
    // BC_ltrBtn_00 (letters button), B_ltrWin_00..09 (letter slots), NaN = not shown; out[71] the
    // number of letter slots shown.
    const jsize length = out ? env->GetArrayLength(out) : 0;
    if (length < 43) return 0;
    const jsize n = length < 72 ? 43 : 72;
    jfloat copy[72];
    bool valid;
    {
        std::lock_guard lock(stateMutex);
        valid = pocketsLayoutValid;
        std::memcpy(copy, pocketsLayout, sizeof copy);
    }
    if (valid) env->SetFloatArrayRegion(out, 0, n, copy);
    else env->SetFloatArrayRegion(out, 30, n - 30, copy + 30);
    return valid ? 1 : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_cityfolk_CityFolkGameActivity_setLowerOwnsPockets(JNIEnv*, jclass, jboolean owns) {
    lowerOwnsPockets.store(owns);
}

// Labels of the rows readPocketsLayout reports (out[30] of them, same order): the choice box's rows
// top to bottom, or a message dialog's buttons left to right. Null when none are shown.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readChoiceRows(JNIEnv* env, jclass) {
    std::u16string rows[5];
    int count;
    {
        std::lock_guard lock(stateMutex);
        count = shownRowCount;
        for (int i = 0; i < count; i++) rows[i] = shownRows[i];
    }
    if (count <= 0) return nullptr;
    const jclass string = env->FindClass("java/lang/String");
    if (!string) return nullptr;
    const jobjectArray out = env->NewObjectArray(count, string, nullptr);
    if (!out) return nullptr;
    for (int i = 0; i < count; i++) {
        const jstring s = env->NewString(reinterpret_cast<const jchar*>(rows[i].data()), static_cast<jsize>(rows[i].size()));
        if (!s) return nullptr;
        env->SetObjectArrayElement(out, i, s);
        env->DeleteLocalRef(s);
    }
    return out;
}

// The message of the dialog that is up (cmn_dialog: "You can't hold any more Bells!"), or null.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readDialog(JNIEnv* env, jclass) {
    std::u16string text;
    {
        std::lock_guard lock(stateMutex);
        if (!shownDialogUp) return nullptr;
        text = shownDialog;
    }
    return env->NewString(reinterpret_cast<const jchar*>(text.data()), static_cast<jsize>(text.size()));
}

// The game's name of an item (the item record's name in the game's language, as the Pockets'
// balloon shows it), for codes that have been in the pockets since the game started; else null.
extern "C" JNIEXPORT jstring JNICALL
Java_dev_wiids_cityfolk_CityFolkState_itemName(JNIEnv* env, jclass, jint code) {
    std::u16string name;
    {
        std::lock_guard lock(nameMutex);
        const auto it = itemNames.find(static_cast<u32>(code));
        if (it == itemNames.end()) return nullptr;
        name = it->second;
    }
    return env->NewString(reinterpret_cast<const jchar*>(name.data()), static_cast<jsize>(name.size()));
}

// How each pocket slot is shown: 0 its item icon (icons15), 1 gift_icon_00_present, 2
// gift_icon_00_present1, 3 gift_icon_00_egg (wrapped; d_menu_itemNP.rel .text 0x1440).
extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readPocketKinds(JNIEnv* env, jclass, jintArray kinds) {
    if (!kinds || env->GetArrayLength(kinds) < 15) return JNI_FALSE;
    jint k[15];
    {
        std::lock_guard lock(stateMutex);
        if (!pocketsValid) return JNI_FALSE;
        std::memcpy(k, pocketKinds, sizeof k);
    }
    env->SetIntArrayRegion(kinds, 0, 15, k);
    return JNI_TRUE;
}

// Names for readMarkers' markers, same order and count: a resident's name for the player houses
// (kinds 6, 7), the villager's name for a villager house (kind 8) when the Map screen would list
// that villager (villagerName; PAL only), null for the others. Null when there are no markers.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_dev_wiids_cityfolk_CityFolkState_readMapNames(JNIEnv* env, jclass) {
    std::u16string names[20];
    bool has[20];
    int count;
    {
        std::lock_guard lock(stateMutex);
        count = markerCount;
        for (int i = 0; i < count; i++) {
            has[i] = markerHasName[i];
            names[i] = markerNames[i];
        }
    }
    if (count <= 0) return nullptr;
    const jclass string = env->FindClass("java/lang/String");
    if (!string) return nullptr;
    const jobjectArray out = env->NewObjectArray(count, string, nullptr);
    if (!out) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!has[i]) continue;
        const jstring s = env->NewString(reinterpret_cast<const jchar*>(names[i].data()), static_cast<jsize>(names[i].size()));
        if (!s) return nullptr;
        env->SetObjectArrayElement(out, i, s);
        env->DeleteLocalRef(s);
    }
    return out;
}

// ---- Performance summary (debug.wiids.cf.perf=1), upper frame level --------------------------
// Host thread, once per main-loop frame (main.cpp): the time aurora_begin_frame took (frame slot:
// waits for the renderer / display), frame::hostFrame (lockstep: the game's CPU work for one game
// frame; interpolating: one replay) and aurora_end_frame (FIFO drain, hand-off). Once a second,
// when the Android property is 1, one logcat line (tag WiiDsCityFolk).
namespace wiids {
void perfFrame(double beginMs, double runMs, double endMs) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point windowStart = Clock::now();
    static u32 frames = 0, interpolated = 0, lastGuest = 0;
    static double sum[3] = {}, peak[3] = {};
    static AuroraPipelineStats lastStats{};
    const double v[3] = {beginMs, runMs, endMs};
    for (int i = 0; i < 3; i++) {
        sum[i] += v[i];
        peak[i] = std::max(peak[i], v[i]);
    }
    frames++;
    if (recomp::frame::interpolating()) interpolated++;
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - windowStart).count();
    if (seconds < 1.0) return;
    char value[PROP_VALUE_MAX] = {};
    const bool on = __system_property_get("debug.wiids.cf.perf", value) > 0 && value[0] == '1';
    const u32 guest = recomp::frame::guestFrames();
    if (on) {
        AuroraPipelineStats stats{};
        aurora_pipeline_cache_get_stats(&stats);
        const AuroraWindowSize size = aurora_get_window_size();
        const double n = frames ? frames : 1;
        __android_log_print(ANDROID_LOG_INFO, "WiiDsCityFolk",
            "perf: game %.1f fps, presented %.1f fps (%u replayed tapes), game native %d Hz, target %d; ms avg/max: game frame %.2f/%.2f, "
            "begin %.2f/%.2f, end %.2f/%.2f; pipelines: +%u on demand, +%u draws waited %.1f ms, %u pending, +%u shader cache misses; "
            "window %ux%u, render %ux%u, surface %ux%u",
            (guest - lastGuest) / seconds, frames / seconds, interpolated, recomp::game::nativeFps(), recomp::frame::targetFps(),
            sum[1] / n, peak[1], sum[0] / n, peak[0], sum[2] / n, peak[2],
            stats.demandCreated - lastStats.demandCreated, stats.stalledDraws - lastStats.stalledDraws,
            double(stats.stallNs - lastStats.stallNs) / 1e6, stats.pending, stats.blobMisses - lastStats.blobMisses,
            size.width, size.height, size.fb_width, size.fb_height, size.native_fb_width, size.native_fb_height);
        lastStats = stats;
    }
    lastGuest = guest;
    windowStart = now;
    frames = interpolated = 0;
    for (int i = 0; i < 3; i++) sum[i] = peak[i] = 0;
}
void noteWipe(unsigned frameByte) {
    const int state = frameByte == 0xFF ? 2 : 1;
    if (state > seen.wipe) seen.wipe = state;
}
}  // namespace wiids

// Average luminance of the upper screen's game image, 0..255 (Aurora's probe: nine rows of every
// sixth frame, read back asynchronously, lightly smoothed). -1 until the first sample.
extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_cityfolk_CityFolkState_topLevel(JNIEnv*, jclass) {
    static std::atomic<bool> enabled{false};
    if (!enabled.exchange(true)) aurora_luma_probe_enable(true);
    return aurora_luma_probe_level();
}
