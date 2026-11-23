from pathlib import Path

out = Path(__file__).resolve().parent
original = Path("build/xd-mod-2026-11-21/upstream/third_party/aurora/lib/gfx")

def edit(name, replacements):
    text = (original / name).read_text()
    for before, after in replacements:
        assert text.count(before) == 1, (name, before)
        text = text.replace(before, after)
    (out / name).write_text(text, newline="\n")

edit("recording.cpp", [])
edit("xd_lower_presenter.inc", [
    ('extern "C" bool PortMenu_Active(void);',
     'extern "C" bool PortMenu_Active(void);\nextern "C" int PortXD_IsXG(void);'),
    ('xdLowerPaint(list,s,width,height,top,topAspect,skyW,skyH);',
     'xdLowerPaint(list,s,width,height,top,topAspect,skyW,skyH,PortXD_IsXG()!=0);'),
    ('xdLowerPaint(list,s,width,height);list.PopClipRect();',
     'xdLowerPaint(list,s,width,height,0,16.f/9.f,0,0,PortXD_IsXG()!=0);list.PopClipRect();'),
])
edit("xd_lower_draw.inc", [
    ('float skyW=0,float skyH=0){', 'float skyW=0,float skyH=0,bool xg=false){'),
    ('auto typeTag=[](const std::string& t)->std::string{\n        return',
     'auto typeTag=[xg](const std::string& t)->std::string{\n        if (xg && !t.empty()) return t;\n        return'),
    ('const St sky[4]={{0,0xFF07060D},{0.36f,0xFF1E1233},{0.62f,0xFF130C22},{1,0xFF06050A}};slices(all,0,h,sky,4,0,h);',
     '''const St sky[4]={{0,0xFF07060D},{0.36f,0xFF1E1233},{0.62f,0xFF130C22},{1,0xFF06050A}};
        const St xgSky[4]={{0,0xFF0C0611},{0.36f,0xFF32152F},{0.62f,0xFF241022},{1,0xFF0A050D}};
        slices(all,0,h,xg ? xgSky : sky,4,0,h);
        const C grid=xg ? 0xFFD880A2 : TEAL;
        const C ring=xg ? 0xFFE4517F : 0xFF9A5CFF;'''),
    ('alpha(TEAL,0.34f-std::abs(i)*0.016f)', 'alpha(grid,0.34f-std::abs(i)*0.016f)'),
    ('alpha(TEAL,std::min(0.32f,0.08f+(y-hz)/h*0.45f))', 'alpha(grid,std::min(0.32f,0.08f+(y-hz)/h*0.45f))'),
    ('im(0x809A5CFF),0,96,0.9f*u', 'im(alpha(ring,0.5f)),0,96,0.9f*u'),
    ('im(0x559A5CFF),0,96,0.9f*u', 'im(alpha(ring,0.333f)),0,96,0.9f*u'),
    ('a<b?"Not very effective":a>b?', 'a<b?(xg ? "Resisted" : "Not very effective"):a>b?'),
    ('e=="Not very effective"?0xFFB8C8E8', '(e=="Not very effective"||e=="Resisted")?0xFFB8C8E8'),
])
