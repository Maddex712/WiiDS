#include "../performance_session.h"
#include <cassert>
#include <cstdio>

namespace {
int creates, closes, updates, reports, queries, failReport, failTarget, efficiencyRequests, failEfficiency;
bool supported = true, accepts = true;
int32_t lastTids[2]; size_t lastCount;
int64_t lastTarget, lastWork;
APerformanceHintManager* manager() {
    ++queries;
    return supported ? reinterpret_cast<APerformanceHintManager*>(1) : nullptr;
}
APerformanceHintSession* create(APerformanceHintManager*, const int32_t* tids, size_t count, int64_t target) {
    ++creates; lastCount = count; lastTarget = target;
    for (size_t i = 0; i < count; ++i) lastTids[i] = tids[i];
    return accepts ? reinterpret_cast<APerformanceHintSession*>(2) : nullptr;
}
int target(APerformanceHintSession*, int64_t ns) { ++updates; lastTarget = ns; return failTarget; }
int report(APerformanceHintSession*, int64_t ns) { ++reports; lastWork = ns; return failReport; }
void close(APerformanceHintSession*) { ++closes; }
int powerEfficiency(APerformanceHintSession*, bool enabled) { assert(!enabled); ++efficiencyRequests; return failEfficiency; }
port_android::HintApi api{manager, create, target, report, close};
}
int main() {
    assert(port_android::workTarget(16683333, true) == 13346667);
    assert(port_android::workTarget(33366666, true) == 26693333);
    assert(port_android::workTarget(16683333, false) == 16683333);
    {
        port_android::HintSession hints(api);
        assert(!hints.configure(0, 22, 16666667));
        assert(!hints.configure(11, 22, 0));
        assert(creates == 0);
        assert(hints.configure(11, 22, 16666667));
        assert(hints.powerEfficiencyStatus() == -1);
        assert(lastCount == 2 && lastTids[0] == 11 && lastTids[1] == 22);
        assert(lastTarget == 16666667);
        assert(hints.configure(11, 22, 16666667));
        assert(creates == 1 && updates == 0);
        assert(!hints.report(0) && reports == 0);
        assert(hints.report(18000000) && reports == 1 && lastWork == 18000000);
        assert(hints.configure(11, 22, 33333333));
        assert(updates == 1 && lastTarget == 33333333);
        assert(hints.configure(33, 22, 16666667));
        assert(closes == 1 && creates == 2 && lastTids[0] == 33);
        hints.close();
        assert(closes == 2 && !hints.report(18000000));
        assert(hints.configure(33, 0, 16666667) && lastCount == 1);
        assert(hints.configure(33, 33, 16666667) && lastCount == 1);
        failReport = 32;
        assert(!hints.report(18000000));
        int calls = creates + reports + updates;
        assert(!hints.configure(33, 33, 16666667));
        assert(!hints.report(18000000));
        assert(creates + reports + updates == calls);
    }
    failReport = 0;
    {
        auto modern = api; modern.powerEfficiency = powerEfficiency;
        port_android::HintSession hints(modern);
        assert(hints.configure(11, 22, 13346667));
        assert(efficiencyRequests == 1 && hints.powerEfficiencyStatus() == 0);
        assert(hints.configure(11, 22, 13346667) && efficiencyRequests == 1);
        hints.close();
        assert(hints.configure(11, 22, 13346667) && efficiencyRequests == 2);
        failEfficiency = 95;
        hints.close();
        assert(hints.configure(11, 22, 13346667));
        assert(hints.powerEfficiencyStatus() == 95);
        assert(hints.report(18000000)); // optional rejection preserves basic hints
    }
    supported = false;
    {
        port_android::HintSession hints(api);
        int before = queries;
        assert(!hints.configure(11, 22, 16666667));
        assert(!hints.configure(11, 22, 16666667));
        assert(queries == before + 1);
    }
    supported = true; accepts = false;
    {
        port_android::HintSession hints(api);
        int before = creates;
        assert(!hints.configure(11, 22, 16666667));
        assert(!hints.configure(11, 22, 16666667));
        assert(creates == before + 1);
    }
    accepts = true; failTarget = 32;
    {
        port_android::HintSession hints(api);
        assert(hints.configure(11, 22, 16666667));
        int before = closes;
        assert(!hints.configure(11, 22, 33333333));
        assert(closes == before + 1);
        assert(!hints.configure(11, 22, 16666667));
    }
    puts("Performance hint headroom, thread groups, budgets, pause/resume and optional/service failures passed");
}
