#include "TouchPointerMailbox.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

int main() {
    mobile_thor::TouchPointerMailbox mailbox;
    float x = 99, y = 99;
    int checks = 0;
    assert(!mailbox.read(x, y)); ++checks;
    for (int i = -1000; i <= 1000; ++i) {
        const float t = i / 1000.f;
        mailbox.set(t, -t, true);
        assert(mailbox.read(x, y));
        assert(std::fabs(x - t) < .00004f && std::fabs(y + t) < .00004f);
        checks += 2;
    }
    mailbox.set(10, -10, true);
    assert(mailbox.read(x, y) && x == 1 && y == -1); ++checks;
    mailbox.set(0, 0, false); assert(!mailbox.read(x, y)); ++checks;
    mailbox.set(std::numeric_limits<float>::quiet_NaN(), 0, true);
    assert(!mailbox.read(x, y)); ++checks;
    mailbox.set(0, std::numeric_limits<float>::infinity(), true);
    assert(!mailbox.read(x, y)); ++checks;
    mailbox.set(1, 1, true); mailbox.clear(); assert(!mailbox.read(x, y)); ++checks;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i) {
            const float t = (i % 2001 - 1000) / 1000.f;
            mailbox.set(t, -t, true);
        }
        done.store(true);
    });
    do {
        if (mailbox.read(x, y)) assert(std::fabs(x + y) < .00004f);
    } while (!done.load());
    writer.join(); ++checks;
    std::printf("PASS %d mailbox checks and 100000 concurrent coordinate writes\n", checks);
}
