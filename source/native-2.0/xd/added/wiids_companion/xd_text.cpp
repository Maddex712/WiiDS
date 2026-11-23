#include "xd_text.h"
#include <map>
#include <mutex>
#include <string>
extern "C" const uint16_t* GSmsgGetGSchar(uint32_t);
namespace {
std::mutex lock;
std::map<uint32_t,std::u16string> messages;
std::map<uint64_t,std::u16string> rows;
thread_local uint64_t row=UINT64_MAX;
thread_local bool clearRow=false;
template<class Map> void bound(Map& map){if(map.size()>2048)map.erase(map.begin());}
int copy(const std::u16string& from,uint16_t* out,int capacity){
    int n=0;for(char16_t c:from){if(n>=capacity-1)break;out[n++]=c;}out[n]=0;return n!=0;
}
}
extern "C" void WiidsXD_TextBegin(uint32_t key){
    std::lock_guard<std::mutex> hold(lock);messages[key].clear();bound(messages);
    if(row!=UINT64_MAX){if(clearRow){rows[row].clear();clearRow=false;}
        else if(!rows[row].empty()&&rows[row].size()<95)rows[row]+=u' ';}
}
extern "C" void WiidsXD_TextGlyph(uint32_t key,uint16_t c){
    if(c<32||c==0xffff)return;
    std::lock_guard<std::mutex> hold(lock);
    if(messages[key].size()<191)messages[key]+=(char16_t)c;
    if(row!=UINT64_MAX&&rows[row].size()<95)rows[row]+=(char16_t)c;
}
extern "C" uint64_t WiidsXD_TextScopeBegin(int menu,int item){
    auto old=row;row=(uint64_t(uint32_t(menu))<<32)|uint32_t(item);
    clearRow=true;std::lock_guard<std::mutex> hold(lock);bound(rows);return old;
}
extern "C" void WiidsXD_TextScopeEnd(uint64_t old){row=old;}
extern "C" int WiidsXD_CopyRowLabel(int menu,int item,uint16_t* out,int capacity){
    if(!out||capacity<1)return 0;
    std::lock_guard<std::mutex> hold(lock);auto it=rows.find((uint64_t(uint32_t(menu))<<32)|uint32_t(item));
    if(it==rows.end()||it->second.empty()){out[0]=0;return 0;}return copy(it->second,out,capacity);
}
extern "C" int WiidsXD_CopyMessageLabel(uint32_t key,uint16_t* out,int capacity){
    if(!out||capacity<1)return 0;out[0]=0;
    {std::lock_guard<std::mutex> hold(lock);auto it=messages.find(key);if(it!=messages.end()&&!it->second.empty())return copy(it->second,out,capacity);}
    // Unformatted native text can be copied before its first draw. Packed font/
    // substitution controls are decoded only by the original renderer, observed above.
    const uint16_t* raw=GSmsgGetGSchar(key);if(!raw)return 0;
    int n=0;while(n<capacity-1&&raw[n]&&raw[n]!=0xffff){out[n]=raw[n];n++;}out[n]=0;return n!=0;
}
