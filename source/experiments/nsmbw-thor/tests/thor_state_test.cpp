// Link this against the actual runtime/src/thor_state.cpp, with tests/mock first
// on the include path. No guest code or assets are part of this test.
#include "memory.h"
#include "thor_state.h"

#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <unordered_map>

namespace {
std::unordered_map<uint32_t, uint8_t> bytes;
int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        std::exit(1);
    }
}
void Word(uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < 4; ++i) bytes[address+i] = static_cast<uint8_t>(value >> (24-i*8));
}
void Fixture(uint32_t mode = 0) {
    bytes.clear();
    Word(0x80428730u, 0xABCD0005u);
    Word(0x80315B98u, mode);
    Word(0x80315B9Cu, 0x02040506u); // shown 2-4, loaded 5-6
    Word(0x80354E60u, 2); // player 1 is character 2
    Word(0x80354E98u, 99);
    Word(0x80354EA8u, 42);
    Word(0x8042A070u, 0x80010000u);
    Word(0x80010004u, 300u * 4096u - 92u);
    Word(0x80429F48u, 0);
}
ThorState::Snapshot Capture() { ThorState::Capture(); return ThorState::Read(); }
void Float(uint32_t address, float value) { Word(address,std::bit_cast<uint32_t>(value)); }
void ExtrasFixture() {
    Fixture();
    Word(0x803741B0u,4); Word(0x803741B4u,2); Word(0x803741B8u,4);
    Word(0x80429CC0u,123450);
    Word(0x8042A1C8u,0x80020000u);
    Word(0x8002120Cu,0x02040107u); // loaded world2/course4, file1, area7
    Word(0x8042A040u,0x80030000u);
    Word(0x80030024u,0x00000100u); // selected save 1 at byte38
    Word(0x80030000u+2432+1728+2*168+4*4+108,5);
    Word(0x80429E98u,0x80040000u);
    Word(0x80040000u+944+44,0x80050000u);
    Word(0x80040000u+944+156,1);
    Word(0x80050000u,(100u<<16)|200u);
    Word(0x80050004u,(2000u<<16)|500u);
    Word(0x8005000Cu,7u<<24);
    Word(0x80354E10u,17);
    Word(0x80377A78u,0x80060000u);
    Word(0x80060004u,0);
    Word(0x80060008u,0x80070000u);
    Word(0x80070000u,17);
    Float(0x800700ACu,600); Float(0x800700B0u,-400);
}
}

bool Memory::TryRead32(uint32_t address, uint32_t& value) noexcept {
    value=0;
    for (uint32_t i=0;i<4;++i) {
        auto found=bytes.find(address+i);
        if (found==bytes.end()) return false;
        value=(value<<8)|found->second;
    }
    return true;
}
bool Memory::Contains(uint32_t address, size_t length) {
    return (address>=0x80000000u && static_cast<uint64_t>(address)+length<=0x81800000ull) ||
        (address>=Memory::kMem2CachedBase && static_cast<uint64_t>(address)+length<=Memory::kMem2CachedEnd);
}

int main() {
#ifdef _WIN32
    _putenv_s("NSMBW_THOR", "1");
#else
    setenv("NSMBW_THOR", "1", 1);
#endif
    Check(ThorState::Read()[0]==0,"unpublished schema is unavailable");
    Fixture(); auto s=Capture();
    Check(s[0]==2 && s[2]==31,"schema two preserves original five field groups");
    Check(s[3]==5 && s[4]==0,"scene low word and normal game mode");
    Check(s[5]==2 && s[6]==4,"big-endian shown world/course, not internally loaded pair");
    Check(s[7]==2 && s[8]==99 && s[9]==42,"character-indexed lives and coins");
    Check(s[10]==300,"timer uses original ceiling: first decrement still displays 300");
    Check(s[11]==0,"zero stop word is running");
    const int32_t prior=s[1];
    s[8]=-100;
    Check(ThorState::Read()[8]==99,"read returns immutable copy");
    for (uint32_t mode : {2u,3u,4u}) {
        Fixture(mode); s=Capture();
        Check(s[2]==1 && s[5]==-1 && s[7]==-1 && s[10]==-1,"title/replay/unsupported mode clears stale course data");
        Check(s[4]==(mode<4 ? static_cast<int32_t>(mode) : -1),"known title/replay mode is retained, unsupported mode unavailable");
    }
    Fixture(1); s=Capture(); Check(s[2]==31 && s[4]==1,"super-guide mode is available");
    Check(s[1]>prior,"publication sequence advances");
    Fixture(); Word(0x80315B9Cu,0x09000001u); s=Capture();
    Check((s[2]&2)==0 && s[5]==-1 && s[6]==-1,"out-of-range world is unavailable");
    Fixture(); Word(0x80315B9Cu,0x0817FF01u); s=Capture();
    Check((s[2]&2)!=0 && s[5]==8 && s[6]==23,"shown special course is retained independently of loaded world");
    Fixture(); Word(0x80428730u,3); s=Capture();
    Check(s[2]==5 && s[3]==3 && s[4]==-1 && s[8]==99 && s[10]==-1,"world map retains lives and clears course-only fields");
    Fixture(); bytes.erase(0x80315B98u); s=Capture();
    Check(s[2]==1 && s[4]==-1 && s[5]==-1,"failed mode read cannot expose course fields");
    Fixture(); Word(0x80354E60u,4); s=Capture();
    Check((s[2]&4)==0 && s[7]==-1,"invalid character does not index arrays");
    Fixture(); Word(0x80354EA8u,100); s=Capture(); Check((s[2]&4)==0,"invalid coin count is unavailable");
    Fixture(); bytes.erase(0x80354E98u); s=Capture(); Check((s[2]&4)==0,"failed lives read clears player group");
    for (uint32_t pointer : {0u,0x80010001u,0x817FFFFCu,0x90010000u}) {
        Fixture(); Word(0x8042A070u,pointer); s=Capture();
        Check((s[2]&8)==0 && s[10]==-1,"invalid timer pointers are unavailable");
    }
    for (uint32_t value : {0u,1u,4096u,4097u,999u*4096u}) {
        Fixture(); Word(0x80010004u,value); s=Capture();
        Check((s[2]&8)!=0 && s[10]==static_cast<int32_t>((value+4095u)>>12),"Q12 timer boundary matches game display");
    }
    Fixture(); Word(0x80010004u,0xFFFFFFFFu); s=Capture(); Check((s[2]&8)==0,"negative timer is unavailable");
    for (uint32_t stop : {1u,4u,5u}) {
        Fixture(); Word(0x80429F48u,stop); s=Capture(); Check((s[2]&16)!=0 && s[11]==1,"pause flags one or four set paused");
    }
    Fixture(); Word(0x80429F48u,2); s=Capture(); Check((s[2]&16)!=0 && s[11]==0,"unrelated stop bit does not masquerade as pause");
    Fixture(); bytes.erase(0x80429F48u); s=Capture(); Check((s[2]&16)==0,"missing stop word has explicit invalid group");
    bytes.clear(); s=Capture(); Check(s[2]==0 && s[3]==-1,"missing memory produces no valid groups");
    Fixture(); Word(0x80428730u,0x300); s=Capture(); Check(s[2]==0,"invalid scene rejected");
    ExtrasFixture(); s=Capture();
    Check(s[2]==511 && s[12]==250,"real actor x normalized over active area, all extra fields valid");
    Check(s[13]==2 && s[14]==5,"current ownership and previously saved star coins are separate masks");
    Check(s[15]==123450 && s[16]==7,"score and active area are published");
    Check(s[17]==-1 && s[18]==9600 && s[19]==32000,"checkpoint stays unavailable; position/width Q4 values are exact");
    ExtrasFixture();
    Word(0x80040000u+944+44,0x91032CDCu);
    Word(0x91032CDCu,(528u<<16)|256u);
    Word(0x91032CE0u,(6800u<<16)|385u);
    Word(0x91032CE8u,7u<<24);
    Float(0x800700ACu,3451.913330078125f); Float(0x800700B0u,-496);
    s=Capture();
    Check((s[2]&32)!=0 && s[12]==430 && s[19]==108800,
        "real E1 MEM2 stage archive location and 1-1 area bounds produce actual progress");
    Float(0x800700ACu,4131.913330078125f); s=Capture();
    Check(s[12]==530,"MEM2 course progress follows a 680-unit player movement across 6800-unit area");
    ExtrasFixture(); Word(0x80040000u+944+44,0x97FFFFF0u); s=Capture();
    Check((s[2]&32)==0,"MEM2 course array cannot cross mapped cached RAM end");
    ExtrasFixture(); Word(0x80040000u+944+44,0x91032CDDu); s=Capture();
    Check((s[2]&32)==0,"misaligned MEM2 course array rejected");
    ExtrasFixture();
    ExtrasFixture(); Word(0x8042A1C8u,0); s=Capture();
    Check((s[2]&480)==0 && s[13]==-1 && s[14]==-1 && s[15]==-1,
        "stage scene before stage-object readiness cannot publish extra fields");
    ExtrasFixture(); Word(0x80354E10u,0);
    Word(0x803741B0u,0); Word(0x803741B4u,0); Word(0x803741B8u,0); s=Capture();
    Check((s[2]&480)==0 && s[13]==-1 && s[15]==-1,
        "zero-initialized ownership array cannot flash all collected before player is ready");
    ExtrasFixture(); Word(0x80377A78u,0); s=Capture();
    Check((s[2]&480)==0 && s[13]==-1 && s[15]==-1,
        "player ID without registered live object cannot expose run state");
    ExtrasFixture(); Word(0x80070000u,18); s=Capture();
    Check((s[2]&480)==0 && s[13]==-1 && s[15]==-1,
        "stale registry object cannot retain previous-run extra fields");
    ExtrasFixture(); s=Capture();
    Check(s[13]==2 && s[15]==123450,"extra fields become available once actual player is ready");
    Word(0x803741B4u,4); s=Capture();
    Check(s[13]==0 && s[14]==5,"death/reset follows guest ownership reset without retaining collected stars");
    for (uint32_t owner : {0u,1u,2u,3u}) {
        ExtrasFixture(); Word(0x803741B0u,owner); s=Capture();
        Check(s[13]==3,"each valid player ownership counts as collected");
    }
    ExtrasFixture(); Word(0x803741B0u,5); s=Capture();
    Check((s[2]&64)==0 && s[13]==-1,"invalid owner does not masquerade as collected star");
    ExtrasFixture(); bytes.erase(0x803741B0u); s=Capture();
    Check((s[2]&64)==0,"partial star-coin memory invalidates entire current group");
    ExtrasFixture(); Word(0x80030024u,0x0000FF00u); s=Capture();
    Check((s[2]&256)==0 && s[13]==2,"invalid save slot clears saved group without hiding current stars");
    ExtrasFixture(); Word(0x80315B98u,1); s=Capture();
    Check((s[2]&256)==0 && s[13]==2,"super-guide excludes saved coin styling as original HUD does");
    ExtrasFixture(); Word(0x8002120Cu,0x02040407u); s=Capture();
    Check((s[2]&32)==0 && s[12]==-1,"invalid course-file index clears progress");
    ExtrasFixture(); Word(0x80040000u+944+156,257); s=Capture();
    Check((s[2]&32)==0,"corrupt zone count rejected");
    ExtrasFixture(); Word(0x8005000Cu,8u<<24); s=Capture();
    Check((s[2]&32)==0,"unmatched active zone never uses another zone");
    ExtrasFixture(); Word(0x80050004u,(200u<<16)|500u); s=Capture();
    Check((s[2]&32)==0,"vertical area omitted from horizontal progress strip");
    ExtrasFixture(); Float(0x800700ACu,50000); s=Capture();
    Check((s[2]&32)==0,"out-of-area actor position unavailable");
    ExtrasFixture(); Word(0x800700ACu,0x7FC00000u); s=Capture();
    Check((s[2]&32)==0,"NaN actor coordinates rejected");
    ExtrasFixture(); Word(0x80070000u,18); Word(0x80060004u,0x80060000u); s=Capture();
    Check((s[2]&32)==0,"stale actor ID with cyclic registry rejected");
    ExtrasFixture(); Float(0x800700ACu,75); s=Capture();
    Check(s[12]==0,"small entry margin clamps to start");
    ExtrasFixture(); Float(0x800700ACu,2125); s=Capture();
    Check(s[12]==1000,"small goal margin clamps to area end");
    ExtrasFixture(); Word(0x80428730u,3); s=Capture();
    Check(s[2]==5 && s[12]==-1 && s[13]==-1 && s[15]==-1,"world-map transition retains lives and clears every course extra");
    // Concurrent UI readers see either complete previous or complete next snapshot.
    Fixture(); Capture(); std::atomic<bool> done{false}, bad{false};
    std::thread reader([&] {
        while(!done.load()) {
            const auto copy=ThorState::Read();
            if (copy[2]!=31 || copy[5]!=2 || copy[6]!=4 || copy[8]!=99 || copy[9]!=42 || copy[10]!=300) bad.store(true);
        }
    });
    for(int i=0;i<2000;++i) Capture();
    done.store(true); reader.join();
    Check(!bad.load(),"mutex publication preserves complete snapshots across UI reads");
    std::printf("PASS: %d checks against actual thor_state.cpp; mock Memory has no write API.\n",checks);
    return 0;
}
