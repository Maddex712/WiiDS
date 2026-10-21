// Read-only PAL RMCP01 race telemetry for the Thor's independent map display.
// Layouts: pinned mkw RaceConfig/RaceManager/KartObjectProxy headers; pointer chains
// additionally checked against translated Kart::Link::GetKartPosition (8059020C).
#include "abi_bridge.h"
#if defined(__ANDROID__)
#include "android_race_map.h"
#include "memory.h"
#include "runtime_product.h"
#include "runtime_log.h"
#include <jni.h>
#include <cryptopp/sha.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <fstream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <vector>

namespace {
constexpr size_t kHeader = 8, kStride = 8, kPlayers = 12;
using Snapshot = std::array<float, kHeader + kStride * kPlayers>;
Snapshot EmptySnapshot() {
    Snapshot next{};
    next[0] = 1;
    for (size_t i = 0; i < kPlayers; ++i) next[kHeader + i * kStride + 7] = -1;
    return next;
}
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_visible{false};
std::mutex g_mapMutex;
Snapshot g_snapshot = EmptySnapshot();
std::chrono::steady_clock::time_point g_updated;
std::string g_loadedCourseIdentity, g_snapshotCourseIdentity;
thread_local std::string g_readCourseIdentity;
std::unordered_map<std::string, std::string> g_courseHashes;

uint32_t Ptr(uint32_t address, size_t size) {
    const uint32_t p = Memory::Read32(address);
    if ((p & 3) != 0 || p < 0x80000000u || !Memory::Contains(p, size)) return 0;
    return p;
}

int HeldItemIcon(unsigned playerId) noexcept {
    // PAL Item::Manager::CreateInstance (80799138), UpdatePlayers (80799ACC):
    // singleton 0x809C3618, player count +0x10, players +0x14, stride 0x248.
    // Player::Init (807976E0) embeds PlayerInventory at +0x88; SetItem/RemoveItems
    // (807BC940/807BC97C) store kind at inventory+4 and remaining count at +8.
    try {
        const uint32_t manager = Ptr(0x809C3618u, 0x18);
        if (!manager) return -1;
        const unsigned count = Memory::Read8(manager + 0x10);
        if (count == 0 || count > kPlayers || playerId >= count) return -1;
        const uint32_t players = Ptr(manager + 0x14, count * 0x248);
        if (!players) return -1;
        const uint32_t player = players + playerId * 0x248;
        const uint32_t inventory = player + 0x88;
        if (Memory::Read32(inventory) != player) return -1; // Inventory's owner backpointer.
        const unsigned kind = Memory::Read32(inventory + 4);
        const int32_t remaining = static_cast<int32_t>(Memory::Read32(inventory + 8));
        if (kind > 18 || remaining <= 0) return -1; // Retail empty kind is 20.
        // GetItemIconPaneName (80860AF0) uses kind identity except triple
        // mushrooms: kinoko / kinoko_2 / kinoko_3 for one / two / three charges.
        // Wire icon 19 names the two-mushroom variant; other IDs stay retail.
        if (kind == 5) return remaining == 1 ? 4 : remaining == 2 ? 19 : remaining == 3 ? 5 : -1;
        return static_cast<int>(kind);
    } catch (const Memory::AccessViolation&) {
        return -1; // An unavailable item object must not discard valid race telemetry.
    }
}

}

void AndroidRaceMapCourseRead(const char* dvdPath, const char* hostPath) noexcept {
    if (!g_enabled.load(std::memory_order_relaxed) || !RuntimeProduct::IsRetroRewind()
            || !dvdPath || !hostPath) return;
    try {
        const std::string path(dvdPath), host(hostPath), prefix("/Race/Course/");
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!lower.starts_with("/race/course/") || !lower.ends_with(".szs")) return;
        {
            std::lock_guard lock(g_mapMutex);
            g_loadedCourseIdentity.clear();
        }
        const std::string key = path.substr(prefix.size(), path.size() - prefix.size() - 4);
        if (key.empty() || key.size() > 48 || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-'; })) return;
        std::string digest;
        {
            std::lock_guard lock(g_mapMutex);
            const auto found = g_courseHashes.find(host);
            if (found != g_courseHashes.end()) digest = found->second;
        }
        if (digest.empty()) {
            std::ifstream file(host, std::ios::binary);
            if (!file) return;
            CryptoPP::SHA256 hash;
            // Android guest fibers have a 64 KiB host stack; stream storage belongs on the heap.
            std::vector<char> buffer(65536);
            while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
                hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()), static_cast<size_t>(file.gcount()));
            if (!file.eof()) return;
            std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> bytes;
            hash.Final(bytes.data());
            constexpr char hex[] = "0123456789abcdef";
            for (unsigned char value : bytes) { digest += hex[value >> 4]; digest += hex[value & 15]; }
        }
        const std::string identity = key + ":" + digest;
        std::lock_guard lock(g_mapMutex);
        g_courseHashes.emplace(host, digest);
        if (identity != g_loadedCourseIdentity) {
            RT_LOG(RT_TAG_DVD) << "Thor course identity " << identity << " from " << host;
            g_loadedCourseIdentity = identity;
        }
    } catch (...) {
        // Optional presentation telemetry must never change DVD read success or game rules.
        std::lock_guard lock(g_mapMutex);
        g_loadedCourseIdentity.clear();
    }
}

void AndroidPublishRaceMap() noexcept {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    Snapshot next = EmptySnapshot(); // Inactive snapshots never carry stale racers or held items.
    try {
        const uint32_t data = Ptr(0x809BD728u, 0xB90);
        const uint32_t race = Ptr(0x809BD730u, 0x4C);
        const uint32_t manager = Ptr(0x809C18F8u, 0x38);
        if (data && race && manager) {
            const unsigned count = Memory::Read8(data + 0x24);
            const unsigned course = Memory::Read32(data + 0xB68);
            const unsigned stage = Memory::Read32(race + 0x28);
            const uint32_t infos = Ptr(race + 0xC, count * 4);
            const uint32_t karts = Ptr(manager + 0x20, count * 4);
            if (count > 0 && count <= kPlayers && course < 32 && stage <= 3 && infos && karts) {
                next[1] = 1;
                next[2] = static_cast<float>(course);
                next[3] = static_cast<float>(stage);
                next[5] = Memory::Read8(data + 0xB84);
                next[6] = static_cast<float>(count);
                next[7] = static_cast<float>(Memory::Read32(race + 0x20));
                if (const uint32_t timer = Ptr(race + 0x14, 0x50)) {
                    next[4] = Memory::Read16(timer + 8) * 60000.0f
                            + Memory::Read8(timer + 10) * 1000.0f + Memory::Read16(timer + 12);
                }
                for (unsigned i = 0; i < count; ++i) {
                    const uint32_t info = Ptr(infos + i * 4, 0x54);
                    const uint32_t kart = Ptr(karts + i * 4, 4);
                    if (!info || !kart) continue;
                    const uint32_t accessor = Ptr(kart, 0x64);
                    if (!accessor) continue;
                    const uint32_t body = Ptr(accessor + 8, 0x94);
                    if (!body) continue;
                    const uint32_t physics = Ptr(body + 0x90, 0x100);
                    if (!physics) continue;
                    const uint32_t dynamics = Ptr(physics + 4, 0x80);
                    if (!dynamics) continue;
                    const float x = Memory::ReadFloat32(dynamics + 0x68);
                    const float z = Memory::ReadFloat32(dynamics + 0x70);
                    const unsigned character = Memory::Read32(data + i * 0xF0 + 0x34);
                    const unsigned rank = Memory::Read8(info + 0x20);
                    if (!std::isfinite(x) || !std::isfinite(z) || std::abs(x) > 1000000
                            || std::abs(z) > 1000000 || character >= 48 || rank > 12) continue;
                    const size_t at = kHeader + i * kStride;
                    next[at] = 1;
                    next[at + 1] = static_cast<float>(character);
                    next[at + 2] = static_cast<float>(rank);
                    next[at + 3] = Memory::Read16(info + 0x24);
                    next[at + 4] = x;
                    next[at + 5] = z;
                    // Direction in the map plane, taken from the kart's forward axis.
                    next[at + 6] = std::atan2(Memory::ReadFloat32(physics + 0xE4),
                                             -Memory::ReadFloat32(physics + 0xEC));
                    next[at + 7] = static_cast<float>(HeldItemIcon(i));
                }
            }
        }
    } catch (const Memory::AccessViolation&) {
        next = EmptySnapshot();
    }
    std::lock_guard lock(g_mapMutex);
    g_snapshot = next;
    g_snapshotCourseIdentity = next[1] != 0 && RuntimeProduct::IsRetroRewind() ? g_loadedCourseIdentity : "";
    g_updated = std::chrono::steady_clock::now();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_driftdroid_android_dualscreen_RaceMapBridge_nativeCourseIdentity(JNIEnv* env, jclass) {
    // Same calling thread as nativeRead: never pair another publication's identity with its floats.
    return env->NewStringUTF(g_readCourseIdentity.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_dualscreen_RaceMapBridge_nativeEnable(JNIEnv*, jclass, jboolean enabled) {
    g_enabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_com_driftdroid_android_dualscreen_RaceMapBridge_nativeMapVisible(JNIEnv*, jclass, jboolean visible) {
    g_visible.store(visible == JNI_TRUE, std::memory_order_relaxed);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_driftdroid_android_dualscreen_RaceMapBridge_nativeRead(JNIEnv* env, jclass, jfloatArray output) {
    if (!output || env->GetArrayLength(output) != static_cast<jsize>(g_snapshot.size())) return JNI_FALSE;
    Snapshot copy;
    {
        std::lock_guard lock(g_mapMutex);
        copy = g_snapshot;
        if (std::chrono::steady_clock::now() - g_updated > std::chrono::seconds(2)) copy[1] = 0;
        g_readCourseIdentity = copy[1] != 0 ? g_snapshotCourseIdentity : "";
    }
    env->SetFloatArrayRegion(output, 0, static_cast<jsize>(copy.size()), copy.data());
    return copy[1] != 0 ? JNI_TRUE : JNI_FALSE;
}
#endif

extern "C" void func_8063DB84(CpuContext* ctx);
extern "C" void ThorLayoutDraw_8063db84(CpuContext* ctx) {
#if defined(__ANDROID__)
    // Hide the upper map and timer only while the lower race map is visible.
    // Lap, items, menus, and other LayoutUIControls retain their original draw.
    if (g_enabled.load(std::memory_order_relaxed) && g_visible.load(std::memory_order_relaxed)
            && Memory::Contains(ctx->gpr[3], 4)) {
        const uint32_t vtable = Memory::Read32(ctx->gpr[3]);
        if (vtable == 0x808D3960u || vtable == 0x808D4010u) return;
    }
#endif
    func_8063DB84(ctx);
}
// Native winner discovery is platform-independent. Keep the wrapper registered
// on other platforms too, where it simply calls the original translated body.
REGISTER_NATIVE_FUNCTION_AS(0x8063DB84, ThorLayoutDraw_8063db84, "ThorLayoutDraw_8063db84");
