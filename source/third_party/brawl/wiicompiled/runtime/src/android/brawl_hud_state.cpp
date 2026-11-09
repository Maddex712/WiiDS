// brawl_hud_state.cpp - the match state the WiiDS lower screen draws (GameActivity.nativeReadBrawlState).
//
// Read straight from guest memory on the UI thread, once per display frame. Every pointer is checked
// against the guest RAM windows first, so a value read mid-update can only show a stale number,
// never fault. Addresses are RSBE01 revision 2 (doldecomp/brawl config RSBE01_02):
//   gfSceneManager*   *(0x805A0060)        (gfSceneManager::getInstance: lwz r3,-0x43C0(r13))
//     ->m_currentScene +0x04, scene->m_sceneName +0x00 ("scMelee" in a match)
//     scMelee->m_operatorRuleMelee +0x54 -> frames left +0xE0, frames played +0xE4 (found on device with the
//     probe below; the decomp headers put them at +0x58 / +0xA0)
//   GameGlobal*       *(0x805A00E0)  ->m_modeMelee +0x08
//     gmMeleeInitData at +0x08 (m_timeLimitFrames +0x18), gmPlayerInitData[4] at +0x98, 0x5C each:
//     state +0x01 (0 human, 1 CPU), stock count +0x04, colour +0x06, team +0x0B, CPU level +0x1F
//   ftOwner data also: KOs per victim +0x38 (4 each), self-destructs +0x58 (half), Final Smash flag +0x1F & 0x80,
//     coins +0x5C; init +0x0B teams, +0x0F self-destruct score
//   ftEntryManager*   *(0x80B87C48)  (sora_melee .bss + 0x2E88) ->m_entries +0x00, count +0x04
//     ftEntry (0x244 each): owner +0x28, player number +0x58, gmCharacterKind +0x5C
//     ftOwner->data +0x00: damage float +0x24, falls +0x30, stocks left +0x34
//   IfMngr*           *(0x805A02D0)  ->m_isPauseMenuActive +0xA9
#if defined(__ANDROID__)

#include "memory.h"
#include "abi_bridge.h"

#include <android/log.h>
#include <jni.h>
#include <sys/system_properties.h>

#include <cstdint>
#include <cstring>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

namespace {

bool Readable(uint32_t addr, size_t length)
{
    const bool mem1 = addr >= Memory::kMem1CachedBase && addr + length <= Memory::kMem1CachedBase + Memory::kMem1Size;
    const bool mem2 = addr >= Memory::kMem2CachedBase && addr + length <= Memory::kMem2CachedEnd;
    return (mem1 || mem2) && Memory::Contains(addr, length);
}

uint32_t Be32(uint32_t addr)
{
    if (!Readable(addr, 4)) return 0;
    const uint8_t* p = Memory::GetPointer(addr, 4);
    return p == nullptr ? 0 : (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint8_t Byte(uint32_t addr)
{
    if (!Readable(addr, 1)) return 0;
    const uint8_t* p = Memory::GetPointer(addr, 1);
    return p == nullptr ? 0 : *p;
}

float BeFloat(uint32_t addr)
{
    const uint32_t bits = Be32(addr);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool InMelee(uint32_t& ruleMelee)
{
    const uint32_t manager = Be32(0x805A0060);
    const uint32_t scene = Be32(manager + 0x04);
    const uint32_t name = Be32(scene);
    if (!Readable(name, 8)) return false;
    if (std::memcmp(Memory::GetPointer(name, 8), "scMelee", 8) != 0) return false;
    ruleMelee = Be32(scene + 0x54);
    return true;
}

// `setprop debug.wiids.hudprobe 1`: every 2 s in a battle, log the words in the objects the melee scene points
// at (+0x14..+0x100) that hold a frame count below the time limit and fell since the last look - the clock.
void ProbeClock(uint32_t scene, uint32_t limit)
{
    static int calls = 0;
    if (++calls % 240 != 0) return;
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get("debug.wiids.hudprobe", value) <= 0 || value[0] != '1') return;
    static std::map<std::pair<uint32_t, uint32_t>, uint32_t> last;
    for (uint32_t field = 0x14; field < 0x100; field += 4) {
        const uint32_t object = Be32(scene + field);
        if (!Readable(object, 0x400)) continue;
        for (uint32_t off = 0; off < 0x400; off += 4) {
            const uint32_t v = Be32(object + off);
            const auto key = std::make_pair(field, off);
            const auto it = last.find(key);
            if (v > 0 && v <= limit && it != last.end() && v < it->second && it->second - v < 600) {
                __android_log_print(ANDROID_LOG_INFO, "Brawl-ThorHUD", "probe scene+0x%X -> +0x%X = %u (was %u)",
                                    field, off, v, it->second);
            }
            last[key] = v;
        }
    }
}

// Layout of the int[] (version 3). Header, then one 14-int block per player slot 0..3:
//   header: version, in melee, paused, frames left, frames played, time limit, players, team battle,
//           frames presented so far (for a frame rate), self-destruct score (the rules' -1/-2),
//           the upper picture's bright level (0..255, -1 unknown), the current scene object (changes per scene)
//   player: present, gmCharacterKind, costume, damage x10, stocks left, stock rule, falls, team, CPU, CPU rank,
//           KOs, self-destructs, Final Smash ready, coins
constexpr int kHeader = 12;
constexpr int kPerPlayer = 14;
constexpr int kResult = kHeader + 4 * kPerPlayer;
constexpr int kResultPlayers = kResult + 4;
constexpr int kResultExtra = kResultPlayers + 4 * 4;
constexpr int kSnapshotLength = kResultExtra + 2;

std::atomic<bool> g_lowerPanels{false};
// 0 idle, 1 queued, 2 guest task running, 3 complete, -1 failed. The UI only requests;
// the guest CPU creates and retires gmSysSave, just as Brawl's shutdown manager does.
std::atomic<int> g_saveState{0};
std::mutex g_saveMutex;
std::condition_variable g_saveDone;
uint32_t g_saveScene = 0;
uint32_t g_ownedSave = 0;
uint64_t g_saveErrors = 0;

constexpr uint32_t kHudMdlShim = 0xF1000000, kHudGroupShim = 0xF1000004;
bool g_drawHooksReady = false;
bool g_drawSuppression = false;
std::array<uint32_t, 4 * 53> g_panelObjects{};
size_t g_panelObjectCount = 0;

void CollectPanelObjects()
{
    g_panelObjectCount = 0;
    uint32_t rule = 0;
    if (!g_drawSuppression || !InMelee(rule)) return;
    const uint32_t manager = Be32(0x805A02D0);
    const uint32_t entryManager = Be32(0x80B87C48), entries = Be32(entryManager);
    const uint32_t count = Be32(entryManager + 4);
    if (!Readable(manager, 0x64)) return;
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        const uint32_t entry = entries + i * 0x244;
        if (!Readable(entry, 0x244) || !Readable(Be32(Be32(entry + 0x28)), 0x38)) continue;
        const uint32_t slot = Be32(entry + 0x58);
        if (slot >= 4) continue;
        const uint32_t player = Be32(manager + 0x4C + slot * 4);
        if (!Readable(player, 0xFC)) continue;
        // IfPlayer::disappear (800E13D8/800E1410) owns exactly three groups
        // and fifty MuObjects. Digits are individual models, outside the groups.
        for (uint32_t off = 0x28; off < 0xFC; off += 4) {
            uint32_t object = Be32(player + off);
            if (off < 0xF0) object = Be32(object + 0x10);
            if (!Readable(object, 4) || g_panelObjectCount == g_panelObjects.size()) continue;
            g_panelObjects[g_panelObjectCount++] = object;
        }
    }
}

void InstallDrawHooks(bool lower)
{
    // Verified RSBE01_02 vtables from the real objects. Native shim addresses are
    // registry-only targets, outside guest memory; no guest instructions are replaced.
    constexpr uint32_t mdlSlot = 0x804663D4, groupSlot = 0x80466504;
    const uint32_t mdl = Be32(mdlSlot), group = Be32(groupSlot);
    if ((mdl != 0x801B21E8 && mdl != kHudMdlShim)
        || (group != 0x801B51BC && group != kHudGroupShim)) {
        g_drawHooksReady = false;
        return;  // incompatible mod: keep the upper HUD
    }
    if (!lower && mdl != kHudMdlShim && group != kHudGroupShim) return;
    if (mdl != kHudMdlShim) Memory::Write32(mdlSlot, kHudMdlShim);
    if (group != kHudGroupShim) Memory::Write32(groupSlot, kHudGroupShim);
    g_drawHooksReady = true;
}

bool SuppressPanelDraw(CpuContext* ctx)
{
    // NW4R tasks: 5 gather, 6 opaque draw, 7 translucent draw. World/material/
    // vertex/view calculations continue normally, as do animations and membership.
    if (!ctx || !g_drawHooksReady || !g_drawSuppression
        || !g_lowerPanels.load(std::memory_order_acquire)
        || ctx->gpr[4] < 5 || ctx->gpr[4] > 7) return false;
    uint32_t rule = 0;
    if (!InMelee(rule)) return false;
    const uint32_t object = ctx->gpr[3];
    for (size_t i = 0; i < g_panelObjectCount; ++i)
        if (object == g_panelObjects[i]) return true;
    return false;
}


bool SaveMenu(uint32_t& scene)
{
    scene = Be32(Be32(0x805A0060) + 4);
    const uint32_t name = Be32(scene);
    if (!Readable(name, 24) || !Readable(Be32(0x805A00E0), 0x50)) return false;
    const char* text = reinterpret_cast<const char*>(Memory::GetPointer(name, 24));
    return std::strncmp(text, "muMenuMain", 24) == 0 || std::strncmp(text, "scSelctCharacter", 24) == 0
        || std::strncmp(text, "scSelStage", 24) == 0 || std::strncmp(text, "scVsResult", 24) == 0;
}

void FinishSave(bool ok)
{
    {
        std::lock_guard<std::mutex> lock(g_saveMutex);
        g_saveState.store(ok ? 3 : -1, std::memory_order_release);
    }
    __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "Brawl-Saves",
                        "guest save %s", ok ? "complete" : "failed; keeping game open");
    g_saveDone.notify_all();
}

} // namespace

extern "C" void func_801B21E8(CpuContext*);
extern "C" void func_801B51BC(CpuContext*);
extern "C" void BrawlDamageModelProc(CpuContext* ctx)
{
    if (!SuppressPanelDraw(ctx)) func_801B21E8(ctx);
}
extern "C" void BrawlDamageGroupProc(CpuContext* ctx)
{
    if (!SuppressPanelDraw(ctx)) func_801B51BC(ctx);
}
REGISTER_NATIVE_FUNCTION(0xF1000000u, BrawlDamageModelProc);
REGISTER_NATIVE_FUNCTION(0xF1000004u, BrawlDamageGroupProc);

extern "C" uint64_t NAND_HLE_ErrorCount();

// Called after the code handler on the guest CPU, not from Android's UI or save worker.
extern "C" void Brawl_ThorFrame(CpuContext* ctx)
{
    char upperOnly[PROP_VALUE_MAX] = {};
    __system_property_get("debug.wiids.upperhud", upperOnly);
    const bool lower = g_lowerPanels.load(std::memory_order_acquire) && upperOnly[0] != '1';
    InstallDrawHooks(lower);
    const bool suppress = lower && g_drawHooksReady;
    if (g_drawSuppression != suppress) {
        g_drawSuppression = suppress;
        __android_log_print(ANDROID_LOG_INFO, "Brawl-ThorHUD", "upper damage draws %s",
                            suppress ? "suppressed for Screen-2" : "restored");
    }
    CollectPanelObjects();

    const int state = g_saveState.load(std::memory_order_acquire);
    if ((state != 1 && state != 2) || ctx == nullptr) return;
    static bool servicing = false;
    if (servicing) return;
    servicing = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{servicing};
    const CpuContext before = *ctx;
    struct Restore { CpuContext* ctx; const CpuContext& before; ~Restore() { *ctx = before; } } restore{ctx, before};
    if (ctx->gpr[1] < 0x100 || !Readable(ctx->gpr[1] - 0x100, 0x100)) { FinishSave(false); return; }
    ctx->gpr[1] -= 0x100;
    Memory::Write32(ctx->gpr[1], before.gpr[1]);
    if (state == 1) {
        uint32_t scene = 0;
        if (!SaveMenu(scene) || scene != g_saveScene || Be32(0x805A00FC) != 0) {
            FinishSave(false);
            return;
        }
        g_saveErrors = NAND_HLE_ErrorCount();
        ctx->gpr[3] = 46;  // actual gfShutdownManager heap argument
        InvokeIndirectCpu(0x8005A3F0, ctx);
        g_ownedSave = Be32(0x805A00FC);
        if (!Readable(g_ownedSave, 20) || ctx->gpr[3] != g_ownedSave) {
            FinishSave(false);
            return;
        }
        g_saveState.store(2, std::memory_order_release);
        __android_log_print(ANDROID_LOG_INFO, "Brawl-Saves", "guest save task=%08X", g_ownedSave);
    } else {
        if (g_ownedSave == 0 || Be32(0x805A00FC) != g_ownedSave) {
            FinishSave(false);  // never destroy a task owned by the game
            return;
        }
        if (Byte(g_ownedSave + 0x10) != 0) {
            const bool ok = NAND_HLE_ErrorCount() == g_saveErrors;
            InvokeIndirectCpu(0x8005A6E8, ctx);  // completed callback cleanup; clears the game's saving flag
            g_ownedSave = 0;
            FinishSave(ok);
        }
    }
}

// While the app menu stays up and silent, let safe menu scenes pump their normal save
// task. Active battles and boot/movie scenes never enter this path.
extern "C" bool Brawl_ThorPumpingSave()
{
    const int state = g_saveState.load(std::memory_order_acquire);
    return state == 1 || state == 2;
}

extern "C" JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetLowerPanels(JNIEnv*, jclass, jboolean ready)
{
    g_lowerPanels.store(ready == JNI_TRUE, std::memory_order_release);
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_wither_nsmbw_GameActivity_nativeCommitGameSave(JNIEnv*, jclass)
{
    std::unique_lock<std::mutex> lock(g_saveMutex);
    int state = g_saveState.load(std::memory_order_acquire);
    if (state != 1 && state != 2) {
        uint32_t scene = 0;
        if (!SaveMenu(scene)) return JNI_TRUE;  // unfinished matches are not completed records
        g_saveScene = scene;
        g_saveState.store(1, std::memory_order_release);
    }
    if (!g_saveDone.wait_for(lock, std::chrono::seconds(15), [] {
            const int state = g_saveState.load(std::memory_order_acquire);
            return state == 3 || state == -1;
        })) {
        __android_log_print(ANDROID_LOG_ERROR, "Brawl-Saves", "guest save still pending after 15 s");
        return JNI_FALSE;  // retain the live task; the game must not be killed
    }
    return g_saveState.load(std::memory_order_acquire) == 3 ? JNI_TRUE : JNI_FALSE;
}

extern "C" uint32_t VI_HLE_PresentedFrames();       // hle/vi.cpp
extern "C" void VI_HLE_SetAppPaused(bool);           // hle/vi.cpp
extern "C" void PAD_HLE_SetAppButtons(uint16_t);     // hle/input/pad.cpp
extern "C" void PAD_HLE_SetL1Shield(bool);
extern "C" void PAD_HLE_SetStandardLayout(bool);
extern "C" void PAD_HLE_SetRumbleEnabled(bool);

// --- the app's controls over the game (GameActivity's WiiDS menu and helpers) -----------------------------------

// The current scene's name ("scStrap", "scMelee", ...), or "" before the game has one.
extern "C" JNIEXPORT jstring JNICALL Java_com_wither_nsmbw_GameActivity_nativeSceneName(JNIEnv* env, jclass)
{
    const uint32_t name = Be32(Be32(Be32(0x805A0060) + 0x04));
    char buffer[24] = {};
    if (Readable(name, 1)) {
        for (size_t i = 0; i + 1 < sizeof(buffer) && Readable(name + i, 1); ++i) {
            const char c = static_cast<char>(Byte(name + static_cast<uint32_t>(i)));
            if (c == 0 || c < 0x20 || c > 0x7E) break;
            buffer[i] = c;
        }
    }
    return env->NewStringUTF(buffer);
}

// Freezes the game at the end of its frame (silent) while the app's menu is open.
extern "C" JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetPaused(JNIEnv*, jclass, jboolean paused)
{
    VI_HLE_SetAppPaused(paused == JNI_TRUE);
}

// GameCube buttons (PAD_BUTTON_*) the app holds on player 1's pad: the strap-screen skip's A, auto-pause's Start.
extern "C" JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetPadButtons(JNIEnv*, jclass, jint buttons)
{
    PAD_HLE_SetAppButtons(static_cast<uint16_t>(buttons));
}

// The Controls page: standard face buttons (else the GameCube layout), L1 as a shield, rumble.
extern "C" JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetControls(JNIEnv*, jclass,
                                                                                       jboolean standardLayout,
                                                                                       jboolean l1Shield,
                                                                                       jboolean rumble)
{
    PAD_HLE_SetStandardLayout(standardLayout == JNI_TRUE);
    PAD_HLE_SetL1Shield(l1Shield == JNI_TRUE);
    PAD_HLE_SetRumbleEnabled(rumble == JNI_TRUE);
}

extern "C" int aurora_thor_top_level();  // aurora-main/lib/aurora.cpp

extern "C" JNIEXPORT jintArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeReadBrawlState(JNIEnv* env, jclass)
{
    jint out[kSnapshotLength] = {};
    out[0] = 4;  // v3's live layout, followed by the game's official completed result
    out[8] = static_cast<jint>(VI_HLE_PresentedFrames());
    out[10] = aurora_thor_top_level();
    out[11] = static_cast<jint>(Be32(Be32(0x805A0060) + 0x04));
    uint32_t ruleMelee = 0;
    if (InMelee(ruleMelee)) {
        out[1] = 1;
        const uint32_t ifMngr = Be32(0x805A02D0);
        out[2] = ifMngr != 0 && Byte(ifMngr + 0xA9) != 0 ? 1 : 0;
        out[3] = Readable(ruleMelee + 0xE0, 8) ? static_cast<jint>(Be32(ruleMelee + 0xE0)) : -1;
        out[4] = Readable(ruleMelee + 0xE0, 8) ? static_cast<jint>(Be32(ruleMelee + 0xE4)) : -1;
        const uint32_t modeMelee = Be32(Be32(0x805A00E0) + 0x08);
        out[5] = modeMelee != 0 ? static_cast<jint>(Be32(modeMelee + 0x08 + 0x18)) : 0;
        out[7] = modeMelee != 0 && Byte(modeMelee + 0x08 + 0x0B) != 0 ? 1 : 0;           // m_isTeams
        out[9] = modeMelee != 0 ? static_cast<int8_t>(Byte(modeMelee + 0x08 + 0x0F)) : -1; // m_suicideScoreMultiplier
        ProbeClock(Be32(Be32(0x805A0060) + 0x04), static_cast<uint32_t>(out[5] > 0 ? out[5] : 3600 * 99));

        const uint32_t entryManager = Be32(0x80B87C48);
        const uint32_t entries = Be32(entryManager);
        const uint32_t count = Be32(entryManager + 0x04);
        int players = 0;
        for (uint32_t i = 0; entries != 0 && i < count && i < 8; ++i) {
            const uint32_t entry = entries + i * 0x244;
            const uint32_t owner = Be32(entry + 0x28);
            const uint32_t data = Be32(owner);
            const int32_t playerNo = static_cast<int32_t>(Be32(entry + 0x58));
            if (owner == 0 || data == 0 || playerNo < 0 || playerNo > 3) continue;
            jint* p = out + kHeader + playerNo * kPerPlayer;
            if (p[0] != 0) continue;  // the first entry for a slot wins (Pokemon Trainer's are one entry)
            const uint32_t setup = modeMelee != 0 ? modeMelee + 0x98 + static_cast<uint32_t>(playerNo) * 0x5C : 0;
            const float damage = BeFloat(data + 0x24);
            p[0] = 1;
            p[1] = static_cast<jint>(Be32(entry + 0x5C));                                  // gmCharacterKind
            p[2] = setup != 0 ? static_cast<int8_t>(Byte(setup + 0x06)) : 0;                // costume
            p[3] = damage >= 0.0f && damage < 10000.0f ? static_cast<jint>(damage * 10.0f + 0.5f) : 0;
            p[4] = static_cast<jint>(Be32(data + 0x34));                                    // stocks left
            p[5] = setup != 0 ? static_cast<int8_t>(Byte(setup + 0x04)) : 0;                // stock rule
            p[6] = static_cast<jint>(Be32(data + 0x30));                                    // falls
            p[7] = setup != 0 ? static_cast<int8_t>(Byte(setup + 0x0B)) : 0;                // team
            p[8] = setup != 0 && Byte(setup + 0x01) == 1 ? 1 : 0;                           // CPU
            p[9] = setup != 0 ? Byte(setup + 0x1F) : 0;                                     // CPU level
            // ftOwner::getBeatCount(player): KOs scored on each player, data + 0x38 + 4 * player.
            jint kos = 0;
            for (uint32_t victim = 0; victim < 4; ++victim) {
                if (static_cast<int32_t>(victim) == playerNo) continue;
                const uint32_t beaten = Be32(data + 0x38 + victim * 4);
                if (beaten < 1000) kos += static_cast<jint>(beaten);
            }
            p[10] = kos;
            p[11] = static_cast<jint>(Readable(data + 0x58, 2) ? (Byte(data + 0x58) << 8 | Byte(data + 0x59)) : 0);
            p[12] = (Byte(data + 0x1F) & 0x80) != 0 ? 1 : 0;                                // ftOwner::setFinal flag
            p[13] = static_cast<jint>(Be32(data + 0x5C));                                   // ftOwner::getCoin
            ++players;
        }
        out[6] = players;
    }
    for (int p = 0; p < 4; ++p) out[kResultPlayers + p * 4] = -1;
    const uint32_t sceneName = Be32(static_cast<uint32_t>(out[11]));
    if (Readable(sceneName, 11) && std::memcmp(Memory::GetPointer(sceneName, 11), "scVsResult", 11) == 0) {
        // RSBE01_02 gmResultInfo (GameGlobal+0x18): players at 0x24, stride 0x2AC.
        // gmPlayerResultInfo: rank +0x0E (zero-based), KOs +0x10, falls +0x14, SDs +0x18.
        const uint32_t result = Be32(Be32(0x805A00E0) + 0x18);
        if (Readable(result, 0x1388)) {
            out[kResult] = Byte(result + 0x1378);
            out[kResult + 1] = static_cast<jint>(Be32(result + 0x1384));
            out[kResult + 2] = Byte(result + 1);
            out[kResult + 3] = Byte(result + 2) != 0 ? 1 : 0;
            out[kResultExtra] = static_cast<int8_t>(Byte(result + 0x1F));
            out[kResultExtra + 1] = Byte(result + 0x0F);
            for (uint32_t p = 0; p < 4; ++p) {
                const uint32_t player = result + 0x24 + p * 0x2AC;
                jint* r = out + kResultPlayers + p * 4;
                r[0] = Byte(player + 0x0E);
                r[1] = static_cast<jint>(Be32(player + 0x10));
                r[2] = static_cast<jint>(Be32(player + 0x14));
                r[3] = (Byte(player + 0x18) << 8) | Byte(player + 0x19);
            }
        }
    }
    jintArray array = env->NewIntArray(kSnapshotLength);
    if (array != nullptr) env->SetIntArrayRegion(array, 0, kSnapshotLength, out);
    return array;
}

#endif
