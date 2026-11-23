#pragma once
#include <deque>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

// Recording-thread cache only. Retain keys, never iterators, across insertions,
// rehashes or external invalidations. A reinserted key shares its pending visit.
template<class K,class V> class XdSweepMap : public absl::flat_hash_map<K,V> {
    using Base=absl::flat_hash_map<K,V>;
    std::deque<K> pending;
    absl::flat_hash_set<K> queued;
    size_t quota=0,phase=0;
public:
    V& operator[](const K& key) {
        if(queued.insert(key).second)pending.push_back(key);
        return Base::operator[](key);
    }
    void clear(){Base::clear();pending.clear();queued.clear();quota=phase=0;}
    template<class Visit> void sweep(Visit visit) {
        if(phase==0)quota=(pending.size()+63)/64;
        phase=(phase+1)%64;
        for(size_t n=0;n<quota&&!pending.empty();++n) {
            const K key=pending.front();pending.pop_front();queued.erase(key);
            auto it=Base::find(key);
            if(it==Base::end())continue;
            visit(it);
            if(Base::contains(key)&&queued.insert(key).second)pending.push_back(key);
        }
    }
};
