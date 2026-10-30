#include "netplay/game_menus.h"

#include "memory.h"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace GameMenus {
namespace {

// fManager_c::m_connectManage (a cTreeMg_c): the root of every object's connect node. A node is
// {parent, child, prev, next, owner}; the owner is an fBase_c whose profile name is at +8.
constexpr uint32_t kTreeRoot = 0x80377A10u;
constexpr uint16_t kProfileSelectPlayer = 0x2BC;
constexpr uint16_t kProfileFileSelect = 0x2D5;
constexpr uint16_t kProfileCourseSelectMenu = 0x2B5;  // the world map's + menu
constexpr uint16_t kProfilePauseWindow = 0x2C6;       // a level's pause window

// Current state IDs (sStateMgr's current pointer) and the fields read from each object.
constexpr uint32_t kSelectPlayerState = 0x9C;
constexpr uint32_t kSelectPlayerCursor = 0x26C;
constexpr uint32_t kFileSelectState = 0x238;
constexpr uint32_t kFileSelectCursor = 0x2A4;

// dSelectPlayer_c::StateID_* (d_basesNP .bss, fixed by the pre-linked layout).
constexpr uint32_t kSpStartWait = 0x809953C0u;
constexpr uint32_t kSpOnStage = 0x80995400u;
constexpr uint32_t kSpInfoOnStage = 0x80995440u;
constexpr uint32_t kSpButtonChange = 0x80995480u;
constexpr uint32_t kSpSelect = 0x809954C0u;
constexpr uint32_t kSpMultiSelect = 0x80995500u;
constexpr uint32_t kSpButtonAnime = 0x80995540u;
constexpr uint32_t kSpExit = 0x80995580u;
// dCourseSelectMenu_c's and Pausewindow_c's current StateID (it names itself at +4).
constexpr uint32_t kPauseMenuState = 0x234;
// dFileSelect_c::StateID_SelectWait.
constexpr uint32_t kFsSelectWait = 0x80992050u;

uint32_t Find(uint16_t profile) {
    // SPM has no NSMBW fManager/profile tree; never expose unrelated objects to pane writers.
    (void)profile;
    return 0;
#if 0
    uint32_t node = 0;
    if (!Memory::TryRead32(kTreeRoot, node)) {
        return 0;
    }
    for (int guard = 0; node != 0 && guard < 4096; ++guard) {
        uint32_t owner = 0;
        uint32_t word = 0;
        if (Memory::TryRead32(node + 0x10, owner) && owner != 0 && Memory::TryRead32(owner + 8, word) &&
            static_cast<uint16_t>(word >> 16) == profile) {
            return owner;
        }
        uint32_t child = 0;
        Memory::TryRead32(node + 4, child);
        if (child != 0) {
            node = child;
            continue;
        }
        while (node != 0) {
            uint32_t next = 0;
            Memory::TryRead32(node + 0xC, next);
            if (next != 0) {
                node = next;
                break;
            }
            uint32_t parent = 0;
            Memory::TryRead32(node, parent);
            node = parent;
        }
    }
    return 0;
#endif
}

} // namespace

bool HasProfile(uint16_t profile) {
    return Find(profile) != 0;
}

std::vector<uint16_t> Profiles() {
    return {};
#if 0
    std::vector<uint16_t> profiles;
    uint32_t node = 0;
    if (!Memory::TryRead32(kTreeRoot, node)) {
        return profiles;
    }
    for (int guard = 0; node != 0 && guard < 4096; ++guard) {
        uint32_t owner = 0;
        uint32_t word = 0;
        if (Memory::TryRead32(node + 0x10, owner) && owner != 0 && Memory::TryRead32(owner + 8, word)) {
            profiles.push_back(static_cast<uint16_t>(word >> 16));
        }
        uint32_t child = 0;
        Memory::TryRead32(node + 4, child);
        if (child != 0) {
            node = child;
            continue;
        }
        while (node != 0) {
            uint32_t next = 0;
            Memory::TryRead32(node + 0xC, next);
            if (next != 0) {
                node = next;
                break;
            }
            uint32_t parent = 0;
            Memory::TryRead32(node, parent);
            node = parent;
        }
    }
    std::sort(profiles.begin(), profiles.end());
    profiles.erase(std::unique(profiles.begin(), profiles.end()), profiles.end());
    return profiles;
#endif
}

SelectPlayers SelectPlayersState() {
    const uint32_t object = Find(kProfileSelectPlayer);
    uint32_t state = 0;
    if (object == 0 || !Memory::TryRead32(object + kSelectPlayerState, state)) {
        return SelectPlayers::Hidden;
    }
    switch (state) {
    case kSpOnStage:
    case kSpInfoOnStage:
        return SelectPlayers::Appearing;
    case kSpSelect:
    case kSpButtonChange:
        return SelectPlayers::Choosing;
    case kSpButtonAnime:
    case kSpExit:
        return SelectPlayers::Leaving;
    case kSpMultiSelect:
        return SelectPlayers::Other;
    case kSpStartWait:
    default:
        return SelectPlayers::Hidden;
    }
}

bool SelectPlayersSettled() {
    const uint32_t object = Find(kProfileSelectPlayer);
    uint32_t state = 0;
    return object != 0 && Memory::TryRead32(object + kSelectPlayerState, state) && state == kSpSelect;
}

int SelectPlayersCursor() {
    const uint32_t object = Find(kProfileSelectPlayer);
    uint32_t cursor = 0;
    if (object == 0 || !Memory::TryRead32(object + kSelectPlayerCursor, cursor) || cursor > 3) {
        return 0;
    }
    return static_cast<int>(cursor);
}

bool FileSelectWaiting() {
    const uint32_t object = Find(kProfileFileSelect);
    uint32_t state = 0;
    return object != 0 && Memory::TryRead32(object + kFileSelectState, state) && state == kFsSelectWait;
}

int FileSelectCursor() {
    const uint32_t object = Find(kProfileFileSelect);
    uint32_t cursor = 0;
    if (object == 0 || !Memory::TryRead32(object + kFileSelectCursor, cursor) || cursor > 2) {
        return 0;
    }
    return static_cast<int>(cursor);
}

int FileSelectRawCursor() {
    const uint32_t object = Find(kProfileFileSelect);
    uint32_t cursor = 0;
    if (object == 0 || !Memory::TryRead32(object + kFileSelectCursor, cursor)) {
        return -1;
    }
    return static_cast<int>(cursor);
}

// NSMBW_DUMP_STATES=2B5,2C6: every state machine field (a pointer to a StateID, which names
// itself through the string pointer at +4) in those objects, logged when it changes (finding
// which field says a menu is open).
std::string DumpStates() {
    static const std::vector<uint16_t> wanted = [] {
        std::vector<uint16_t> list;
        if (const char* value = std::getenv("NSMBW_DUMP_STATES")) {
            for (const char* c = value; *c != 0;) {
                char* end = nullptr;
                const unsigned long v = std::strtoul(c, &end, 16);
                if (end == c) break;
                list.push_back(static_cast<uint16_t>(v));
                c = *end == ',' ? end + 1 : end;
            }
        }
        return list;
    }();
    std::string text;
    for (const uint16_t profile : wanted) {
        const uint32_t object = Find(profile);
        char head[32];
        std::snprintf(head, sizeof(head), "[%03X %08X]", profile, object);
        text += head;
        for (uint32_t off = 0; object != 0 && off < 0x800; off += 4) {
            uint32_t id = 0, namePtr = 0;
            if (!Memory::TryRead32(object + off, id) || id < 0x80000000u || id >= 0x81800000u ||
                !Memory::TryRead32(id + 4, namePtr) || namePtr < 0x80000000u || namePtr >= 0x81800000u) {
                continue;
            }
            char name[96] = {};
            int n = 0;
            for (; n < 95; ++n) {
                uint32_t word = 0;
                if (!Memory::TryRead32((namePtr + n) & ~3u, word)) break;
                const char ch = static_cast<char>(word >> (24 - 8 * ((namePtr + n) & 3u)));
                if (ch == 0) break;
                if (ch < 32 || ch > 126) { n = 0; break; }
                name[n] = ch;
            }
            name[n] = 0;
            if (n > 8 && std::strstr(name, "StateID_") != nullptr) {
                char item[140];
                std::snprintf(item, sizeof(item), " +%X=%s", off, name);
                text += item;
            }
        }
    }
    return text;
}

// NSMBW_DUMP_OBJ=<profile>: log the words of that object's first 0x800 bytes that change (finding
// fields such as a menu's cursor).
void DumpObjectChanges() {
    static const int profile = [] {
        const char* value = std::getenv("NSMBW_DUMP_OBJ");
        return value != nullptr ? static_cast<int>(std::strtol(value, nullptr, 16)) : -1;
    }();
    if (profile < 0) {
        return;
    }
    static uint32_t lastObject = 0;
    static std::vector<uint32_t> last;
    const uint32_t object = Find(static_cast<uint16_t>(profile));
    if (object == 0) {
        lastObject = 0;
        return;
    }
    std::vector<uint32_t> now(0x200);
    for (uint32_t i = 0; i < now.size(); ++i) {
        Memory::TryRead32(object + i * 4, now[i]);
    }
    if (object == lastObject) {
        std::string text;
        for (uint32_t i = 0; i < now.size(); ++i) {
            if (now[i] != last[i] && now[i] < 0x10000 && last[i] < 0x10000) {
                char item[40];
                std::snprintf(item, sizeof(item), " +%X:%X->%X", i * 4, last[i], now[i]);
                text += item;
            }
        }
        if (!text.empty()) {
            std::fprintf(stderr, "[obj] %s\n", text.c_str());
        }
    }
    lastObject = object;
    last = std::move(now);
}

int PauseMenuKind() {
    DumpObjectChanges();
    // The state's name, by StateID address (they never move).
    static uint32_t knownOpen[4] = {}, knownClosed[16] = {};
    for (const uint16_t profile : {kProfileCourseSelectMenu, kProfilePauseWindow}) {
        const uint32_t object = Find(profile);
        uint32_t id = 0;
        if (object == 0 || !Memory::TryRead32(object + kPauseMenuState, id) || id == 0) {
            continue;
        }
        const int kind = profile == kProfileCourseSelectMenu ? 1 : 2;
        if (std::find(std::begin(knownOpen), std::end(knownOpen), id) != std::end(knownOpen)) {
            return kind;
        }
        if (std::find(std::begin(knownClosed), std::end(knownClosed), id) != std::end(knownClosed)) {
            continue;
        }
        uint32_t namePtr = 0;
        std::string name;
        if (Memory::TryRead32(id + 4, namePtr) && namePtr >= 0x80000000u && namePtr < 0x81800000u) {
            for (uint32_t n = 0; n < 96; ++n) {
                uint32_t word = 0;
                if (!Memory::TryRead32((namePtr + n) & ~3u, word)) break;
                const char ch = static_cast<char>(word >> (24 - 8 * ((namePtr + n) & 3u)));
                if (ch == 0) break;
                name += ch;
            }
        }
        // Waiting for a choice, or moving between choices: the menu is up and steady.
        const bool open = name.find("StateID_PauseDisp") != std::string::npos ||
                          name.find("StateID_ButtonChangeAnimeEndWait") != std::string::npos;
        uint32_t* list = open ? knownOpen : knownClosed;
        const size_t size = open ? std::size(knownOpen) : std::size(knownClosed);
        for (size_t i = 0; i < size; ++i) {
            if (list[i] == 0) {
                list[i] = id;
                break;
            }
        }
        if (open) {
            return kind;
        }
    }
    return 0;
}

bool PauseMenuOpen() {
    return PauseMenuKind() != 0;
}

uint32_t CourseSelectMenuObject() {
    return Find(kProfileCourseSelectMenu);
}

int CourseMenuCursor() {
    const uint32_t object = Find(kProfileCourseSelectMenu);
    uint32_t cursor = 0;
    if (object == 0 || !Memory::TryRead32(object + 0x268, cursor) || cursor > 3) {
        return -1;
    }
    return static_cast<int>(cursor);
}

uint32_t SelectCursorObject() {
    return Find(0x2CF);  // SELECT_CURSOR: the corner brackets the game's menus share
}

uint16_t CurrentScene() {
    return 0xFFFF;  // SPM seqdrv is not NSMBW's dScene profile ID.

}

} // namespace GameMenus
