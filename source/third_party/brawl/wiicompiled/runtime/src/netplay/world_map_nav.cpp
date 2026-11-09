#include "netplay/world_map_nav.h"

#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace WorldMapNav {
namespace {

// daWmMap_c::m_instance and its active dWmConnect_c (one per sub-map, picked by +0x338C): 192
// dPnt_c of 0x54 bytes from +4, each {+0 vtable, +4 name, +0xC pos, +0x18 six neighbours by
// Direction3D_e (up, down, front, back, left, right: dWmConnect_c::__connect), +0x4C its index}.
// Names: "W501" courses, "W5S0" start, "W5T0" tower, "W5K0" Toad house...; "F"/"K" points only
// shape the paths between them.
constexpr uint32_t kMapInstance = 0x8042A18Cu;
constexpr uint32_t kMapSubIndex = 0x338C;
constexpr uint32_t kMapConnect = 0x5C7F4;
constexpr uint32_t kConnectSize = 0x3F08;
constexpr int kPointCount = 192;
constexpr uint32_t kPointSize = 0x54;
constexpr uint32_t kPointName = 0x4, kPointPos = 0xC, kPointLinks = 0x18;
// daWmPlayer_c::ms_instance; its position (dBaseActor_c::mPos) at +0xAC.
constexpr uint32_t kPlayerInstance = 0x8042A1A0u;
constexpr uint32_t kActorPos = 0xAC;
// m3d::internal::l_scnRoot_p: nw4r::g3d::ScnRoot, camera 0's CameraData at +0xF8 (the camera the
// map is drawn with and dWmLib projects with): +0 camera matrix (3x4), +0x30 projection (4x4),
// +0x70 flags (0x80: projection computed), +0xAC fovy, +0xB0 aspect.
constexpr uint32_t kScnRoot = 0x8042A3CCu;
constexpr uint32_t kCameraData = 0xF8;

// The remote held sideways: the screen's directions on the Wii Remote's D-pad.
constexpr uint32_t kScreenLeft = 0x0008, kScreenRight = 0x0004, kScreenUp = 0x0002, kScreenDown = 0x0001;

bool GuestPtr(uint32_t p) { return p >= 0x80000000u && p < 0x81800000u; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Point {
    bool used = false;
    char name[6] = {};
    Vec3 pos;
    int links[6] = {-1, -1, -1, -1, -1, -1};
};

uint32_t ConnectBase() {
    const uint32_t map = Memory::Read32(kMapInstance);
    if (!GuestPtr(map)) return 0;
    const uint32_t sub = Memory::Read32(map + kMapSubIndex);
    if (sub > 3) return 0;
    return map + kMapConnect + sub * kConnectSize;
}

Vec3 ReadVec(uint32_t a) {
    return Vec3{Memory::ReadFloat32(a), Memory::ReadFloat32(a + 4), Memory::ReadFloat32(a + 8)};
}

std::vector<Point> ReadPoints() {
    std::vector<Point> points;
    const uint32_t base = ConnectBase();
    if (base == 0) return points;
    const uint32_t first = base + 4;
    points.resize(kPointCount);
    for (int i = 0; i < kPointCount; ++i) {
        const uint32_t p = first + i * kPointSize;
        Point& pt = points[i];
        for (int c = 0; c < 5; ++c) pt.name[c] = static_cast<char>(Memory::Read8(p + kPointName + c));
        pt.used = pt.name[0] != 0;
        if (!pt.used) continue;
        pt.pos = ReadVec(p + kPointPos);
        for (int d = 0; d < 6; ++d) {
            const uint32_t link = Memory::Read32(p + kPointLinks + d * 4);
            if (link >= first && link < first + kPointCount * kPointSize && (link - first) % kPointSize == 0) {
                pt.links[d] = static_cast<int>((link - first) / kPointSize);
            }
        }
    }
    return points;
}

bool PlayerPos(Vec3& pos) {
    const uint32_t player = Memory::Read32(kPlayerInstance);
    if (!GuestPtr(player)) return false;
    pos = ReadVec(player + kActorPos);
    return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z);
}

// The point the player is on (within a few units), or -1 between points.
int PointAt(const std::vector<Point>& points, const Vec3& pos) {
    int best = -1;
    float bestD = 6.0f * 6.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        if (!points[i].used) continue;
        const float dx = points[i].pos.x - pos.x, dz = points[i].pos.z - pos.z;
        const float d = dx * dx + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

// Where camera 0 puts a world position, in the overlay's layout units (y up, 456 tall, centred).
// The picture keeps its height on any screen and widens (Hor+), so x scales with the camera's own
// aspect: guest NDC x spans 228 * aspect units either way.
bool Project(const Vec3& p, float& lx, float& ly) {
    const uint32_t root = Memory::Read32(kScnRoot);
    if (!GuestPtr(root)) return false;
    const uint32_t cam = root + kCameraData;
    float m[12];
    for (int i = 0; i < 12; ++i) m[i] = Memory::ReadFloat32(cam + i * 4);
    const float vx = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    const float vy = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
    const float vz = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
    float P[16];
    if ((Memory::Read32(cam + 0x70) & 0x80) != 0) {
        for (int i = 0; i < 16; ++i) P[i] = Memory::ReadFloat32(cam + 0x30 + i * 4);
    } else {
        const float fovy = Memory::ReadFloat32(cam + 0xAC), aspect = Memory::ReadFloat32(cam + 0xB0);
        const float t = std::tan(fovy * 0.5f * 3.14159265f / 180.0f);
        if (!(t > 0.0f) || !(aspect > 0.0f)) return false;
        std::memset(P, 0, sizeof(P));
        P[5] = 1.0f / t;
        P[0] = P[5] / aspect;
        P[14] = -1.0f;
    }
    const float cx = P[0] * vx + P[1] * vy + P[2] * vz + P[3];
    const float cy = P[4] * vx + P[5] * vy + P[6] * vz + P[7];
    const float cw = P[12] * vx + P[13] * vy + P[14] * vz + P[15];
    if (!(std::fabs(cw) > 1e-5f) || P[0] == 0.0f) return false;
    const float aspect = P[5] / P[0];
    lx = cx / cw * 228.0f * aspect;
    ly = cy / cw * 228.0f;
    return std::isfinite(lx) && std::isfinite(ly);
}

// A place on the map: a course, the start, a tower, castle, house... (the points named "W...").
bool StopPoint(const Point& p) { return p.name[0] == 'W'; }

// Next point from `from` towards `to` (breadth first over the map's links, skipping `blocked`).
int FirstHop(const std::vector<Point>& points, int from, int to, const std::set<std::pair<int, int>>& blocked) {
    std::vector<int> prev(points.size(), -2);
    std::vector<int> queue{from};
    prev[from] = -1;
    for (size_t q = 0; q < queue.size(); ++q) {
        const int cur = queue[q];
        if (cur == to) break;
        for (int n : points[cur].links) {
            if (n < 0 || prev[n] != -2 || blocked.count({cur, n})) continue;
            prev[n] = cur;
            queue.push_back(n);
        }
    }
    if (prev[to] == -2) return -1;
    int step = to;
    while (prev[step] != from) step = prev[step];
    return step;
}

}  // namespace

// The walk, run from the reads of channel 1 (the game's thread). Taps arrive from the UI thread.
namespace {
std::atomic<int> g_request{-1};   // target point of a new tap
std::atomic<bool> g_cancel{false};
struct Walk {
    bool active = false;
    int target = -1;
    int reads = 0;
    int still = 0;            // reads the player has stood still
    Vec3 last;
    int pressFrom = -1, pressTo = -1, tries = 0;
    uint32_t button = 0;
    int held = 0;
    std::set<std::pair<int, int>> blocked;
};
Walk g_walk;

}  // namespace

int PointAtTap(float lx, float ly) {
    const std::vector<Point> points = ReadPoints();
    if (points.empty()) return -1;
    int best = -1;
    float bestD = 30.0f * 30.0f;
    for (int i = 0; i < kPointCount; ++i) {
        if (!points[i].used || !StopPoint(points[i])) continue;
        float px = 0, py = 0;
        if (!Project(points[i].pos, px, py)) continue;
        const float d = (px - lx) * (px - lx) + (py - ly) * (py - ly);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    if (best >= 0) {
        RT_LOGF(RT_TAG_RUNTIME, "map tap: point %d %s\n", best, points[best].name);
    }
    return best;
}

void WalkTo(int point) { g_request.store(point, std::memory_order_release); }

bool PlayerOn(int point) {
    const std::vector<Point> points = ReadPoints();
    Vec3 pos;
    return !points.empty() && PlayerPos(pos) && PointAt(points, pos) == point;
}

void Cancel() { g_cancel.store(true, std::memory_order_release); }

uint32_t Buttons(bool& active) {
    Walk& w = g_walk;
    if (g_cancel.exchange(false)) {
        w = Walk{};
    }
    const int request = g_request.exchange(-1);
    if (request >= 0) {
        w = Walk{};
        w.active = true;
        w.target = request;
    }
    active = w.active;
    if (!w.active) return 0;
    if (w.held > 0) {  // a press: 4 reads down, then 4 up
        --w.held;
        return w.held >= 4 ? w.button : 0;
    }
    if (++w.reads > 60 * 60) {
        w = Walk{};
        active = false;
        return 0;
    }
    const std::vector<Point> points = ReadPoints();
    Vec3 pos;
    if (points.empty() || !PlayerPos(pos) || w.target >= static_cast<int>(points.size())) {
        w = Walk{};
        active = false;
        return 0;
    }
    const float moved = std::fabs(pos.x - w.last.x) + std::fabs(pos.y - w.last.y) + std::fabs(pos.z - w.last.z);
    w.last = pos;
    w.still = moved < 0.01f ? w.still + 1 : 0;
    if (w.still < 12) return 0;  // walking, or just arrived: let the game settle
    const int at = PointAt(points, pos);
    if (at < 0) return 0;
    if (at == w.target) {
        RT_LOGF(RT_TAG_RUNTIME, "map walk: arrived at %s\n", points[at].name);
        w = Walk{};
        active = false;
        return 0;
    }
    if (at == w.pressFrom) {
        // Still where the last press started: that way is shut (a locked path) or the game was busy.
        if (++w.tries >= 3) {
            w.blocked.insert({w.pressFrom, w.pressTo});
            w.tries = 0;
        }
    } else {
        w.tries = 0;
    }
    const int hop = FirstHop(points, at, w.target, w.blocked);
    if (hop < 0) {
        RT_LOGF(RT_TAG_RUNTIME, "map walk: no open way from %s to %s\n", points[at].name, points[w.target].name);
        w = Walk{};
        active = false;
        return 0;
    }
    float ax = 0, ay = 0, bx = 0, by = 0;
    if (!Project(points[at].pos, ax, ay) || !Project(points[hop].pos, bx, by)) {
        w = Walk{};
        active = false;
        return 0;
    }
    const float dx = bx - ax, dy = by - ay;
    w.button = std::fabs(dx) >= std::fabs(dy) ? (dx > 0 ? kScreenRight : kScreenLeft) : (dy > 0 ? kScreenUp : kScreenDown);
    w.pressFrom = at;
    w.pressTo = hop;
    w.held = 8;
    w.still = 0;
    RT_LOGF(RT_TAG_RUNTIME, "map walk: %s -> %s (towards %s), button 0x%X\n", points[at].name, points[hop].name,
            points[w.target].name, w.button);
    --w.held;
    return w.button;
}

void Dump() {
    const std::vector<Point> points = ReadPoints();
    Vec3 pos;
    const bool havePlayer = PlayerPos(pos);
    float plx = 0, ply = 0;
    if (havePlayer) Project(pos, plx, ply);
    RT_LOGF(RT_TAG_RUNTIME, "wmap: connect 0x%08X player (%.1f %.1f %.1f) at point %d, screen (%.0f %.0f)\n",
            ConnectBase(), pos.x, pos.y, pos.z, havePlayer ? PointAt(points, pos) : -1, plx, ply);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const Point& p = points[i];
        if (!p.used) continue;
        float lx = 0, ly = 0;
        Project(p.pos, lx, ly);
        RT_LOGF(RT_TAG_RUNTIME, "wmap: %3d %-5s (%7.1f %6.1f %7.1f) screen (%4.0f %4.0f) links %d %d %d %d %d %d%s\n",
                i, p.name, p.pos.x, p.pos.y, p.pos.z, lx, ly, p.links[0], p.links[1], p.links[2],
                p.links[3], p.links[4], p.links[5], StopPoint(p) ? " stop" : "");
    }
}

}  // namespace WorldMapNav
