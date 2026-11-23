// Design pass 9: pass 8's drawing at the pass 5b mockups' measured sizes (plates no larger, text no
// smaller relative to its plate than 5b). Design pass 8: the approved pass 5b look for the native lower START, Party and battle menus.
// The helpers below are pass 6's XdSkin drawing (XdSkin.java: steel, inlay, well, porthole, rivet,
// pill, hpBar, heartGauge, ball, monSlot, glyph, typeGlyph, arena, cursor, focus) and the layouts
// are XdKey/XdBoard's, ported from Canvas to ImDrawList in the same 320-unit design space. Static:
// pass 6's pulses, glints and breathing aura are drawn at rest because the mesh is cached.
// Included by xd_lower_presenter.inc and by the host preview harness, so the previews render this
// exact code. Pure drawing: no ImGui windows, input, focus or touch, and only values the game
// publishes in XdPresentationState. Missing values are left out.
#include "xd_presentation.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
static std::string xdUtf8(const uint16_t* p,int capacity=XD_TEXT){
    std::string s;
    for(int i=0;i<capacity&&p[i];i++){
        unsigned c=p[i];if(c==10){s+='\n';continue;}if(c<32||c==0xffff)continue;
        // The star in P*DA has no glyph in the device font.
        if(c==0x2605||c==0x2606||c==0x22c6||c==0x2217||c==0xff0a)c='*';
        if(c<128)s+=(char)c;
        else if(c<2048){s+=(char)(0xc0|(c>>6));s+=(char)(0x80|(c&63));}
        else if(c<0xd800||c>0xdfff){s+=(char)(0xe0|(c>>12));s+=(char)(0x80|((c>>6)&63));s+=(char)(0x80|(c&63));}
    }return s;
}
// XD captions are plain upper case, Latin-1 accents included (POKéMON -> POKÉMON).
static std::string xdCaps(std::string s){
    for(size_t i=0;i<s.size();i++){
        unsigned char c=s[i];
        if(c>='a'&&c<='z')s[i]=(char)(c-32);
        else if(c==0xc3&&i+1<s.size()){unsigned char d=s[i+1];if(d>=0xa0&&d<=0xbe&&d!=0xb7)s[i+1]=(char)(d-0x20);i++;}
    }return s;
}
// Display case only; leave mixed-case nicknames and stored save names intact.
static std::string xdPartyName(std::string s){
    bool lower=false;for(unsigned char c:s)if(c>='a'&&c<='z')lower=true;
    if(lower)return s;
    bool first=true;for(char& c:s){if(c>='A'&&c<='Z'){if(!first)c+=32;first=false;}else if(c==' '||c=='-')first=true;}
    return s;
}
// imgui.cpp adds the device's condensed face as "XD Lower" ("XD Lower Faux" when only a
// regular weight exists, drawn with a small double strike). Fallback: the default font.
static ImFont* xdLowerFont(bool* faux){
    auto& fonts=ImGui::GetIO().Fonts->Fonts;
    for(ImFont* f:fonts){const char* n=f->GetDebugName();
        if(n&&std::strncmp(n,"XD Lower",8)==0){*faux=std::strstr(n,"Faux")!=nullptr;return f;}}
    *faux=false;return fonts.empty()?ImGui::GetFont():fonts[0];
}
// "XD Wide" is pass 6's tracked caption face (sans-serif bold); absent, captions use the lower face.
static ImFont* xdWideFont(){
    for(ImFont* f:ImGui::GetIO().Fonts->Fonts){const char* n=f->GetDebugName();if(n&&std::strcmp(n,"XD Wide")==0)return f;}
    return nullptr;
}
static thread_local ImTextureID xdSummaryImage=0;
static thread_local XdTouchRegion xdHits[160];static thread_local int xdHitCount=0;

static void xdLowerPaint(ImDrawList& list,const XdPresentationState& s,int width,int height,ImTextureID topFrame=0,float topAspect=16.f/9.f,float skyW=0,float skyH=0,bool xg=false){
    typedef uint32_t C; // 0xAARRGGBB, as in XdSkin
    const float PI=3.14159265f;
    const float u=width/320.f,H=height/u;
    bool faux=false;ImFont* font=xdLowerFont(&faux);
    ImFont* wide=xdWideFont();const bool wideFaux=!wide&&faux;if(!wide)wide=font;
    // XdSkin palette.
    const C WHITE=0xFFFFFFFF,STEEL_HI=0xFFC4C4C4,STEEL=0xFF8E8E8E,STEEL_LOW=0xFF5C5C5C,INSET=0xFF181818,
        CIPHER=0xFF6A3A86,MAROON=0xFF7A1450,TEXT_2=0xFFC9CEDA,TEXT_3=0xFF8D95A6,NAVY=0xFF14203A,
        FOCUS=0xFFFFB531,FOCUS_HOT=0xFFFF7A1F,SHADOW_AURA=0xFF9A5CFF,SHADOW_LIGHT=0xFFC9A8F5,SNAG=0xFF20A9A4,
        FIGHT=0xFFD8352A,BAG=0xFFE8A51C,POKEMON=0xFF45AE3E,RUN=0xFF3474C8,CALL=0xFF6A37B4,CLOSE=0xFFD8323C,
        UTILITY=0xFFA9B0BC,TEAL=0xFF0E8C94,PDA=0xFF0E7F86,HP_GOOD=0xFF30D850,HP_MID=0xFFF8C800,HP_LOW=0xFFF84838,
        HP_TRACK=0xFF2A2C30,GREY=0x99120D1E;
    auto im=[](C c){return (ImU32)IM_COL32((c>>16)&255,(c>>8)&255,c&255,c>>24);};
    auto cl=[](float v){return (C)std::clamp((int)v,0,255);};
    auto shade=[&](C c,float f){return (c&0xFF000000u)|cl(((c>>16)&255)*f)<<16|cl(((c>>8)&255)*f)<<8|cl((c&255)*f);};
    auto mix=[&](C a,C b,float t){
        auto ch=[&](int sh){float x=(float)((a>>sh)&255),y=(float)((b>>sh)&255);return cl(x+(y-x)*t)<<sh;};
        return ch(24)|ch(16)|ch(8)|ch(0);};
    auto alpha=[&](C c,float a){return cl((c>>24)*a)<<24|(c&0xFFFFFF);};
    auto light=[](C c){return ((c>>16)&255)*0.299+((c>>8)&255)*0.587+(c&255)*0.114>176;};
    auto P=[&](float x,float y){return ImVec2(x*u,y*u);};

    // Shapes: XdSkin.shape(), a rectangle with 45-degree clipped corners; a cut of half the short
    // side or more gives a circle (or a pill), used only for portholes, balls, buttons and tags.
    struct S{float x,y,w,h,c;bool round;};
    auto mk=[](float x,float y,float w,float h,float c){
        S r{x,y,w,h,std::max(0.f,c),false};const float m=std::min(w,h)/2;if(r.c>=m-0.01f){r.c=m;r.round=true;}return r;};
    auto hit=[&](const S& r,int action,int arg,int cursor=-1,bool enabled=true){
        if(enabled&&xdHitCount<160)xdHits[xdHitCount++]={r.x,r.y,r.w,r.h,action,arg,cursor};};
    auto choiceHit=[&](const S& r,const XdChoice& c){hit(r,c.action,c.argument,c.cursor,c.enabled);};
    auto ins=[&](const S& r,float d){
        const float w=r.w-2*d,h=r.h-2*d;
        return r.round?S{r.x+d,r.y+d,w,h,std::min(w,h)/2,true}:S{r.x+d,r.y+d,w,h,std::max(0.f,r.c-d*0.42f),false};};
    auto grow=[&](const S& r,float d,float c){return mk(r.x-d,r.y-d,r.w+2*d,r.h+2*d,c);};
    auto span=[&](const S& r,float y,float& l,float& rr){
        float dy=y<r.y+r.c?r.y+r.c-y:y>r.y+r.h-r.c?y-(r.y+r.h-r.c):0,d=dy;
        if(r.round)d=r.c-std::sqrt(std::max(0.f,r.c*r.c-dy*dy));
        l=r.x+d;rr=r.x+r.w-d;};
    auto path=[&](const S& r){
        if(r.round){list.PathRect(P(r.x,r.y),P(r.x+r.w,r.y+r.h),r.c*u);return;}
        const float x=r.x,y=r.y,w=r.w,h=r.h,c=r.c;
        const ImVec2 q[8]={P(x+c,y),P(x+w-c,y),P(x+w,y+c),P(x+w,y+h-c),P(x+w-c,y+h),P(x+c,y+h),P(x,y+h-c),P(x,y+c)};
        for(const ImVec2& p:q)list.PathLineTo(p);};
    auto fillS=[&](const S& r,C col){
        if(r.round)list.AddRectFilled(P(r.x,r.y),P(r.x+r.w,r.y+r.h),im(col),r.c*u);else{path(r);list.PathFillConvex(im(col));}};
    auto strokeS=[&](const S& r,C col,float w){path(r);list.PathStroke(im(col),ImDrawFlags_Closed,w*u);};
    auto quad=[&](ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 d,C ca,C cb,C cc,C cd){
        list.PrimReserve(6,4);const ImVec2 uv=ImGui::GetIO().Fonts->TexUvWhitePixel;const ImDrawIdx i=(ImDrawIdx)list._VtxCurrentIdx;
        list.PrimWriteIdx(i);list.PrimWriteIdx((ImDrawIdx)(i+1));list.PrimWriteIdx((ImDrawIdx)(i+2));
        list.PrimWriteIdx(i);list.PrimWriteIdx((ImDrawIdx)(i+2));list.PrimWriteIdx((ImDrawIdx)(i+3));
        list.PrimWriteVtx(a,uv,im(ca));list.PrimWriteVtx(b,uv,im(cb));list.PrimWriteVtx(c,uv,im(cc));list.PrimWriteVtx(d,uv,im(cd));};
    // Horizontal slices of a shape between y0 and y1, coloured by colAt(y): exact on the clipped
    // corners, sampled on round ones. Canvas LinearGradient fills and clipped bands become this.
    struct St{float t;C c;};
    auto slices=[&](const S& r,float y0,float y1,const St* st,int n,float g0,float g1){
        y0=std::max(y0,r.y);y1=std::min(y1,r.y+r.h);if(y1<=y0)return;
        float ys[48];int k=0;ys[k++]=y0;ys[k++]=y1;
        auto add=[&](float y){if(y>y0&&y<y1&&k<48)ys[k++]=y;};
        if(r.round){for(int i=1;i<8;i++){add(r.y+r.c*i/8);add(r.y+r.h-r.c*i/8);}}
        add(r.y+r.c);add(r.y+r.h-r.c);
        for(int i=0;i<n;i++)add(g0+(g1-g0)*st[i].t);
        std::sort(ys,ys+k);
        auto colAt=[&](float y){
            if(n==1)return st[0].c;
            const float t=g1>g0?(y-g0)/(g1-g0):0;
            if(t<=st[0].t)return st[0].c;
            for(int i=1;i<n;i++)if(t<=st[i].t)return mix(st[i-1].c,st[i].c,(t-st[i-1].t)/std::max(1e-4f,st[i].t-st[i-1].t));
            return st[n-1].c;};
        for(int i=0;i+1<k;i++){
            if(ys[i+1]-ys[i]<1e-4f)continue;float l0,r0,l1,r1;span(r,ys[i],l0,r0);span(r,ys[i+1],l1,r1);
            const C a=colAt(ys[i]),b=colAt(ys[i+1]);
            quad(P(l0,ys[i]),P(r0,ys[i]),P(r1,ys[i+1]),P(l1,ys[i+1]),a,a,b,b);}};
    auto band=[&](const S& r,float y0,float y1,C col){const St st[1]={{0,col}};slices(r,y0,y1,st,1,r.y,r.y+r.h);};
    // Top-to-bottom gradient with an anti-aliased outline.
    auto grad=[&](const S& r,const St* st,int n){fillS(r,st[n/2].c);slices(r,r.y,r.y+r.h,st,n,r.y,r.y+r.h);};
    // Radial highlight (Canvas RadialGradient) on a vertex grid kept inside the shape.
    auto radial=[&](const S& r,float cx,float cy,float R,C in,C out,int nx,int ny){
        auto at=[&](float x,float y){return mix(in,out,std::min(1.f,std::hypot(x-cx,y-cy)/R));};
        for(int j=0;j<ny;j++){
            const float y0=r.y+r.h*j/ny,y1=r.y+r.h*(j+1)/ny;float l0,r0,l1,r1;span(r,y0,l0,r0);span(r,y1,l1,r1);
            for(int i=0;i<nx;i++){
                const float a0=l0+(r0-l0)*i/nx,a1=l0+(r0-l0)*(i+1)/nx,b0=l1+(r1-l1)*i/nx,b1=l1+(r1-l1)*(i+1)/nx;
                quad(P(a0,y0),P(a1,y0),P(b1,y1),P(b0,y1),at(a0,y0),at(a1,y0),at(b1,y1),at(b0,y1));}}};
    auto disc=[&](float cx,float cy,float r,C in,C out,float hx,float hy,float R){
        const S d=mk(cx-r,cy-r,2*r,2*r,r);fillS(d,out);radial(d,hx,hy,R,in,out,10,10);};

    // XD brushed steel: keyline, light-to-dark rim, body shaded top to bottom with fine streaks and one
    // soft reflection. tint 0 is silver; a colour gives Cipher purple or maroon steel.
    auto steel=[&](const S& r,C tint){
        const C hi=tint?mix(tint,WHITE,0.35f):STEEL_HI,mid=tint?tint:STEEL,lo=tint?shade(tint,0.62f):STEEL_LOW;
        S sh=r;sh.y+=1;fillS(sh,0x4D000000);
        fillS(r,0xFF0E0E0E);
        const St rim[2]={{0,mix(hi,WHITE,0.5f)},{1,shade(lo,0.7f)}};grad(ins(r,0.6f),rim,2);
        const S b=ins(r,1.4f);const St body[3]={{0,hi},{0.45f,mid},{1,lo}};grad(b,body,3);
        uint32_t seed=(uint32_t)(int)(r.x*7+r.y*13);
        for(float y=b.y+0.5f;y<b.y+b.h;y+=0.7f){
            seed=seed*1103515245u+12345u;const int v=(int)((seed>>16)&63);
            band(b,y,y+0.3f,v<32?((C)(v*2)<<24|0xFFFFFF):((C)((v-32)*2)<<24));}
        radial(b,b.x+b.w*0.28f,b.y+b.h*0.2f,std::max(b.w,b.h)*0.45f,0x30FFFFFF,0x00FFFFFF,8,6);};
    // Black inset window with a shadowed top edge and a faint light bottom edge.
    auto well=[&](const S& r){fillS(r,INSET);band(r,r.y,r.y+1.2f,0x99000000);band(r,r.y+r.h-0.6f,r.y+r.h,0x26FFFFFF);};
    // Colour inlay set into a plate: shaded colour, pinstripes, hard specular band, inner shadow, keyline.
    auto inlay=[&](const S& r,C base){
        const St st[3]={{0,mix(base,WHITE,0.18f)},{0.5f,base},{1,shade(base,0.62f)}};grad(r,st,3);
        list.PushClipRect(P(r.x+r.c*0.6f,r.y),P(r.x+r.w-r.c*0.6f,r.y+r.h),true);
        for(float x=r.x-r.h;x<r.x+r.w;x+=3)list.AddLine(P(x,r.y+r.h),P(x+r.h,r.y),im(0x14FFFFFF),0.4f*u);
        list.PopClipRect();
        band(r,r.y,r.y+std::min(r.h*0.4f,20.f),0x26FFFFFF);band(r,r.y,r.y+1.4f,0x80000000);band(r,r.y+r.h-0.8f,r.y+r.h,0x33FFFFFF);
        strokeS(r,0xB3000000,0.8f);};
    // Gen 6 button finish for round dock-style discs and app chips.
    auto gloss=[&](const S& r,C base){
        S sh=r;sh.y+=1;fillS(sh,0x47000000);fillS(r,shade(base,0.4f));
        const St rim[3]={{0,0xFFF4F4F4},{0.5f,mix(STEEL,base,0.25f)},{1,0xFF6E6E6E}};grad(ins(r,0.7f),rim,3);
        const S b=ins(r,1.6f);const St body[2]={{0,shade(base,1.08f)},{1,shade(base,0.88f)}};grad(b,body,2);
        band(b,b.y,b.y+std::min(b.h*0.42f,22.f),0x24FFFFFF);band(b,b.y,b.y+0.8f,0x52FFFFFF);band(b,b.y+b.h-0.9f,b.y+b.h,0x33000000);};
    auto porthole=[&](float cx,float cy,float d,C tint){
        steel(mk(cx-d/2,cy-d/2,d,d,d/2),tint);
        list.AddCircleFilled(P(cx,cy),(d/2-2.2f)*u,im(0xFF161616),40);
        list.AddCircle(P(cx,cy+0.4f),(d/2-2.2f)*u,im(0x40FFFFFF),40,0.6f*u);};
    auto rivet=[&](float x,float y,float r){
        list.AddCircleFilled(P(x,y+r*0.35f),r*u,im(0x80000000),16);
        disc(x,y,r,WHITE,0xFF7A7A7A,x-r*0.35f,y-r*0.35f,r*1.3f);};
    auto ring=[&](const S& r,C col,float w){
        const S o=r.round?mk(r.x-w/2,r.y-w/2,r.w+w,r.h+w,999):mk(r.x-w/2,r.y-w/2,r.w+w,r.h+w,r.c+w*0.2f);strokeS(o,col,w);};
    // Pass 5b selection: warm orange rim on menus and party, XD's white cursor on battle plates.
    auto focus=[&](const S& r){ring(r,FOCUS_HOT,2);strokeS(ins(r,0.4f),0xFFFFE07A,0.8f);};
    auto cursor=[&](const S& r){
        const S o=mk(r.x+1.6f,r.y+1.6f,r.w-3.2f,r.h-3.2f,std::max(0.f,r.c-0.7f));
        strokeS(o,0x80000000,3.6f);strokeS(o,WHITE,2);
        strokeS(mk(o.x+1.7f,o.y+1.7f,o.w-3.4f,o.h-3.4f,std::max(0.f,o.c-0.7f)),alpha(0xFFBFF6F2,0.8f),0.7f);};
    // Shadow aura at rest (pass 6 breathes it; the cached mesh holds the mid pose).
    auto aura=[&](const S& r){for(int i=3;i>=1;i--)strokeS(mk(r.x-i*1.3f,r.y-i*1.3f,r.w+i*2.6f,r.h+i*2.6f,r.c+i),alpha(SHADOW_AURA,0.55f*(0.5f-i*0.12f)),1.4f);};
    // Disabled plates read at about 40%, as pass 5b's 0 PP plate: a veil in the arena's colour.
    auto veil=[&](const S& r){fillS(grow(r,0.5f,r.c+0.5f),GREY);};

    // Text. HEAVY is the condensed lower face, WIDE the tracked caption face; edges are Canvas stroke
    // outlines (round joins), drawn as eight offset passes.
    auto cps=[](const std::string& t){int n=0;for(unsigned char c:t)n+=(c&0xc0)!=0x80;return n;};
    auto tw=[&](ImFont* f,const std::string& t,float size,float track){
        return f->CalcTextSizeA(size*u,FLT_MAX,0,t.c_str()).x/u+track*size*std::max(0,cps(t)-1);};
    auto run=[&](ImFont* f,float x,float base,const std::string& t,float size,C col,float track){
        const float y=base-size*(f->FontSize>0?f->Ascent/f->FontSize:0.8f);const bool dbl=(f==font&&faux)||(f==wide&&wideFaux);
        if(track==0){
            list.AddText(f,size*u,P(x,y),im(col),t.c_str());if(dbl)list.AddText(f,size*u,P(x+size*0.025f,y),im(col),t.c_str());return;}
        const char* p=t.c_str();const char* e=p+t.size();
        while(p<e){
            const unsigned char c=(unsigned char)*p;const int n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
            list.AddText(f,size*u,P(x,y),im(col),p,p+n);if(dbl)list.AddText(f,size*u,P(x+size*0.025f,y),im(col),p,p+n);
            x+=f->CalcTextSizeA(size*u,FLT_MAX,0,p,p+n).x/u+track*size;p+=n;}};
    enum{LEFT,CENTER,RIGHT};
    auto draw=[&](ImFont* f,const std::string& t,float x,float base,float size,C col,C edge,float ew,float track,int align){
        const float w=tw(f,t,size,track);if(align==CENTER)x-=w/2;else if(align==RIGHT)x-=w;
        if(ew>0&&(edge>>24))for(int k=0;k<8;k++){const float a=k*PI/4;run(f,x+std::cos(a)*ew/2,base+std::sin(a)*ew/2,t,size,edge,track);}
        run(f,x,base,t,size,col,track);};
    auto fit=[&](ImFont* f,std::string t,float size,float maxw,float track){
        if(tw(f,t,size,track)<=maxw)return t;
        const std::string e="\xE2\x80\xA6";
        while(!t.empty()&&tw(f,t+e,size,track)>maxw)
            while(!t.empty()){unsigned char b=(unsigned char)t.back();t.pop_back();if((b&0xc0)!=0x80)break;}
        return t+e;};
    auto fitSize=[&](ImFont* f,const std::string& t,float max,float min,float w){
        const float m=tw(f,t,max,0);return m<=w||m<=0?max:std::max(min,max*w/m);};
    // XD caption with a navy outline, shrunk to fit a width (XdSkin.xdLabel).
    auto xdLabel=[&](const std::string& t,float x,float y,float max,float min,float w,float track,float edge,int align){
        float size=max;while(size>min&&tw(wide,t,size,track)>w)size-=max*0.04f;
        draw(wide,fit(wide,t,size,w,track),x,y+size*0.36f,size,WHITE,NAVY,edge,track,align);};

    // Ball marks, tags, bars and gauges.
    auto ball=[&](float cx,float cy,float d,C top,C line){
        const float r=d/2,lw=std::max(1.f/u,d*0.09f);
        list.AddCircleFilled(P(cx,cy),r*u,im(0xFFF6F6F6),32);
        list.PathArcTo(P(cx,cy),r*u,PI,2*PI,20);list.PathFillConvex(im(top));
        list.AddCircle(P(cx,cy),r*u,im(line),32,lw*u);list.AddLine(P(cx-r,cy),P(cx+r,cy),im(line),lw*u);
        list.AddCircleFilled(P(cx,cy),r*0.3f*u,im(0xFFF6F6F6),20);list.AddCircle(P(cx,cy),r*0.3f*u,im(line),20,lw*u);};
    auto monSlot=[&](float cx,float cy,float d,bool shadow,bool fainted){
        list.AddCircleFilled(P(cx,cy+0.8f),d/2*u,im(0x40000000),32);
        disc(cx,cy,d/2,shadow?0xFF6B4A9A:0xFFF4F7FB,shadow?0xFF2A1640:0xFFB9C3D1,cx-d*0.15f,cy-d*0.2f,d*0.7f);
        ball(cx,cy,d*0.62f,fainted?0xFF8A8F99:shadow?0xFF8A5CCF:0xFFE2463A,shadow?0xFF3A2456:0xFF2B2F38);
        if(shadow)list.AddCircle(P(cx,cy),(d/2+0.4f)*u,im(alpha(SHADOW_AURA,0.9f)),40,1.3f*u);};
    auto pillWidth=[&](const std::string& t,float h){return tw(font,t,h*0.72f,0.02f)+h*1.1f;};
    auto pill=[&](float x,float y,float w,float h,C col,const std::string& t){
        fillS(mk(x,y,w,h,h),shade(col,0.45f));
        const St st[3]={{0,mix(col,WHITE,0.45f)},{0.35f,col},{1,shade(col,0.8f)}};grad(mk(x+0.6f,y+0.6f,w-1.2f,h-1.2f,h),st,3);
        draw(font,t,x+w/2,y+h*0.76f,h*0.72f,0xFF0C0C0C,alpha(mix(col,WHITE,0.7f),0.9f),0.9f,0.02f,CENTER);};
    auto statusLabel=[](const XdPokemon& p)->const char*{
        if(p.max_hp>0&&p.hp==0)return "FNT";
        switch(p.status){case 0:return "";case 3:case 4:return "PSN";case 5:return "PAR";case 6:return "BRN";case 7:return "FRZ";case 8:return "SLP";default:return "";}};
    auto statusColor=[](const std::string& l)->C{
        return l=="PSN"?0xFFA462B4:l=="PAR"?0xFFCDC520:l=="BRN"?0xFFFF8B52:l=="FRZ"?0xFFB4CDEE:l=="FNT"?0xFFE65A41:0xFFA4A48B;};
    auto badgeWidth=[&](const std::string& l,float h){return l.empty()?0.f:pillWidth(l,h);};
    auto badge=[&](const std::string& l,float x,float y,float h){if(!l.empty())pill(x,y,badgeWidth(l,h),h,statusColor(l),l);};
    auto hpColor=[&](int hp,int max){return max<=0?TEXT_3:hp*5<=max?HP_LOW:hp*2<=max?HP_MID:HP_GOOD;};
    auto hpBar=[&](float x,float y,float x1,float h,int hp,int max){
        float left=x;
        const float tagW=h*2.6f;fillS(mk(x,y-h*0.35f,tagW+h,h*1.7f,h*1.7f*0.3f),0xFF0C0C0C);
        draw(font,"HP",x+h*0.55f,y+h/2+h*0.45f,h*1.25f,0xFFFF6A3D,0,0,0,LEFT);left=x+tagW;
        fillS(mk(left-0.6f,y-0.6f,x1-left+1.2f,h+1.2f,h),0xFF1A1E26);fillS(mk(left,y,x1-left,h,h),HP_TRACK);
        if(max<=0)return;const float t=std::clamp(hp/(float)max,0.f,1.f);if(t<=0)return;
        const C base=hpColor(hp,max);const St st[3]={{0,mix(base,WHITE,0.35f)},{0.45f,base},{1,shade(base,0.72f)}};
        grad(mk(left,y,std::max(h,(x1-left)*t),h,h),st,3);};
    auto heartGauge=[&](float x,float y,float w,float h,int heart,int max){
        const float gap=0.8f,sw=(w-gap*4)/5;const bool known=heart>=0&&max>0;
        const int lit=known?(int)std::ceil(std::min(1.f,heart/(float)max)*5-1e-4f):0;
        for(int i=0;i<5;i++){
            const S g=mk(x+i*(sw+gap),y,sw,h,h);
            if(!known){strokeS(g,alpha(SHADOW_LIGHT,0.6f),0.6f);continue;}
            fillS(grow(g,0.4f,h),0xFF1A1024);fillS(g,i<lit?SHADOW_AURA:0xFFE9E4F2);}};

    // Original glyphs (XdSkin.glyph / typeGlyph), in an s-unit box at x,y.
    auto star=[&](float cx,float cy,float r,C col){
        ImVec2 p[10];for(int i=0;i<10;i++){const double a=-PI/2+i*PI/5;const float rr=i%2==0?r:r*0.45f;p[i]=P(cx+(float)std::cos(a)*rr,cy+(float)std::sin(a)*rr);}
        list.AddConcavePolyFilled(p,10,im(col));};
    auto mv=[&](float x,float y){list.PathLineTo(P(x,y));};
    auto cub=[&](float ax,float ay,float bx,float by,float cx,float cy){list.PathBezierCubicCurveTo(P(ax,ay),P(bx,by),P(cx,cy),12);};
    auto fillPath=[&](C col){
        auto& p=list._Path;
        if(p.Size>1&&std::fabs(p[0].x-p[p.Size-1].x)<0.01f&&std::fabs(p[0].y-p[p.Size-1].y)<0.01f)p.pop_back();
        list.PathFillConcave(im(col));};
    auto glyph=[&](const std::string& name,float x,float y,float s,C col){
        const float lw=std::max(1.f/u,s*0.12f);
        if(name=="bag"){
            list.AddRectFilled(P(x+s*0.14f,y+s*0.32f),P(x+s*0.86f,y+s*0.9f),im(col),s*0.16f*u);
            list.PathArcTo(P(x+s*0.5f,y+s*0.3f),s*0.18f*u,PI,2*PI,12);list.PathStroke(im(col),0,lw*u);
            list.AddRectFilled(P(x+s*0.14f,y+s*0.5f),P(x+s*0.86f,y+s*0.58f),im(0x55000000));
        }else if(name=="ball")ball(x+s/2,y+s/2,s*0.84f,col,0xFF20242C);
        else if(name=="pda"){
            list.AddRectFilled(P(x+s*0.18f,y+s*0.06f),P(x+s*0.82f,y+s*0.94f),im(col),s*0.14f*u);
            list.AddRectFilled(P(x+s*0.28f,y+s*0.17f),P(x+s*0.72f,y+s*0.55f),im(0xFF1E2430),s*0.05f*u);
            star(x+s*0.5f,y+s*0.74f,s*0.11f,0xFF1E2430);
        }else if(name=="snag"){
            for(int i=0;i<3;i++){const float a=(-90+i*120+14)*PI/180;
                list.PathArcTo(P(x+s*0.5f,y+s*0.5f),s*0.42f*u,a,a+72*PI/180,10);list.PathStroke(im(col),0,lw*u);}
            ball(x+s*0.5f,y+s*0.5f,s*0.42f,col,0xFF20242C);
        }else if(name=="run"){
            for(int i=0;i<2;i++){const float o=i*0.34f;mv(x+s*(0.16f+o),y+s*0.2f);mv(x+s*(0.46f+o),y+s*0.5f);mv(x+s*(0.16f+o),y+s*0.8f);
                list.PathStroke(im(col),0,s*0.16f*u);}
        }else if(name=="fight"){
            list.AddLine(P(x+s*0.18f,y+s*0.82f),P(x+s*0.78f,y+s*0.22f),im(col),s*0.14f*u);
            list.AddLine(P(x+s*0.22f,y+s*0.22f),P(x+s*0.82f,y+s*0.82f),im(col),s*0.14f*u);
            list.AddTriangleFilled(P(x+s*0.86f,y+s*0.14f),P(x+s*0.86f,y+s*0.4f),P(x+s*0.6f,y+s*0.14f),im(col));
        }else if(name=="call"){
            list.PathArcTo(P(x+s*0.5f,y+s*0.5f),s*0.26f*u,-PI/3,PI/3,10);list.PathStroke(im(col),0,lw*u);
            list.PathArcTo(P(x+s*0.5f,y+s*0.5f),s*0.44f*u,-55*PI/180,55*PI/180,12);list.PathStroke(im(col),0,lw*u);
            list.AddCircleFilled(P(x+s*0.32f,y+s*0.5f),s*0.12f*u,im(col),12);
        }else if(name=="save"){
            list.AddRectFilled(P(x+s*0.14f,y+s*0.08f),P(x+s*0.86f,y+s*0.92f),im(col),s*0.1f*u);
            for(int i=0;i<3;i++)list.AddRectFilled(P(x+s*0.3f,y+s*(0.3f+0.18f*i)),P(x+s*(i==2?0.56f:0.7f),y+s*(0.36f+0.18f*i)),im(0x66000000),s*0.03f*u);
        }else if(name=="exit"){
            list.AddRect(P(x+s*0.12f,y+s*0.12f),P(x+s*0.58f,y+s*0.88f),im(col),s*0.08f*u,0,lw*u);
            list.AddLine(P(x+s*0.4f,y+s*0.5f),P(x+s*0.9f,y+s*0.5f),im(col),lw*u);
            mv(x+s*0.74f,y+s*0.32f);mv(x+s*0.9f,y+s*0.5f);mv(x+s*0.74f,y+s*0.68f);list.PathStroke(im(col),0,lw*u);
        }else if(name=="game"){
            list.AddCircle(P(x+s*0.5f,y+s*0.5f),s*0.18f*u,im(col),20,lw*u);
            for(int i=0;i<8;i++){const float a=PI*i/4,c=std::cos(a),n=std::sin(a);
                list.AddLine(P(x+s*0.5f+c*s*0.3f,y+s*0.5f+n*s*0.3f),P(x+s*0.5f+c*s*0.42f,y+s*0.5f+n*s*0.42f),im(col),lw*u);}
        }else star(x+s/2,y+s/2,s*0.46f,col);};
    auto typeGlyph=[&](const std::string& t,float x,float y,float s,C col){
        const float cx=x+s/2,cy=y+s/2,lw=s*0.14f;
        if(t=="FIRE"){mv(cx,y+s*0.04f);cub(x+s*0.98f,y+s*0.4f,x+s*0.86f,y+s*0.96f,cx,y+s*0.96f);
            cub(x+s*0.14f,y+s*0.96f,x+s*0.06f,y+s*0.5f,x+s*0.34f,y+s*0.3f);cub(x+s*0.32f,y+s*0.5f,x+s*0.42f,y+s*0.56f,x+s*0.46f,y+s*0.5f);
            cub(x+s*0.4f,y+s*0.3f,x+s*0.44f,y+s*0.14f,cx,y+s*0.04f);fillPath(col);}
        else if(t=="WATER"){mv(cx,y+s*0.04f);cub(x+s*0.62f,y+s*0.3f,x+s*0.88f,y+s*0.5f,x+s*0.86f,y+s*0.66f);
            cub(x+s*0.84f,y+s*0.86f,x+s*0.68f,y+s*0.96f,cx,y+s*0.96f);cub(x+s*0.32f,y+s*0.96f,x+s*0.16f,y+s*0.86f,x+s*0.14f,y+s*0.66f);
            cub(x+s*0.12f,y+s*0.5f,x+s*0.38f,y+s*0.3f,cx,y+s*0.04f);fillPath(col);}
        else if(t=="GRASS"){mv(x+s*0.1f,y+s*0.9f);cub(x+s*0.1f,y+s*0.3f,x+s*0.5f,y+s*0.06f,x+s*0.92f,y+s*0.08f);
            cub(x+s*0.94f,y+s*0.5f,x+s*0.7f,y+s*0.9f,x+s*0.1f,y+s*0.9f);fillPath(col);}
        else if(t=="ELECTRIC"){mv(x+s*0.62f,y+s*0.02f);mv(x+s*0.16f,y+s*0.56f);mv(x+s*0.46f,y+s*0.56f);mv(x+s*0.36f,y+s*0.98f);
            mv(x+s*0.86f,y+s*0.4f);mv(x+s*0.54f,y+s*0.4f);fillPath(col);}
        else if(t=="ICE"){for(int i=0;i<3;i++){const float a=PI*i/3,dx=std::cos(a)*s*0.44f,dy=std::sin(a)*s*0.44f;
            list.AddLine(P(cx-dx,cy-dy),P(cx+dx,cy+dy),im(col),lw*u);}}
        else if(t=="FIGHTING"){list.AddRectFilled(P(x+s*0.14f,y+s*0.18f),P(x+s*0.86f,y+s*0.86f),im(col),s*0.2f*u);
            for(int i=1;i<3;i++){const float xx=x+s*(0.14f+0.24f*i);list.AddLine(P(xx,y+s*0.2f),P(xx,y+s*0.5f),im(0x66000000),s*0.07f*u);}}
        else if(t=="POISON"){list.AddCircleFilled(P(cx,y+s*0.4f),s*0.3f*u,im(col),24);
            list.AddRectFilled(P(x+s*0.3f,y+s*0.6f),P(x+s*0.7f,y+s*0.94f),im(col),s*0.1f*u);}
        else if(t=="GROUND"){mv(x+s*0.04f,y+s*0.9f);mv(x+s*0.38f,y+s*0.24f);mv(x+s*0.56f,y+s*0.56f);mv(x+s*0.7f,y+s*0.38f);mv(x+s*0.96f,y+s*0.9f);fillPath(col);}
        else if(t=="FLYING"){mv(x+s*0.06f,y+s*0.3f);cub(x+s*0.4f,y+s*0.22f,x+s*0.8f,y+s*0.36f,x+s*0.94f,y+s*0.86f);
            cub(x+s*0.6f,y+s*0.7f,x+s*0.3f,y+s*0.7f,x+s*0.06f,y+s*0.3f);fillPath(col);}
        else if(t=="PSYCHIC"){list.AddEllipse(P(cx,cy),ImVec2(s*0.44f*u,s*0.26f*u),im(col),0,32,lw*u);list.AddCircleFilled(P(cx,cy),s*0.16f*u,im(col),16);}
        else if(t=="BUG"){list.AddEllipseFilled(P(cx,y+s*0.56f),ImVec2(s*0.28f*u,s*0.38f*u),im(col),0,28);
            list.AddLine(P(x+s*0.36f,y+s*0.2f),P(x+s*0.22f,y+s*0.04f),im(col),lw*u);list.AddLine(P(x+s*0.64f,y+s*0.2f),P(x+s*0.78f,y+s*0.04f),im(col),lw*u);}
        else if(t=="ROCK"){mv(x+s*0.3f,y+s*0.08f);mv(x+s*0.8f,y+s*0.16f);mv(x+s*0.94f,y+s*0.62f);mv(x+s*0.6f,y+s*0.94f);mv(x+s*0.12f,y+s*0.8f);mv(x+s*0.06f,y+s*0.36f);fillPath(col);}
        else if(t=="GHOST"){mv(x+s*0.16f,y+s*0.94f);mv(x+s*0.16f,y+s*0.44f);cub(x+s*0.16f,y+s*0.04f,x+s*0.84f,y+s*0.04f,x+s*0.84f,y+s*0.44f);
            mv(x+s*0.84f,y+s*0.94f);mv(x+s*0.67f,y+s*0.8f);mv(cx,y+s*0.94f);mv(x+s*0.33f,y+s*0.8f);fillPath(col);}
        else if(t=="DRAGON"){mv(x+s*0.2f,y+s*0.06f);cub(x+s*0.9f,y+s*0.2f,x+s*0.96f,y+s*0.8f,x+s*0.5f,y+s*0.96f);
            cub(x+s*0.66f,y+s*0.6f,x+s*0.5f,y+s*0.3f,x+s*0.2f,y+s*0.06f);fillPath(col);}
        else if(t=="DARK"){
            // Crescent moon: the moon circle minus the bite circle, as one outline.
            const float R0=s*0.44f,R1=s*0.36f,bx=cx+s*0.24f,by=cy-s*0.14f,d=std::hypot(bx-cx,by-cy),tb=std::atan2(by-cy,bx-cx);
            const float a=std::acos(std::clamp((d*d+R0*R0-R1*R1)/(2*d*R0),-1.f,1.f));
            const float p1x=cx+R0*std::cos(tb+a),p1y=cy+R0*std::sin(tb+a),p2x=cx+R0*std::cos(tb-a),p2y=cy+R0*std::sin(tb-a);
            float b1=std::atan2(p2y-by,p2x-bx),b2=std::atan2(p1y-by,p1x-bx);
            // The bite arc that runs through the moon's centre side.
            const float toward=tb+PI;auto inArc=[&](float from,float to,float q){float d1=to-from,dq=q-from;
                while(dq<0)dq+=2*PI;while(dq>=2*PI)dq-=2*PI;return d1>=0?dq<=d1:dq>=2*PI+d1;};
            if(b2<b1)b2+=2*PI;if(!inArc(b1,b2,toward))b2-=2*PI;
            list.PathArcTo(P(cx,cy),R0*u,tb+a,tb+2*PI-a,28);list.PathArcTo(P(bx,by),R1*u,b1,b2,20);fillPath(col);}
        else if(t=="STEEL"){list.AddCircleFilled(P(cx,cy),s*0.42f*u,im(col),24);list.AddCircleFilled(P(cx,cy),s*0.18f*u,im(0x66000000),16);}
        else if(t=="FAIRY")star(cx,cy,s*0.48f,col);
        else if(t=="SHADOW"){mv(cx,y+s*0.02f);cub(x+s*1.0f,y+s*0.36f,x+s*0.9f,y+s*0.98f,cx,y+s*0.98f);
            cub(x+s*0.1f,y+s*0.98f,x,y+s*0.36f,cx,y+s*0.02f);fillPath(col);
            if(col>>24==0xFF)list.AddCircleFilled(P(cx,y+s*0.64f),s*0.13f*u,im(SHADOW_LIGHT),16);}
        else star(cx,cy,s*0.44f,col);};
    auto typeColor=[](const std::string& t)->C{
        return t=="NORMAL"?0xFFACA47B:t=="FIRE"?0xFFF67B31:t=="WATER"?0xFF6A94EE:t=="ELECTRIC"?0xFFFFCD31:t=="GRASS"?0xFF7BC552:
            t=="ICE"?0xFF9CD5DE:t=="FIGHTING"?0xFFBD3131:t=="POISON"?0xFFA4419C:t=="GROUND"?0xFFE6BD6A:t=="FLYING"?0xFFAC94EE:
            t=="PSYCHIC"?0xFFEE628B:t=="BUG"?0xFFA4AC4A:t=="ROCK"?0xFFA4944A:t=="GHOST"?0xFF735A94:t=="DRAGON"?0xFF7B52EE:
            t=="DARK"?0xFF735A4A:t=="STEEL"?0xFFBDBDCD:t=="FAIRY"?0xFFEE99AC:t=="SHADOW"?0xFF5A2E78:0xFF6AC594;};
    auto typeTag=[xg](const std::string& t)->std::string{
        if (xg && !t.empty()) return t;
        return t=="PSYCHIC"?"PSYCHC":t=="ELECTRIC"?"ELECTR":t=="FIGHTING"?"FIGHT":t.empty()?"???":t;};

    // Night Colosseum backdrop (XdSkin.arena at rest): Cipher-violet sky, two spotlight cones, a teal
    // perspective floor grid, two elliptical ring marks, a horizon glow and a vignette.
    auto arena=[&](){
        const float w=320,h=H,hz=h*0.36f,cx=160;const S all=mk(0,0,w,h,0);
        const St sky[4]={{0,0xFF07060D},{0.36f,0xFF1E1233},{0.62f,0xFF130C22},{1,0xFF06050A}};
        const St xgSky[4]={{0,0xFF0C0611},{0.36f,0xFF32152F},{0.62f,0xFF241022},{1,0xFF0A050D}};
        slices(all,0,h,xg ? xgSky : sky,4,0,h);
        const C grid=xg ? 0xFFD880A2 : TEAL;
        const C ring=xg ? 0xFFE4517F : 0xFF9A5CFF;
        for(int i=0;i<2;i++){
            const float sx=i==0?w*0.18f:w*0.82f,sway=std::sin(i*2.1f)*w*0.06f,bx=cx+sway+(i==0?w*0.02f:-w*0.02f);
            quad(P(sx-6,0),P(sx+6,0),P(bx+w*0.12f,h),P(bx-w*0.12f,h),0x26E8E0FF,0x26E8E0FF,0x00E8E0FF,0x00E8E0FF);}
        for(int i=-12;i<=12;i++)list.AddLine(P(cx+i*3,hz),P(cx+i*w*0.16f,h),im(alpha(grid,0.34f-std::abs(i)*0.016f)),0.45f*u);
        for(float y=hz,step=1.6f;y<h;step*=1.22f,y+=step)list.AddLine(P(0,y),P(w,y),im(alpha(grid,std::min(0.32f,0.08f+(y-hz)/h*0.45f))),0.45f*u);
        const float ry0=h-h*0.3f,ry1=h+h*0.06f,ecx=cx,ecy=(ry0+ry1)/2;
        list.AddEllipse(P(ecx,ecy),ImVec2(w*0.42f*u,(ry1-ry0)/2*u),im(alpha(ring,0.5f)),0,96,0.9f*u);
        list.AddEllipse(P(ecx,ecy),ImVec2(w*0.32f*u,((ry1-ry0)/2-h*0.05f)*u),im(alpha(ring,0.333f)),0,96,0.9f*u);
        const St glow[3]={{0,0x006A3A86},{0.5f,0x2A6A3A86},{1,0x006A3A86}};slices(all,hz-10,hz+10,glow,3,hz-10,hz+10);
        radial(all,cx,h*0.5f,std::max(w,h)*0.75f,0x00000000,0x80000000,12,10);};

    auto titleSky=[&](){
        const float W=(float)width,Hp=(float)height;
        float sw=skyW>0?skyW:Hp*topAspect,sh=skyH>0?skyH:Hp;
        const float cover=std::max({1.f,W/sw,Hp/sh});sw*=cover;sh*=cover;
        const float x0=std::round((W-sw)/2),y0=std::round((Hp-sh)/2);
        list.AddRectFilled({0,0},{W,Hp},IM_COL32(0,0,0,255));
        list.AddImage(topFrame,{x0,y0},{x0+sw,y0+sh});};

    // Header plate (XdBoard.drawStrip): brushed steel ending in a porthole cap with a ball mark, the
    // party's ball pips and a tracked caption in the black inset. Captions are fixed screen names,
    // never the game's prompt, so nothing repeats the top screen.
    const bool battle=s.kind==XD_SCREEN_COMMAND||s.kind==XD_SCREEN_MOVES||s.kind==XD_SCREEN_TARGET;
    auto header=[&](const std::string& title){
        const float m=6,sl=m,st=4,sr=320-m-34,sb=28;
        steel(mk(sl-1,st-1.5f,320-4-(sl-1),sb-st+3,4),0);
        porthole(320-m-16,16,28,0);monSlot(320-m-16,16,19,false,false);
        well(mk(sl+3,st+2,sr-2-(sl+3),sb-st-4,3));
        const float d=8.2f,cy=(st+sb)/2;int shown=0;
        for(int i=0;i<6;i++){
            const XdPokemon& p=s.values.party[i];if(!p.species)continue;const float cx=sl+10+d/2+shown*10.4f;shown++;
            const bool fainted=p.max_hp>0&&p.hp==0;const std::string st=statusLabel(p);
            ball(cx,cy,d,fainted?0xFF5A606C:p.shadow_id?0xFF8A5CCF:!st.empty()?statusColor(st):0xFFE2463A,0xFF20242C);
            if(fainted)list.AddCircleFilled(P(cx,cy),d/2*u,im(0x80000000),20);
            if(battle&&s.values.actor_party_slot==i&&s.values.acting.species)list.AddCircle(P(cx,cy),(d/2+1.4f)*u,im(FOCUS),24,1.2f*u);
        }
        const float tx=shown?sl+76.25f:sl+10.25f,avail=sr-5-tx;float size=11.25f;const float track=0.14f;
        while(size>7&&tw(wide,title,size,track)>avail)size-=0.25f;
        draw(wide,fit(wide,title,size,avail,track),tx,cy+size*0.36f,size,0xFFDDF8F6,NAVY,1.4f,track,LEFT);};
    // Controller hints along the foot: D-pad, A and B caps in the pass 5b button finish.
    auto legend=[&](const char* a){
        const float cy=H-9.5f;float x=8;
        const C key=0xFF3A404C;
        gloss(mk(x,cy-4.5f,9,9,4.5f),UTILITY);
        list.AddRectFilled(P(x+1.8f,cy-0.9f),P(x+7.2f,cy+0.9f),im(key));list.AddRectFilled(P(x+3.6f,cy-2.7f),P(x+5.4f,cy+2.7f),im(key));x+=12;
        draw(font,"Select",x,cy+2.9f,8,TEXT_2,0xFF10131A,1.2f,0,LEFT);x+=tw(font,"Select",8,0)+8;
        for(int i=0;i<2;i++){
            hit(mk(x-2,cy-7,12+tw(font,i?"Back":a,8,0),14,0),i?XD_CANCEL:XD_CONFIRM,0);
            gloss(mk(x,cy-4.5f,9,9,4.5f),UTILITY);draw(font,i?"B":"A",x+4.5f,cy+2.4f,6.5f,key,0,0,0,CENTER);x+=12;
            const char* l=i?"Back":a;draw(font,l,x,cy+2.9f,8,TEXT_2,0xFF10131A,1.2f,0,LEFT);x+=tw(font,l,8,0)+8;}};

    if(s.title_sky&&topFrame&&!s.lower_owner)titleSky();
    else if(!s.values.battle&&!s.lower_owner){}
    else if(s.kind!=XD_SCREEN_FIELD&&!(s.kind==XD_SCREEN_DETAIL&&s.values.battle))arena();
    if(!s.lower_owner)return;
    const float top=34,gap=4;
    if(s.kind==XD_SCREEN_ROOT){
        // START: XD menu-card rows (XdKey.drawMenuTile): silver plate with corner rivets, the icon in a
        // chrome-rimmed colour disc, the caption in a black inset.
        header("MENU");
        const int n=(int)std::min<uint32_t>(s.values.choice_count,XD_MAX_CHOICES);
        const float rg=6,rh=n?std::min(35.f,(H-20-top-rg*(n-1))/n):0,y0=top+((H-20-top)-(n*rh+rg*std::max(0,n-1)))/2;
        for(int i=0;i<n;i++){
            const auto& c=s.values.choices[i];const bool sel=c.cursor==s.row||c.item_id==s.item;
            const S r=mk(14.5f,y0+i*(rh+rg),291,rh,4);choiceHit(r,c);
            steel(r,0);
            const float k=std::min(5.5f,rh/4),rv=1.5f;
            rivet(r.x+k,r.y+k,rv);rivet(r.x+r.w-k,r.y+k,rv);rivet(r.x+k,r.y+r.h-k,rv);rivet(r.x+r.w-k,r.y+r.h-k,rv);
            const std::string label=xdCaps(xdUtf8(c.label));auto has=[&](const char* t){return label.find(t)!=std::string::npos;};
            const char* g=has("POK")?"ball":has("DA")&&label[0]=='P'?"pda":has("ITEM")||has("BAG")?"bag":has("SAVE")?"save":
                has("EXIT")||has("CLOSE")||has("QUIT")?"exit":has("SETTING")||has("OPTION")?"game":"star";
            const C tone=!std::strcmp(g,"ball")?POKEMON:!std::strcmp(g,"pda")?PDA:!std::strcmp(g,"bag")?BAG:!std::strcmp(g,"save")?RUN:
                !std::strcmp(g,"exit")?CLOSE:UTILITY;
            const float d=std::min(r.h-8,30.f),cx=r.x+11+d/2,cy=r.y+r.h/2;
            gloss(mk(cx-d/2,cy-d/2,d,d,d/2),tone);
            const float gs=d*0.54f;glyph(g,cx-gs/2,cy-gs/2,gs,!std::strcmp(g,"ball")?0xFFE2463A:tone==UTILITY?0xFF3A404C:WHITE);
            const S wl=mk(cx+d/2+6,cy-(r.h/2-5),r.x+r.w-12-(cx+d/2+6),r.h-10,2.5f);well(wl);
            float size=std::min(12.5f,wl.h*0.56f);const float track=0.1f;
            while(size>8&&tw(wide,label,size,track)>wl.w-12)size-=0.25f;
            draw(wide,fit(wide,label,size,wl.w-12,track),wl.x+7,wl.y+wl.h/2+size*0.36f,size,c.enabled?WHITE:TEXT_3,NAVY,1.2f,track,LEFT);
            if(!c.enabled)veil(r);
            if(sel)focus(r);
        }
        legend("Open");
    }else if(s.kind==XD_SCREEN_BAG||s.kind==XD_SCREEN_OPTIONS){
        header(s.kind==XD_SCREEN_OPTIONS?"OPTIONS":s.page==2?"ITEM ACTION":"BAG");
        float y0=top;
        if(s.kind==XD_SCREEN_BAG){
            const float w=(291-4*3)/5;
            for(int i=0;i<5;i++){const S r=mk(14.5f+i*(w+3),y0,w,22,3);steel(r,0);
                const auto title=xdCaps(xdUtf8(s.tabs[i]));
                draw(font,fit(font,title,8.5f,w-6,0),r.x+r.w/2,r.y+14,8.5f,0xFF141414,0x66FFFFFF,1,0,CENTER);
                if(i!=s.tab)hit(r,i<s.tab?XD_LEFT:XD_RIGHT,0);if(i==s.tab)focus(r);
            }y0+=29;
        }
        int n=std::min<int>(s.values.choice_count,5);float rh=35,rg=6;
        if(s.kind==XD_SCREEN_OPTIONS)y0=top+((H-20-top)-(n*rh+std::max(0,n-1)*rg))/2;
        for(int i=0;i<n;i++){
            const auto& c=s.values.choices[i];const S r=mk(14.5f,y0+i*(rh+rg),291,rh,4);choiceHit(r,c);steel(r,0);
            for(float x:{r.x+5.5f,r.x+r.w-5.5f})for(float y:{r.y+5.5f,r.y+r.h-5.5f})rivet(x,y,1.5f);
            const float cx=r.x+22,cy=r.y+r.h/2;gloss(mk(cx-13.5f,cy-13.5f,27,27,13.5f),s.kind==XD_SCREEN_BAG?BAG:UTILITY);
            glyph(s.kind==XD_SCREEN_BAG?"bag":"game",cx-7,cy-7,14,WHITE);
            const S wl=mk(r.x+43,r.y+5,r.w-55,r.h-10,2.5f);well(wl);
            const auto text=xdUtf8(c.label);float avail=wl.w-14;
            if(s.kind==XD_SCREEN_BAG&&s.page!=2&&s.counts[i]>=0){char b[24];std::snprintf(b,sizeof b,"x%d",s.counts[i]);
                draw(font,b,wl.x+wl.w-6,cy+4.4f,12.5f,WHITE,0xFF000000,1.2f,0,RIGHT);avail-=36;}
            draw(wide,fit(wide,text,12.5f,avail,0.1f),wl.x+7,cy+4.5f,12.5f,WHITE,NAVY,1.2f,0.1f,LEFT);
            if(c.cursor==s.row)focus(r);
        }
        legend(s.kind==XD_SCREEN_OPTIONS?"Change":"Choose");
    }else if(s.kind==XD_SCREEN_DETAIL){
        // Complementary item/setting help owns the upper display only.
        const S r=mk(18.5f,s.values.battle?H*0.62f:H*0.06f,283,s.values.battle?H*0.35f:H*0.88f,7);steel(r,0);const S wl=ins(r,7);well(wl);
        const std::string help=xdUtf8(s.detail,2048);const float size=12.5f,lineH=15;
        float y=wl.y+15;std::string line,word;
        auto emit=[&](){draw(font,line,wl.x+10,y,size,WHITE,NAVY,1.2f,0,LEFT);y+=lineH;line.clear();};
        for(size_t i=0;i<=help.size();i++){char ch=i<help.size()?help[i]:' ';
            if(ch==' '||ch=='\n'){if(!word.empty()){auto next=line.empty()?word:line+" "+word;
                if(tw(font,next,size,0)>wl.w-20&&!line.empty())emit();
                if(!line.empty())line+=' ';line+=word;word.clear();}
                if(ch=='\n')emit();
            }else word+=ch;
        }if(!line.empty())emit();
    }else if(s.kind==XD_SCREEN_COMMAND){
        // Battle commands as XD machine plates in the 3DS colour code (XdKey.drawCapsule): Fight is the
        // hero plate with rivets and an emblem porthole; the rest share a row in Gen 6 order.
        header("CHOOSE A COMMAND");
        struct Cmd{const XdChoice* c;std::string label;C tone;const char* glyph;int rank;float weight;};
        Cmd cmds[4];int n=0;const Cmd* hero=nullptr;
        for(uint32_t i=0;i<s.values.choice_count&&i<XD_MAX_CHOICES&&n<4;i++){
            const auto& c=s.values.choices[i];
            if(c.item_id<0x12c||c.item_id>0x132||(c.item_id&1))continue;
            Cmd m{&c,xdCaps(xdUtf8(c.label)),UTILITY,"star",5,1};auto has=[&](const char* t){return m.label.find(t)!=std::string::npos;};
            if(has("FIGHT")){m.tone=FIGHT;m.glyph="fight";m.rank=-1;m.weight=1.25f;}
            else if(has("ITEM")||has("BAG")){m.tone=BAG;m.glyph="bag";m.rank=0;}
            else if(has("SNAG")){m.tone=SNAG;m.glyph="snag";m.rank=1;}
            else if(has("CALL")){m.tone=CALL;m.glyph="call";m.rank=2;}
            else if(has("RUN")||has("GIVE")){m.tone=RUN;m.glyph="run";m.rank=3;m.weight=0.8f;}
            else if(has("POK")){m.tone=POKEMON;m.glyph="ball";m.rank=4;}
            if(m.label.empty())m.label="\xE2\x80\xA6";
            cmds[n++]=m;
        }
        std::sort(cmds,cmds+n,[](const Cmd& a,const Cmd& b){return a.rank<b.rank;});
        if(n&&cmds[0].rank==-1)hero=&cmds[0];
        // Pass 5b sizes: a 283 x 139 Fight plate, then 75 x 43 row buttons (Run 57) 7.2 apart, the
        // block centred between the header and the controller legend.
        const float heroH=139,rowH=43,rowGap=12.4f,y0=top+std::max(0.f,((H-20-top)-(heroH+rowGap+rowH))/2),rowY=y0+heroH+rowGap;
        if(hero){
            const S r=mk(18.5f,y0,283,heroH,7);choiceHit(r,*hero->c);const C color=hero->c->enabled?hero->tone:0xFF4A4C54;
            steel(r,0);
            const S in=mk(r.x+7,r.y+7,r.w-14,r.h-14,4);inlay(in,color);
            const float k=3.6f,rv=2;
            rivet(r.x+14,r.y+k,rv);rivet(r.x+r.w-14,r.y+k,rv);rivet(r.x+14,r.y+r.h-k,rv);rivet(r.x+r.w-14,r.y+r.h-k,rv);
            rivet(r.x+k,r.y+r.h/2,rv);rivet(r.x+r.w-k,r.y+r.h/2,rv);
            const float d=std::min(in.h-14,74.f),cx=in.x+in.w-10-d/2,cy=in.y+in.h/2;
            porthole(cx,cy,d,0);
            disc(cx,cy,d/2-3,mix(0xFFFF8A6A,color,color==FIGHT?0:0.6f),shade(color==FIGHT?0xFF8E1A12:color,color==FIGHT?1:0.55f),cx-d*0.12f,cy-d*0.15f,d*0.45f);
            const float g=d*0.5f;glyph(hero->glyph,cx-g/2,cy-g/2+0.8f,g,0x99000000);glyph(hero->glyph,cx-g/2,cy-g/2,g,WHITE);
            const float lx=in.x+(cx-d/2-in.x)/2,av=cx-d/2-in.x-16;
            xdLabel(hero->label,lx,in.y+in.h/2-4,std::min(37.f,in.h*0.3f),14,av,0.16f,2.8f,CENTER);
            list.AddRectFilled(P(lx-av*0.32f,in.y+in.h/2+12),P(lx+av*0.32f,in.y+in.h/2+12.7f),im(0x66FFFFFF));
            if(!hero->c->enabled)veil(r);
            if(hero->c->item_id==s.item)cursor(r);
        }
        const int first=hero?1:0;const float rowSp=7.2f;float total=0;
        for(int i=first;i<n;i++)total+=75*(cmds[i].weight<1?0.76f:1.f);
        const float fitScale=std::min(1.f,(308-rowSp*std::max(0,n-first-1))/std::max(1.f,total));
        float x=160-(total*fitScale+rowSp*std::max(0,n-first-1))/2;
        for(int i=first;i<n;i++){
            const Cmd& m=cmds[i];const float w=75*(m.weight<1?0.76f:1.f)*fitScale;
            const S r=mk(x,rowY,w,rowH,5);choiceHit(r,*m.c);x+=w+rowSp;const C color=m.c->enabled?m.tone:0xFF4A4C54;
            steel(r,0);const S in=mk(r.x+4,r.y+4,r.w-8,r.h-8,3);inlay(in,color);
            auto icon=[&](float gx,float gy,float g){
                if(!std::strcmp(m.glyph,"ball")||!std::strcmp(m.glyph,"snag")){glyph(m.glyph,gx,gy,g,!std::strcmp(m.glyph,"snag")?0xFF1C8E8A:0xFFE2463A);return;}
                glyph(m.glyph,gx,gy+0.6f,g,shade(color,0.4f));glyph(m.glyph,gx,gy,g,WHITE);};
            // 5b's stacked button: icon over an 11-unit caption.
            const float g=std::min(in.h*0.42f,14.5f),t=in.y+in.h/2-(g+2+11)/2-0.5f;
            icon(in.x+in.w/2-g/2,t,g);
            xdLabel(m.label,in.x+in.w/2,t+g+2.5f+5.5f,11,7,in.w-5,0.1f,1.7f,CENTER);
            if(!m.c->enabled)veil(r);
            if(m.c->item_id==s.item)cursor(r);
        }
        legend("Choose");
    }else if(s.kind==XD_SCREEN_MOVES||s.kind==XD_SCREEN_SUMMARY){
        // Moves as XD metal battle plates (XdKey.drawMove): steel (Cipher purple for Shadow moves)
        // around a type-colour inlay, the type emblem in a porthole, XD's type pill, the name, the
        // game's own match result bottom left and PP in a black well bottom right.
        header(s.kind==XD_SCREEN_SUMMARY?"SUMMARY":"CHOOSE A MOVE");
        static const char* names[]={"NORMAL","FIGHTING","FLYING","POISON","GROUND","ROCK","BUG","GHOST","STEEL","???",
            "FIRE","WATER","GRASS","ELECTRIC","PSYCHIC","ICE","DRAGON","DARK","SHADOW"};
        const float w=(308-gap)/2,th=std::min(83.f,(H-20-top-gap)/2),gy=s.kind==XD_SCREEN_SUMMARY?91.f:top+((H-20-top)-(2*th+gap))/2;
        if(s.kind==XD_SCREEN_SUMMARY&&xdSummaryImage){
            const S portrait=mk(123,31,74,56,4);steel(portrait,0);well(ins(portrait,3));
            list.AddImage(xdSummaryImage,P(127,35),P(193,83),{0.174f,0.248f},{0.487f,0.640f});
        }
        for(uint32_t i=0;i<s.values.choice_count&&i<XD_MAX_CHOICES;i++){
            const auto& c=s.values.choices[i];
            if(c.item_id<0x110||c.item_id>0x116||(c.item_id&1))continue;
            const int slot=(c.item_id-0x110)/2;const bool sel=c.item_id==s.item||(s.kind==XD_SCREEN_SUMMARY&&c.cursor==s.row);
            const auto& m=s.values.moves[slot];
            std::string type=xdCaps(xdUtf8(s.values.move_type_names[slot]));
            if(type.empty()&&m.type>=0&&m.type<19)type=names[m.type];
            const bool shadow=type.find("SHADOW")!=std::string::npos;if(shadow)type="SHADOW";
            const bool grey=!c.enabled||(!shadow&&m.pp==0&&m.max_pp>0);
            const float px=6+(slot%2)*(w+gap),py=gy+(slot/2)*(th+gap);
            const S r=mk(px+1,py+1,w-2,th-3,5);if(s.kind==XD_SCREEN_MOVES)choiceHit(r,c);
            const C tc0=typeColor(type),base=light(tc0)?shade(tc0,0.78f):shade(tc0,0.92f);
            if(shadow&&!grey)aura(r);
            steel(r,shadow?CIPHER:0);
            const S in=mk(r.x+4,r.y+4,r.w-8,r.h-8,3);inlay(in,base);
            const float d=std::min(in.h*0.46f,34.f),cx=in.x+3+d/2,cy=in.y+3+d/2;
            porthole(cx,cy,d,shadow?CIPHER:0);
            const C tc=shadow?0xFFBD6ACD:tc0;
            disc(cx,cy,d/2-2.4f,mix(tc,WHITE,0.3f),shade(tc,0.62f),cx-d*0.12f,cy-d*0.14f,d*0.42f);
            const float gs=d*0.46f;typeGlyph(type,cx-gs/2,cy-gs/2+0.6f,gs,0x99000000);typeGlyph(type,cx-gs/2,cy-gs/2,gs,WHITE);
            const float x0=cx+d/2+5,x1=in.x+in.w-5;
            if(!type.empty()){const std::string tag=typeTag(type);pill(x0,in.y+4,pillWidth(tag,10),10,tc,tag);}
            std::string name=xdUtf8(c.label);if(name.empty())name="\xE2\x80\xA6";
            const float size=fitSize(font,name,std::min(17.f,in.h*0.24f),10,x1-x0);
            draw(font,fit(font,name,size,x1-x0,0),x0,in.y+4+10+3.5f+size*0.66f,size,WHITE,shade(base,0.28f),2,0,LEFT);
            const float by=in.y+in.h-4;float ppw=0;
            std::string pp;C ppCol=WHITE;
            if(shadow)pp="--/--";
            else if(m.pp>=0&&m.max_pp>=0){char b[24];std::snprintf(b,sizeof b,"%d/%d",m.pp,m.max_pp);pp=b;
                ppCol=m.pp==0?0xFFFF5A4A:m.pp*4<=m.max_pp?0xFFFFA23A:m.pp*2<=m.max_pp?0xFFFFE04A:WHITE;}
            if(!pp.empty()){
                ppw=tw(font,pp,11,0)+tw(font,"PP",7.5f,0)+13;const S wl=mk(in.x+in.w-4-ppw,by-13,ppw,13,2.5f);well(wl);
                draw(font,"PP",wl.x+4,wl.y+6.5f+2.6f,7.5f,0xFFFF6A3D,0,0,0,LEFT);
                draw(font,pp,wl.x+wl.w-4,wl.y+6.5f+3.6f,11,ppCol,0,0,0,RIGHT);}
            // Only the game's own match result; nothing is computed from a type chart here.
            const char* effect=nullptr;
            if(m.effectiveness_info==5)effect="No effect";
            else if(m.effectiveness_info==1&&m.effectiveness_num>=0&&m.effectiveness_den>0){
                const long a=m.effectiveness_num,b=m.effectiveness_den;
                effect=a==0?"No effect":a<b?(xg ? "Resisted" : "Not very effective"):a>b?"Super effective":"Effective";}
            if(effect){
                const std::string e=effect;const float mw=std::min(tw(font,e,8,0)+8,in.w-ppw-14);
                const S tg=mk(in.x+4,by-11.5f,mw,11,2);fillS(tg,0xB3000000);
                const C tone=e=="Super effective"?0xFFFFE04A:(e=="Not very effective"||e=="Resisted")?0xFFB8C8E8:e=="Effective"?WHITE:0xFFFFB0A8;
                draw(font,fit(font,e,8,mw-6,0),tg.x+4,tg.y+5.5f+2.9f,8,tone,0,0,0,LEFT);}
            if(grey)veil(r);
            if(sel)cursor(r);
        }
        legend("Choose");
    }else if(s.kind==XD_SCREEN_TARGET){
        // Target plates as XD battle plates (XdKey.drawTarget): maroon steel with rivets, the ball in a
        // porthole cap at the right end, the name in a black inset.
        header("CHOOSE A TARGET");
        const float w=(308-gap)/2,ph=std::min(83.f,(H-20-top-gap)/2),y0=top+((H-20-top)-(2*ph+gap))/2;
        for(uint32_t i=0;i<s.values.choice_count&&i<XD_MAX_CHOICES;i++){
            const auto& c=s.values.choices[i];
            if(c.item_id<0x138||c.item_id>0x13b)continue;
            // Native target positions and identity, even with duplicate names.
            const int positions[]={0,2,1,3};const int slot=positions[c.item_id-0x138];const bool sel=c.item_id==s.item;
            const S r=mk(6+(slot%2)*(w+gap)+1,y0+(slot/2)*(ph+gap)+1,w-2,ph-3,4.5f);choiceHit(r,c);
            steel(r,MAROON);
            rivet(r.x+3.2f,r.y+10,1.6f);rivet(r.x+3.2f,r.y+r.h-10,1.6f);
            const float d=std::min(r.h-10,40.f),cx=r.x+r.w-5-d/2,cy=r.y+r.h/2;
            porthole(cx,cy,d,MAROON);monSlot(cx,cy,d-9,false,false);
            const S wl=mk(r.x+5,r.y+5,cx-d/2-4-(r.x+5),r.h-10,2.5f);well(wl);
            const float x0=wl.x+6,x1=wl.x+wl.w-6;
            std::string name=xdUtf8(c.label);if(name.empty())name="\xE2\x80\xA6";
            const float size=fitSize(font,name,16,10,x1-x0);
            draw(font,fit(font,name,size,x1-x0,0),x0,wl.y+wl.h/2+size*0.3f,size,WHITE,0xFF000000,1.4f,0,LEFT);
            if(!c.enabled)veil(r);
            if(sel)cursor(r);
        }
        legend("Choose");
    }else if(s.kind==XD_SCREEN_PARTY){
        // Party plates laid out as the pass 5b switch screen (XdKey.drawTile): steel (Cipher purple for
        // Shadow, maroon when fainted), the ball in a porthole, name, status, HP and Lv in a black inset.
        header(s.values.battle?"SWITCH POK\xC3\x89MON":"POK\xC3\x89MON");
        const float g=6,cw=(308-g)/2,vg=7.5f,ch=std::min(63.f,(H-22-top-2*vg)/3),py0=top+((H-22-top)-(3*ch+2*vg))/2;
        for(int i=0;i<6;i++){
            const auto& p=s.values.party[i];const bool sel=s.party_slot==i;
            const S r=mk(6+(i%2)*(cw+g)+1,py0+(i/2)*(ch+vg)+1,cw-2,ch-3,4);
            hit(r,XD_PARTY_MEMBER,i,i,p.species&&(!(s.values.battle&&s.party_context==2)||(s.legal_mask&(1<<i))));
            if(!p.species){fillS(r,0x33000000);strokeS(r,0x33FFFFFF,0.6f);if(sel)focus(r);continue;}
            if(s.values.battle&&s.party_context==2&&(s.active_mask&(1<<i))){
                steel(r,0);porthole(r.x+22,r.y+r.h/2,36,0);monSlot(r.x+22,r.y+r.h/2,28,false,false);
                draw(font,"IN BATTLE",r.x+48,r.y+r.h/2+4,14.5f,TEXT_2,0xFF000000,1.2f,0,LEFT);
                if(sel)focus(r);continue;
            }
            const bool shadow=p.shadow_id!=0,fainted=p.max_hp>0&&p.hp==0;
            const C tint=shadow?CIPHER:fainted?MAROON:0;
            if(shadow)ring(r,alpha(SHADOW_AURA,0.9f),1.6f);
            steel(r,tint);
            const float d=std::min(r.h-8,36.f),cx=r.x+4+d/2,cy=r.y+r.h/2;
            porthole(cx,cy,d,tint);monSlot(cx,cy,d-8,shadow,fainted);
            const S wl=mk(cx+d/2+3,r.y+4,r.x+r.w-4-(cx+d/2+3),r.h-8,2.5f);well(wl);
            const float x0=wl.x+5,x1=wl.x+wl.w-5;
            const std::string status=statusLabel(p);const float bh=9,bw=badgeWidth(status,bh);
            std::string name=xdPartyName(xdUtf8(p.name));if(name.empty())name="POK\xC3\x89MON";
            const float nameW=x1-x0-(bw>0?bw+4:0),size=fitSize(font,name,14.5f,9,nameW),nt=wl.y+2.5f;
            draw(font,fit(font,name,size,nameW,0),x0,nt+size*0.7f,size,WHITE,0xFF000000,1.2f,0,LEFT);
            badge(status,x1-bw,nt+0.5f,bh);
            const float barTop=wl.y+wl.h*0.48f;
            if(p.max_hp>0)hpBar(x0,barTop,x1,4,p.hp,p.max_hp);
            const float ny=std::min(wl.y+wl.h-3,barTop+14);float nw=0;
            if(p.max_hp>0){char hp[24];std::snprintf(hp,sizeof hp,"%d/%d",p.hp,p.max_hp);nw=tw(font,hp,11.5f,0);
                draw(font,hp,x1,ny,11.5f,WHITE,0xFF000000,1.1f,0,RIGHT);}
            float lvLeft=x1-nw;
            if(s.levels[i]>0){char lv[16];std::snprintf(lv,sizeof lv,"Lv %d",s.levels[i]);
                const float lw=tw(font,lv,9,0);lvLeft=x1-nw-(nw>0?4:0)-lw;draw(font,lv,lvLeft,ny,9,TEXT_2,0xFF000000,1,0,LEFT);}
            if(shadow&&p.heart>=0&&p.heart_max>0){
                const float gx1=std::min(x0+48,lvLeft-6);
                if(gx1>x0+10)heartGauge(x0,ny-5,gx1-x0,2.8f,p.heart,p.heart_max);}
            if(sel)focus(r);
        }
        // Cancel, the menu's seventh native slot, as a small silver app plate in the foot.
        const S cr=mk(320-6-64,H-18,64,15,3);hit(cr,XD_CANCEL,0);steel(cr,0);
        draw(wide,"CANCEL",cr.x+cr.w/2,cr.y+cr.h/2+7.5f*0.36f,7.5f,0xFF141414,0x66FFFFFF,1,0.1f,CENTER);
        if(s.party_slot==6)focus(cr);
        legend("Choose");
    }
}

// Design pass 10: black over the whole lower screen while the top fades or cuts to black (light 0..1).
static void xdLowerFade(ImDrawList& list,int width,int height,float light){
    light=std::clamp(light,0.f,1.f);if(light>=0.999f)return;
    list.AddRectFilled({0,0},{(float)width,(float)height},IM_COL32(0,0,0,(int)std::lround((1-light)*255)));
}
