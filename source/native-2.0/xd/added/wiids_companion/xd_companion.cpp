#include "xd_companion.h"
#include "xd_chain.h"
#include <chrono>
#include <cstring>
#include <mutex>

namespace {
std::mutex lock;
XdSnapshot published{};
XdRequest request{};
bool pending=false;
bool blocked=false;
uint64_t epoch=1, sequence=0, last_context=0;
uintptr_t last_window=0;
constexpr uint64_t max_age_ms=250;
uint64_t context(const XdSnapshot& s) {
    // Hash only admission context, not timestamps or cosmetic/state values.
    uint64_t h=1469598103934665603ULL;
    auto add=[&](uint64_t v){ h^=v; h*=1099511628211ULL; };
    add(s.available);add(s.menu_id);add(s.phase);add(s.battle);add(s.actor);add(s.target);
    add(s.map_id);add(s.choice_count);
    for(uint32_t i=0;i<s.choice_count;i++) {
        auto& c=s.choices[i];add(c.action);add(c.argument);add(c.item_id);add(c.cursor);add(c.enabled);
    }
    return h;
}
bool fresh() {
    uint64_t now=WiidsXD_Now();
    return !blocked&&published.available&&published.captured_ms<=now&&now-published.captured_ms<=max_age_ms;
}
bool allowed(int action,int argument) {
    for(uint32_t i=0;i<published.choice_count;i++) {
        const auto& c=published.choices[i];
        if(c.action==action&&c.argument==argument&&c.enabled)return true;
    }
    return false;
}
}
extern "C" uint64_t WiidsXD_Now(void) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
extern "C" void WiidsXD_Publish(XdSnapshot* frame,uintptr_t cookie,int physical) {
    std::lock_guard<std::mutex> guard(lock);
    if(!frame||frame->choice_count>XD_MAX_CHOICES||frame->shadow_count>XD_MAX_SHADOW) {
        published={};pending=false;++epoch;last_context=0;last_window=0;return;
    }
    frame->version=2;
    uint64_t key=context(*frame);
    if(key!=last_context||cookie!=last_window||physical) {
        ++epoch;pending=false;last_context=key;last_window=cookie;
    }
    frame->epoch=epoch;frame->sequence=++sequence;frame->captured_ms=WiidsXD_Now();
    published=*frame;
}
extern "C" int WiidsXD_Read(XdSnapshot* out) {
    if(!out)return 0;
    std::lock_guard<std::mutex> guard(lock);
    *out=published;
    if(!fresh()) {out->available=0;for(auto& c:out->choices)c.enabled=0;return 0;}
    return 1;
}
extern "C" int WiidsXD_Submit(uint64_t token,int action,int argument) {
    std::lock_guard<std::mutex> guard(lock);
    if(pending||!fresh()||token!=epoch||!allowed(action,argument))return 0;
    request={token,action,argument};pending=true;return 1;
}
extern "C" int WiidsXD_Take(XdRequest* out) {
    if(!out)return 0;
    std::lock_guard<std::mutex> guard(lock);
    if(!pending)return 0;
    pending=false;
    if(!fresh()||request.epoch!=epoch||!allowed(request.action,request.argument))return 0;
    *out=request;
    // Consume the token too: repeated taps on a still-visible menu cannot issue a second action.
    ++epoch;published.epoch=epoch;
    return 1;
}
extern "C" void WiidsXD_Suspend(void) {
    WiidsXD_ChainCancel();
    std::lock_guard<std::mutex> guard(lock);
    pending=false;published={};++epoch;last_context=0;last_window=0;
}
extern "C" void WiidsXD_BlockInput(int value) {
    WiidsXD_ChainCancel();
    std::lock_guard<std::mutex> guard(lock);
    blocked=value!=0;pending=false;published={};++epoch;last_context=0;last_window=0;
}
extern "C" int WiidsXD_InputBlocked(void){std::lock_guard<std::mutex> guard(lock);return blocked?1:0;}
