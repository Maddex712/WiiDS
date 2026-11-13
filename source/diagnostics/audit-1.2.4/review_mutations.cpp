#define main original_font_regression_main
#include "font_test.cpp"
#undef main
#include "wiids_disc.h"
#include <algorithm>
#include <memory>

static uint32_t state = 0x61d34a89;
static uint32_t random32() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
class MemoryPart : public wiids::disc::Part {
    Bytes bytes;
public:
    explicit MemoryPart(Bytes value) : bytes(std::move(value)) {}
    uint64_t Size() const override { return bytes.size(); }
    void ReadAt(uint64_t offset, void* output, size_t size) override {
        if (offset > bytes.size() || size > bytes.size() - size_t(offset)) throw std::runtime_error("fixture range");
        if (size) std::memcpy(output, bytes.data()+size_t(offset), size);
    }
};
static Bytes mutate(const Bytes& seed, int iteration) {
    Bytes b=seed;
    if (iteration % 7 == 0) b.resize(random32() % (b.size()+1));
    else for (unsigned changes=1+random32()%6; changes && !b.empty(); --changes) {
        size_t at=random32()%b.size();
        b[at] ^= uint8_t(1u << (random32()%8));
        if (iteration%3==0 && at+4<=b.size()) u32(b,at,random32());
    }
    return b;
}
int main(int argc,char** argv) {
    size_t accepted=0,rejected=0;
    const auto seed=font();
    for(int i=0;i<20000;++i) {
        try { (void)BuildFontAtlas(mutate(seed,i),64,64); ++accepted; }
        catch(const std::exception&) { ++rejected; }
    }
    std::printf("FONT mutations 20000 accepted=%zu rejected=%zu\n",accepted,rejected);
    size_t discCases=0,discAccepted=0,discRejected=0;
    for(int arg=1;arg<argc;++arg) {
        Bytes disc=ReadFile(argv[arg]);
        for(int i=0;i<300;++i) {
            std::vector<std::unique_ptr<wiids::disc::Part>> parts;
            parts.push_back(std::make_unique<MemoryPart>(mutate(disc,i)));
            try { (void)wiids::disc::Identify(parts); ++discAccepted; }
            catch(const std::exception& e) {
                if(std::string(e.what()).find("TEST_ALLOCATION_BUDGET")!=std::string::npos) {
                    std::printf("ALLOCATION GUARD %s mutation %d\n",argv[arg],i); return 2;
                }
                ++discRejected;
            }
            ++discCases;
        }
    }
    std::printf("DISC mutations %zu accepted=%zu rejected=%zu\n",discCases,discAccepted,discRejected);
    std::printf("PASS deterministic mutation smoke checks\n");
}
