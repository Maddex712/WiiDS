// Scripted controller input. See input_script.h.
#include "input_script.h"
#include "runtime.h"
#include "gx/render.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct ScriptedPress { uint32_t at, dur; uint16_t buttons; int sx, sy; };
std::vector<ScriptedPress> g_script;
bool g_log;
}  // namespace

void input_script_init(const char* spec) {
    g_log = getenv("GCN_INPUT_LOG") != nullptr;
    if (!spec || !*spec) spec = getenv("GCN_INPUT");
    if (!spec || !*spec) return;
    std::string s = spec;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(pos, end - pos);
        pos = end + 1;
        size_t c1 = item.find(':'), c2 = item.rfind(':');
        if (c1 == std::string::npos || c2 == c1) continue;
        ScriptedPress p{(uint32_t)atoi(item.c_str()), (uint32_t)atoi(item.c_str() + c2 + 1), 0, 0, 0};
        std::string btns = item.substr(c1 + 1, c2 - c1 - 1);
        size_t bp = 0;
        while (bp <= btns.size()) {
            size_t be = btns.find('+', bp);
            if (be == std::string::npos) be = btns.size();
            std::string b = btns.substr(bp, be - bp);
            bp = be + 1;
            if (b == "A") p.buttons |= PAD_A; else if (b == "B") p.buttons |= PAD_B;
            else if (b == "X") p.buttons |= PAD_X; else if (b == "Y") p.buttons |= PAD_Y;
            else if (b == "Z") p.buttons |= PAD_Z; else if (b == "L") p.buttons |= PAD_L;
            else if (b == "R") p.buttons |= PAD_R; else if (b == "START") p.buttons |= PAD_START;
            else if (b == "UP") p.buttons |= PAD_UP; else if (b == "DOWN") p.buttons |= PAD_DOWN;
            else if (b == "LEFT") p.buttons |= PAD_LEFT; else if (b == "RIGHT") p.buttons |= PAD_RIGHT;
            // SL SR SU SD push the stick all the way (100); SR19 and the like push it that far.
            else if (b.size() >= 2 && b[0] == 'S' && strchr("LRUD", b[1])) {
                const int amount = b.size() > 2 ? atoi(b.c_str() + 2) : 100;
                if (b[1] == 'L') p.sx = -amount; else if (b[1] == 'R') p.sx = amount;
                else if (b[1] == 'U') p.sy = amount; else p.sy = -amount;
            }
        }
        g_script.push_back(p);
    }
    if (g_log) fprintf(stderr, "[input] %zu scripted press(es)\n", g_script.size());
}

void input_script_apply(PadState& p) {
    if (g_script.empty()) return;
    uint32_t frame = gx::g_frames_submitted.load();  // time base: presented game frames
    for (auto& s : g_script) {
        if (frame >= s.at && frame < s.at + s.dur) {
            if (g_log) fprintf(stderr, "[input] frame %u press %04X\n", frame, s.buttons);
            p.buttons |= s.buttons;
            if (s.sx) p.stick_x = (uint8_t)(128 + s.sx);
            if (s.sy) p.stick_y = (uint8_t)(128 + s.sy);
            if (s.buttons & PAD_L) p.trig_l = 255;
            if (s.buttons & PAD_R) p.trig_r = 255;
        }
    }
}
