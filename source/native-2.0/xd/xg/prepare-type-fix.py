from pathlib import Path
source = Path("build/xd-mod-2026-11-21/upstream/wiids_companion/xd_effectiveness.cpp")
text = source.read_text()
text = text.replace('#include "xd_companion.h"', '#include "xd_companion.h"\n#include "xd_xg.h"')
text = text.replace('    const bool modified=xd::challenges::Active("gen4_rules");', '''    const bool xg = xd::xg::On();
    const bool modified = !xg && xd::challenges::Active("gen4_rules");
    // XG uses the original typeless slot for Fairy. Its disc supplies the chart.
    const auto hasType = [xg](uint16_t type) { return xg || type != 9; };''')
text = text.replace("if(type!=9&&match", "if(hasType(type)&&match")
text = text.replace("if(types[i]==9||(i==1&&types[0]==types[1]))continue;", "if(!hasType(types[i])||(i==1&&types[0]==types[1]))continue;")
text = text.replace('actor&&xd::challenges::Active("gen4_rules")', 'actor&&!xd::xg::On()&&xd::challenges::Active("gen4_rules")')
assert text.count("hasType") == 3
out = Path(__file__).resolve().parent / source.name
out.write_text(text, newline="\n")
