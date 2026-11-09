#include "widescreen_hud.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

// The real runtime recycles Batch storage here. This host fixture owns its small synthetic batches.
gx::Batch::~Batch() = default;
using namespace wiids::mkdd;
static unsigned checks;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}
static bool near(float a, float b) { return std::abs(a-b) < 0.000001f; }
static Packet race() {
    auto p = unavailable();
    p[Flags] = Race; p[Mode] = 2; p[Phase] = 17; p[Screens] = 1; p[TargetKart] = 0;
    return p;
}
static uint32_t texture(gx::Batch& b, uint64_t hash) {
    auto t = std::make_shared<gx::TexData>();
    t->id = static_cast<uint32_t>(b.new_textures.size()+1); t->hash = hash;
    b.new_textures.push_back(t);
    return t->id;
}
static unsigned draw(gx::Batch& b, uint32_t texture, float x, float y, float w=40, float h=30) {
    gx::PixelState state{};
    state.proj[0] = 2.0f/608; state.proj[1] = -1;
    state.proj[2] = -2.0f/448; state.proj[3] = 1; state.proj[6] = 1;
    state.viewport[0] = 304; state.viewport[1] = -224;
    state.tex_id[0] = texture; state.num_texgens = 1;
    gx::Cmd c{};
    c.type = gx::CmdType::Draw; c.count = 6;
    c.state = static_cast<uint32_t>(b.states.size());
    c.first = static_cast<uint32_t>(b.indices.size());
    c.vfirst = static_cast<uint32_t>(b.verts.size()); c.vcount = 4;
    for (unsigned i=0; i<4; ++i) {
        gx::GpuVertex v;
        v.pos[0] = x+((i==1 || i==2) ? w : 0); v.pos[1] = y+(i>=2 ? h : 0); v.pos[2] = 0;
        b.verts.push_back(v);
    }
    for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u}) b.indices.push_back(c.vfirst+i);
    b.states.push_back(state); b.cmds.push_back(c);
    return static_cast<unsigned>(b.cmds.size()-1);
}
static float offset(const gx::Batch& b, unsigned c) {
    return b.states[b.cmds[c].state].proj[1]+1;
}
static constexpr uint64_t rank8=0x0e02ee11fcaba4a3ull, timeLabel=0x308cfec6dd602f1aull,
    lapLabel=0x78d6316197c6639cull, digit1=0xdd297d2abef7fef4ull,
    luigi=0x2a2c7d128254063dull, marker=0x9fda7060918eefcaull,
    mushroom=0x0d91ebea89c0d260ull, slot=0xbd87ce88ca5b4c3cull, white=0xe2174af050c83512ull;

int main(int argc, char**) {
    try {
        WidescreenHud hud;
        gx::Batch b;
        auto rank=draw(b,texture(b,rank8),23,16,96,96);
        auto portrait=draw(b,texture(b,luigi),40,145,51,51);
        auto lap=draw(b,texture(b,lapLabel),27,389,55,27);
        auto time=draw(b,texture(b,timeLabel),370,23,51,26);
        auto map=draw(b,texture(b,marker),490,210,32,32);
        auto lapSlash=draw(b,texture(b,0xd55fea1b8779e21eull),107,392,16,32);
        auto mapLine=draw(b,texture(b,0xa2a48331f2a65e00ull),490,125,16,8);
        auto mapOutline=draw(b,texture(b,0x815368b1461aed9cull),463,54,121,242);
        auto item=draw(b,texture(b,mushroom),126,23,60,60);
        auto itemSlot=draw(b,texture(b,slot),190,19,60,59);
        auto highlight=draw(b,texture(b,white),182,30,7,38);
        auto fade=draw(b,b.states[b.cmds[highlight].state].tex_id[0],0,0,608,448);
        auto digit=texture(b,digit1);
        auto lapDigit=draw(b,digit,80,384,32,32);
        auto clockDigit=draw(b,digit,422,25,24,24);
        // Two draws may share the exact same original PixelState, despite different positions.
        b.cmds[clockDigit].state = b.cmds[lapDigit].state;
        auto middleDigit=draw(b,digit,288,200,32,32);
        b.cmds[middleDigit].state = b.cmds[lapDigit].state;
        auto prompt=draw(b,texture(b,0x123456789abcdef0ull),230,190,150,70);
        auto composite=draw(b,b.states[b.cmds[time].state].tex_id[0],0,0,608,448);
        b.states[b.cmds[composite].state].tex_is_efb[0] = 1;
        const auto states=b.states;
        auto result = argc>1 ? WidescreenHud::Result{} : hud.fit(b,true,race());
        check(near(offset(b,rank),-1.0f/3),"rank is still at the old 4:3 inset");
        check(near(offset(b,portrait),-1.0f/3),"portrait must follow left rank");
        check(near(offset(b,lap),-1.0f/3),"lap label left anchor");
        check(near(offset(b,time),1.0f/3),"time right anchor");
        check(near(offset(b,map),1.0f/3),"map marker right anchor");
        check(near(offset(b,lapSlash),-1.0f/3),"lap slash must follow its digits");
        check(near(offset(b,mapLine),1.0f/3),"minimap start line must follow map and markers");
        check(near(offset(b,mapOutline),1.0f/3),"course map outline right anchor");
        check(near(offset(b,item),-1.0f/3),"held item follows top-left group");
        check(near(offset(b,itemSlot),-1.0f/3),"item slot follows held item");
        check(near(offset(b,highlight),-1.0f/3),"item slot highlight stays attached");
        check(near(offset(b,fade),0),"full-screen mask must not move");
        check(near(offset(b,lapDigit),-1.0f/3),"shared lap digit left");
        check(near(offset(b,clockDigit),1.0f/3),"shared clock digit right");
        check(near(offset(b,middleDigit),0),"shared centred digit stays centred");
        check(near(offset(b,prompt),0),"GO/countdown/unknown picture stays centred");
        check(near(offset(b,composite),0),"frame copy must not move");
        check(result.left==8 && result.right==5,"classified group counts");
        for (unsigned i=0; i<states.size(); ++i)
            check(std::memcmp(&states[i],&b.states[i],sizeof(gx::PixelState))==0,"original state mutated");
        check(near(b.states[b.cmds[rank].state].proj[0],2.0f/608),"icon width changed");
        check(near(b.states[b.cmds[rank].state].proj[2],-2.0f/448),"icon height changed");
        check(std::abs(offset(b,rank)*.75f*1920/2+240)<0.001f,"left group must move 240 physical pixels");
        check(std::abs(offset(b,time)*.75f*1920/2-240)<0.001f,"right group must move 240 physical pixels");

        for (unsigned condition=0;condition<8;++condition) {
            gx::Batch x; WidescreenHud h;
            auto command=draw(x,texture(x,rank8),23,16,96,96); auto p=race(); bool wide=true;
            if(condition==0) wide=false;
            if(condition==1) p[Flags]=0;
            if(condition==2) p[Screens]=2;
            if(condition==3) p[Phase]=19;
            if(condition==4) p[kHeader+Finished]=1;
            if(condition==5) x.states[0].proj[6]=0;
            if(condition==6) x.states[0].viewport[0]=100;
            if(condition==7) x.verts[0].pos[0]=NAN;
            h.fit(x,wide,p);
            check(near(offset(x,command),0),"4:3/non-race/split/ending/finished/perspective/offscreen/invalid must not move");
        }
        // Art learned in 4:3 remains recognised after a live switch, without reloading textures.
        gx::Batch a; WidescreenHud live;
        draw(a,texture(a,timeLabel),370,23,51,26); live.fit(a,false,race());
        gx::Batch next; auto nextDraw=draw(next,1,370,23,51,26);
        live.fit(next,true,race());
        check(near(offset(next,nextDraw),1.0f/3),"live aspect switch missed resident textures");
        // As in the renderer, a reported dead texture survives its final batch, then expires.
        gx::Batch last;
        auto lastDraw=draw(last,1,370,23,51,26); last.dead_textures.push_back(1);
        live.fit(last,true,race());
        check(near(offset(last,lastDraw),1.0f/3),"texture identity deleted one batch too early");
        gx::Batch expired;
        auto expiredDraw=draw(expired,1,370,23,51,26); live.fit(expired,true,race());
        check(near(offset(expired,expiredDraw),0),"dead texture identity retained");
        gx::Batch hidden; WidescreenHud clipping;
        auto stray=draw(hidden,texture(hidden,rank8),-90,20,40,40);
        auto incoming=draw(hidden,1,-20,20,40,40);
        auto copyDraw=draw(hidden,1,700,20,40,40);
        hidden.states[hidden.cmds[copyDraw].state].tex_is_efb[0]=1;
        auto noRace=unavailable(); auto clipped=clipping.fit(hidden,true,noRace);
        check(hidden.cmds[stray].count==0 && clipped.clipped==1,"originally clipped title/course sprites exposed");
        check(hidden.cmds[incoming].count==6,"partly visible animated pane removed");
        check(hidden.cmds[copyDraw].count==6,"copy-sampling draw clipped as a sprite");
        for (const auto& xy : {std::array<float,2>{608,20}, {20,-40}, {20,448}, {-40,20}}) {
            gx::Batch edge; WidescreenHud h;
            auto edgeDraw=draw(edge,texture(edge,rank8),xy[0],xy[1],40,40);
            h.fit(edge,true,noRace);
            check(edge.cmds[edgeDraw].count==0,"pane wholly beyond an original clipping boundary exposed");
        }
        std::printf("PASS %u actual WidescreenHud checks; synthetic GX batches\n",checks);
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1;
    }
}
