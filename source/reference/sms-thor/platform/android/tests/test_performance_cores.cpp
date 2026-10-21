#include "../performance_cores.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using port_android::CpuWeight;
    using port_android::performanceCores;
    // Representative tri-tier topology, with a prime and multiple big cores.
    std::vector<CpuWeight> cpus{{0,280},{1,280},{2,820},{3,820},{4,850},{5,850},{6,850},{7,1024}};
    assert((performanceCores(cpus) == std::vector<int>{2,3,4,5,6,7}));
    // CPU IDs are not necessarily contiguous, or ordered by capability.
    assert((performanceCores({{12,900},{2,300},{27,1024},{5,800}}) == std::vector<int>{12,27,5}));
    assert(performanceCores({}).empty());
    assert(performanceCores({{0,0},{1,1000}}).empty()); // unreadable metadata
    assert(performanceCores({{0,1000},{1,1000},{2,1000}}).empty()); // homogeneous
    assert(performanceCores({{0,300},{1,300},{2,1024}}).empty()); // lone prime
    assert(performanceCores({{2,850},{7,1024}}).empty()); // already fast-only
    assert(performanceCores({{2,1024}}).empty()); // one permitted CPU
    assert((performanceCores({{0,100},{1,750},{2,1000}}) == std::vector<int>{1,2}));
    const auto max = std::numeric_limits<uint32_t>::max();
    assert((performanceCores({{0,1},{1,max},{2,max}}) == std::vector<int>{1,2}));
    puts("Performance-core tiers, sparse IDs, missing metadata, narrow masks and overflow checks passed");
}
