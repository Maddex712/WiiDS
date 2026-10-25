#pragma once
#include <array>
#include <cstdint>
#include <vector>

// Pure observer: no guest writes, no unlock inferred from loading saved progress.
namespace ThorAchievements {
struct Sample {
    bool eligible = false, inCourse = false;
    uint32_t header = 0;
    int slot = -1, world = -1, course = -1, stars = -1, flags = -1;
};
using Event = std::array<int32_t, 6>; // seq, type (1 pickup, 2 new exit), slot, world, course, bits
class Observer {
    Sample previous{};
    int sequence = 0;
    std::vector<Event> events;
public:
    void Capture(const Sample& s) {
        if (s.eligible && (s.header == 0 || s.slot < 0 || s.slot > 2 || s.world < 0 || s.world > 8 ||
            s.course < 0 || s.course >= 38 || s.stars < -1 || s.stars > 7 || s.flags < -1)) {
            previous = {}; return;
        }
        bool same = s.eligible && previous.eligible && s.header == previous.header &&
            s.slot == previous.slot && s.world == previous.world && s.course == previous.course;
        auto emit = [&](int type, int bits) {
            // Keep a bounded queue. Sequence gaps let the consumer detect an overrun.
            if (events.size() == 256) events.erase(events.begin());
            events.push_back({++sequence, type, s.slot, s.world, s.course, bits});
        };
        if (same) {
            if (s.inCourse && previous.inCourse && s.stars >= 0 && previous.stars >= 0 &&
                (s.stars & ~previous.stars) != 0) emit(1, s.stars);
            // dMj2dGame::isCourseDataFlag uses 0x30 for completed exits (800B4DE0).
            if (s.flags >= 0 && previous.flags >= 0 && ((s.flags & ~previous.flags) & 0x30))
                emit(2, (s.flags & ~previous.flags) & 0x30);
        }
        previous = s;
    }
    std::vector<Event> After(int cursor) const {
        std::vector<Event> result;
        for (const auto& event : events) if (event[0] > cursor) result.push_back(event);
        return result;
    }
};
}
