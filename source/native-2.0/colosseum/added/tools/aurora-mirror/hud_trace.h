// Diagnostic for finding the guest code that draws one part of the battle HUD.
// Everything is off unless ColosseumGameActivity.setHudTrace / setHudSkip or
// the debug.wiids.colo.* properties turn it on, and nothing here acts outside
// a battle. Included by standalone.cpp only; all calls are on the guest thread
// except the setters, which only store atomics.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#define HUD_TRACE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "WiiDsColoHud", __VA_ARGS__)
#else
#define HUD_TRACE_LOG(...) (std::fprintf(stderr, "WiiDsColoHud: " __VA_ARGS__), std::fputc('\n', stderr))
#endif

namespace hud_trace {
constexpr int kDepth = 8;
constexpr uint32_t kBattleByte = 0x8047b5da;  // companion_story_battle
constexpr uint32_t kTextStart = 0x80003100, kTextEnd = 0x80266360;

inline std::atomic<int> mode{0};             // 1: table, 2: skip summary only, 3: table by queue-time caller
inline std::atomic<uint32_t> skip_start{0};  // guest return addresses in
inline std::atomic<uint32_t> skip_end{0};    // [start, end) drop the draw

// Experiment: drop replayed 2D draws (see kReplay) that lie wholly inside this
// rectangle, in percent of the frame. Packed x0, y0, x1, y1 bytes; 0 is off.
inline std::atomic<uint32_t> skip_rect{0};
// Every battle HUD draw of GC6E01 is issued while 0x800D461C replays the
// recorded 2D command buffer; this is its return address after the
// primitive-begin command.
constexpr uint32_t kReplay = 0x800d4678;

// The lower screen owns the player's status: owns() says so while it is
// requested and the battle feed has a player battler, and dropped() is told
// each time one of the original plate draws was withheld. Set by the host.
inline bool (*owns)() = nullptr;
inline void (*dropped)() = nullptr;
inline void (*party_screen)() = nullptr;   // the game's party screen was drawn
// The player's two plates and ball row in the authored 4:3 frame, in percent:
// x is taken back to 4:3 by the projection's own aspect, so the region is the
// same in 4:3, 16:9 and Auto. Measured on GC6E01 (plates 2.5-41, enemy side
// and the target cursor from 54).
constexpr float kPlayerPlates[4] = {-60.0f, 2.0f, 43.0f, 38.5f};  // left edge off-screen: the plates slide in from the left  // top 2: the EXP panel sits higher

// Experiment: drop replayed draws recorded by code whose return address is in
// [queue_skip_start, queue_skip_end).
inline std::atomic<uint32_t> queue_skip_start{0};
inline std::atomic<uint32_t> queue_skip_end{0};
inline std::atomic<uint32_t>& queue_skip_start_ref() { return queue_skip_start; }
inline bool diagnostics() {
  return mode.load(std::memory_order_relaxed) != 0 || skip_start.load(std::memory_order_relaxed) != 0 ||
         skip_rect.load(std::memory_order_relaxed) != 0 || queue_skip_start_ref().load(std::memory_order_relaxed) != 0;
}
// The same for the battle's input windows. An element's id is the menu item
// it was made from (0x802EF0A8); companion_state.cpp has the windows' code.
//   1 command window 0x4B: 175-184 (its four choices 181-184); the button
//     layout 0xF6: 4547-4557
//   2 move window 0x4C: 185-199 (acting name 193, PP 194, type 195, the four
//     moves 196-199); the alternative 0xF7 / 0xF8 frame: 4558-4568, and the
//     panels 0xF9 4569-4592, 0xFA 4593-4607 that 0xF7's callback opens
//   4 party screen 0x63: 938-951; its six slot panels 0x64-0x6A (shared ids):
//     952-971, 1346-1351, 4765-4773; the alternative layout's party panels
//     0xFC / 0xFD (opened by 0xF8's callback 0x8000ED34): 4616-4686
//   5 bag 0x59: 505-546
//   6 two-way choice 0xFB: 4608-4615
//   7 a party member's action list 0x6D: 985-990, 0x108: 4759-4764; the bag
//     item's action list 0x5A: 547-570
inline bool (*owns_menu)() = nullptr;
inline void (*menu_dropped)() = nullptr;
inline void (*menu_drawn)(int kind) = nullptr;   // the kind as menu_element gives it
inline bool (*menu_ready)(int kind) = nullptr;   // the feed reads that kind's open window completely
inline int menu_element(int id) {
  if ((id >= 175 && id <= 184) || (id >= 4547 && id <= 4557)) return 1;
  if ((id >= 185 && id <= 199) || (id >= 4558 && id <= 4607)) return 2;
  if ((id >= 938 && id <= 971) || (id >= 1346 && id <= 1351) || (id >= 4765 && id <= 4773) ||
      (id >= 4616 && id <= 4686))
    return 4;
  if (id >= 505 && id <= 546) return 5;
  if (id >= 4608 && id <= 4615) return 6;
  if ((id >= 985 && id <= 990) || (id >= 4759 && id <= 4764) || (id >= 547 && id <= 570)) return 7;
  return 0;
}
// Whether the upper copy of this element goes while the lower screen owns the
// input windows: the ordinary command and move windows always (as before);
// every other window only while the feed reads it, so a window the feed cannot
// read stays on the upper screen.
inline bool menu_withheld(int id) {
  const int kind = menu_element(id);
  if (!kind) return false;
  if (id >= 175 && id <= 199) return true;
  return menu_ready && menu_ready(kind);
}
// The windows that sit in the band at the bottom of the frame, whose band goes
// with them (see the band rule below). The party screen and the bag fill the
// frame and are not banded.
inline bool menu_banded(int id) {
  const int kind = menu_element(id);
  return kind == 1 || kind == 2 || kind == 6;
}
// The battle's own party screen, either layout (the readBattle party-screen
// flag): 0x63 with its panels and action lists, and 0xFC / 0xFD.
inline bool party_screen_element(int id) {
  const int kind = menu_element(id);
  return kind == 4 || (kind == 7 && !(id >= 547 && id <= 570));   // not the bag's action list
}

inline bool wanted() {
  return diagnostics() || (owns && owns()) || (owns_menu && owns_menu());
}

struct Position {  // where a vertex's position is, from the VCD and VAT
  uint32_t offset = 0, mode = 0, format = 0, count = 2, shift = 0, stride = 0;
  bool matrix_index = false;
};

// Who recorded a primitive into the game's 2D command buffer. GC6E01 draws
// every 2D window through 0x801028B0, which walks the window's element list
// and calls 0x80108C14(window, element) for each (return addresses 0x80102E28
// and 0x80102E48); that function saves the caller's r25-r31 at +20 of its
// frame, and the caller keeps the window in r28 and the element in r29. The
// element's id is the signed word at +6, the one its callback switches on.
// Text is batched and drawn later by 0x801C43F4, so it has no element.
struct Tag {
  std::array<uint32_t, kDepth> chain{};
  uint32_t window = 0, element = 0;
  int id = -1;
  bool text = false;
};

// The status plate laid out with its portrait on the right (elements 164-174
// and 1338-1345, one window instance per active battler) and the party-ball
// row above it (4522-4527) are the player's. The mirrored plate (153-163,
// 1330-1337) and the rows from x 501 (4534-4545) are the opponent's.
inline bool player_element(int id) {
  return (id >= 164 && id <= 174) || (id >= 1338 && id <= 1345) || (id >= 4522 && id <= 4527);
}

struct Entry {
  uint32_t window = 0;
  int id = -1;
  bool text = false;
  bool queued = false;
  std::array<uint32_t, kDepth> chain{};
  uint32_t lr = 0, texture = 0, width = 0, height = 0, draws = 0, vertices = 0;
  uint8_t command = 0, projection = '?';
  bool boxed = false, screen = false, printed = false;
  float raw[4]{}, pct[4]{};
  int last_box[4]{-9999, -9999, -9999, -9999};
};

struct State {
  const uint8_t* ram = nullptr;
  uint32_t ram_size = 0;
  const uint8_t* locked = nullptr;   // the locked cache at guest 0xE0000000
  uint32_t locked_size = 0;
  uint64_t observed = 0, unreadable = 0, frame_number = 0, party_frame = 0, menu_frame = 0, player_frame = 0, screen_frame = 0;
  uint32_t last_gs = 0;
  struct Plate { float x0, y0, x1, y1; uint64_t frame; int group; };
  std::vector<Plate> plates;         // where the player's plate sprites were withheld
  bool context = false, battle = false;
  uint32_t begin_sp = 0;
  std::array<uint32_t, kDepth> chain{};
  uint32_t lr = 0;
  float xf[0x100]{};
  bool xf_known[0x100]{};
  float projection[7]{};
  bool projection_known = false;
  uint32_t matrix_index = 0, texture = 0, width = 0, height = 0;
  std::unordered_map<uint64_t, Entry> table;
  std::unordered_set<uint32_t> enemy_windows;   // windows that drew an opponent plate element this battle
  std::map<uint32_t, Tag> tags;   // by the recorded command's address (ordered: a replay finds the recording it is in)
  bool tagging = false;           // the module reports recordings
  uint64_t recorded = 0, replayed = 0, untagged = 0;
  uint64_t skipped = 0, seen = 0, frames = 0;
  std::chrono::steady_clock::time_point next{};
  int reports = 0;
};
inline State state;

inline uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline float bef(const uint8_t* p) {
  const uint32_t bits = be32(p);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
// MEM1 through either of its mirrors, or the locked cache, where GC6E01 keeps
// its GS state block.
inline bool guest32(uint32_t address, uint32_t* out) {
  if (address >= 0xE0000000u) {
    const uint32_t offset = address - 0xE0000000u;
    if (!state.locked || state.locked_size < 4 || offset > state.locked_size - 4) return false;
    *out = be32(state.locked + offset);
    return true;
  }
  if (address < 0x80000000u || state.ram_size < 4) return false;
  const uint32_t offset = address & 0x3fffffffu;
  if (offset > state.ram_size - 4) return false;
  *out = be32(state.ram + offset);
  return true;
}

inline uint32_t parse_rect(const char* text) {
  unsigned v[4];
  if (std::sscanf(text, "%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3]) != 4 || v[2] > 100 || v[3] > 100 ||
      v[0] >= v[2] || v[1] >= v[3])
    return 0;
  return v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
}

// Reads the debug.wiids.colo.* properties. A property is applied when its
// text changes, so a value set through JNI stays until the property is edited.
inline void poll_properties() {
#ifdef __ANDROID__
  static char last[7][PROP_VALUE_MAX];
  static const char* const names[7] = {"debug.wiids.colo.hudtrace", "debug.wiids.colo.hudskip",
                                       "debug.wiids.colo.hudskipend", "debug.wiids.colo.hudskiprect",
                                       "debug.wiids.colo.queuetrace", "debug.wiids.colo.queueskip",
                                       "debug.wiids.colo.queueskipend"};
  bool changed = false;
  for (int i = 0; i < 7; ++i) {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(names[i], value);
    if (!std::strcmp(value, last[i])) continue;
    std::memcpy(last[i], value, sizeof value);
    changed = true;
    if (i == 0) mode = int(std::strtol(value, nullptr, 0));
    if (i == 1) skip_start = uint32_t(std::strtoul(value, nullptr, 16));
    if (i == 2) skip_end = uint32_t(std::strtoul(value, nullptr, 16));
    if (i == 3) skip_rect = parse_rect(value);
    if (i == 4) mode = std::strtol(value, nullptr, 0) ? 3 : 0;
    if (i == 5) queue_skip_start = uint32_t(std::strtoul(value, nullptr, 16));
    if (i == 6) queue_skip_end = uint32_t(std::strtoul(value, nullptr, 16));
  }
  if (changed)
    HUD_TRACE_LOG("set mode=%d skip=%08X end=%08X rect=%08X", mode.load(), skip_start.load(),
                  skip_end.load(), skip_rect.load());
#endif
}

inline void init(const uint8_t* ram, uint32_t ram_size, const uint8_t* locked = nullptr,
                 uint32_t locked_size = 0) {
  state.ram = ram;
  state.ram_size = ram_size;
  state.locked = locked;
  state.locked_size = locked_size;
  poll_properties();
}

// The guest is writing the first byte of a draw: remember who is drawing.
// The stack entries are the saved return addresses of the frames above the
// stack pointer, innermost first. lr is the link register at that moment,
// which is the direct caller only when the writing function made no call of
// its own before the write.
inline void begin(uint32_t lr, uint32_t sp) {
  state.context = true;
  state.begin_sp = sp;
  state.lr = lr;
  state.chain.fill(0);
  for (int level = 0; level < kDepth; ++level) {
    uint32_t next, ret;
    if (!guest32(sp, &next) || next <= sp || next - sp > 0x10000 || !guest32(next + 4, &ret))
      break;
    if (ret < kTextStart || ret >= kTextEnd)
      break;
    state.chain[level] = ret;
    sp = next;
  }
}

// Called by the module at every call of the GS command recorder 0x800D4F98
// (r3 = opcode), before it writes at the buffer's write pointer (+0x494 of
// the GS state at r13 - 23968). Opcode 2 is the primitive begin that
// 0x800D461C later replays through 0x800D67BC, possibly more than once, so
// the tag is kept by the command's address.
inline void queued(uint32_t opcode, uint32_t r13, uint32_t sp) {
  state.tagging = true;
  if (!wanted()) {
    if (!state.tags.empty()) state.tags.clear();
    return;
  }
  if (opcode != 2) return;
  ++state.observed;
  uint32_t gs = 0, command;
  if (!guest32(r13 - 23968u, &gs) || !guest32(gs + 0x494, &command)) {
    state.last_gs = gs;
    ++state.unreadable;
    return;
  }
  state.last_gs = gs;
  if (state.tags.size() >= 32768) state.tags.clear();
  Tag tag;
  for (int level = 0; level < kDepth + 4; ++level) {
    uint32_t next, ret;
    if (!guest32(sp, &next) || next <= sp || next - sp > 0x10000 || !guest32(next + 4, &ret))
      break;
    if (ret < kTextStart || ret >= kTextEnd)
      break;
    if (level < kDepth) tag.chain[level] = ret;
    if (ret >= 0x801c43f4u && ret < 0x801c4cb8u) tag.text = true;
    if (ret == 0x80102e28u || ret == 0x80102e48u) {
      uint32_t window, element, word;
      if (guest32(sp + 32, &window) && guest32(sp + 36, &element) && guest32(element + 4, &word)) {
        tag.window = window;
        tag.element = element;
        tag.id = int16_t(word & 0xffff);
      }
      break;
    }
    sp = next;
  }
  // The battle's own party screen: 0x63 (ordinary layout) or 0xFC / 0xFD
  // (alternative layout; their callbacks 0x8000EA10 and 0x8000E290 read each
  // member's HP). The old range 4629-4702 held only the latter, plus the
  // target windows' 0x125C-0x125E and unrelated 0xFE / 0xB9 items.
  const bool party_element = party_screen_element(tag.id);
  if (party_element) state.party_frame = state.frame_number + 1;
  if (party_element && party_screen) party_screen();
  if (menu_drawn && menu_element(tag.id)) menu_drawn(menu_element(tag.id));
  { static int qd = 0; if (qd < 12 && (state.recorded % 400) == 0) { ++qd; HUD_TRACE_LOG("queued cmd=%08X gs=%08X id=%d", command, gs, tag.id); } }
  state.tags[command] = tag;
  ++state.recorded;
}

inline void cp(uint8_t reg, uint32_t value) {
  if (reg == 0x30) state.matrix_index = value & 63;
}
inline void bp(uint32_t value) {
  const uint32_t reg = value >> 24;
  if (reg == 0x94) state.texture = 0x80000000u | ((value & 0xffffff) << 5);
  if (reg == 0x88) {
    state.width = (value & 0x3ff) + 1;
    state.height = ((value >> 10) & 0x3ff) + 1;
  }
}
inline void xf(const uint8_t* command) {  // 0x10, (count - 1) << 16 | address, values
  const uint32_t head = be32(command + 1), count = (head >> 16) + 1, base = head & 0xffff;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t address = base + i;
    if (address < 0x100) {
      state.xf[address] = bef(command + 5 + i * 4);
      state.xf_known[address] = true;
    } else if (address >= 0x1020 && address <= 0x1026) {
      if (address == 0x1026)
        state.projection[6] = float(be32(command + 5 + i * 4));
      else
        state.projection[address - 0x1020] = bef(command + 5 + i * 4);
      state.projection_known = true;
    }
  }
}
inline void xf_indexed(const uint8_t* command) {  // values come from an array: not tracked
  const uint32_t tail = uint32_t(command[3]) << 8 | command[4];
  const uint32_t count = (tail >> 12) + 1, base = tail & 0xfff;
  for (uint32_t i = 0; i < count; ++i)
    if (base + i < 0x100) state.xf_known[base + i] = false;
}

inline bool in_battle() {
  return state.ram && state.ram[kBattleByte - 0x80000000u] == 1;
}

inline int rounded(const Entry& entry, int i) {
  return entry.boxed ? int(std::lround(entry.screen ? entry.pct[i] : entry.raw[i])) : -9999;
}

struct Box {
  bool boxed = false, screen = false;
  float raw[4]{}, pct[4]{};
  float authored_x[2]{};  // pct[0] and pct[2] in the authored 4:3 frame
};

inline Box measure(const uint8_t* command, size_t length, const Position& layout) {
  Box box;
  const uint32_t vertices = uint32_t(command[1]) << 8 | command[2];
  const uint32_t component = layout.format <= 1 ? 1 : layout.format <= 3 ? 2 : 4;
  if (layout.mode != 1 || !vertices || layout.offset + layout.count * component > layout.stride ||
      3 + size_t(vertices) * layout.stride > length)
    return box;
  float raw[4] = {1e30f, 1e30f, -1e30f, -1e30f}, pct[4] = {1e30f, 1e30f, -1e30f, -1e30f};
  bool screen = state.projection_known;
  for (uint32_t v = 0; v < vertices; ++v) {
    const uint8_t* vertex = command + 3 + v * layout.stride;
    float p[3] = {0, 0, 0};
    const float scale = 1.0f / float(1u << layout.shift);
    for (uint32_t c = 0; c < layout.count; ++c) {
      const uint8_t* at = vertex + layout.offset + c * component;
      switch (layout.format) {
      case 0: p[c] = float(at[0]) * scale; break;
      case 1: p[c] = float(int8_t(at[0])) * scale; break;
      case 2: p[c] = float(uint16_t(at[0] << 8 | at[1])) * scale; break;
      case 3: p[c] = float(int16_t(at[0] << 8 | at[1])) * scale; break;
      default: p[c] = bef(at); break;
      }
    }
    raw[0] = std::min(raw[0], p[0]);
    raw[1] = std::min(raw[1], p[1]);
    raw[2] = std::max(raw[2], p[0]);
    raw[3] = std::max(raw[3], p[1]);
    const uint32_t matrix = (layout.matrix_index ? vertex[0] & 63u : state.matrix_index) * 4;
    for (uint32_t i = 0; screen && i < 12; ++i) screen = matrix + i < 0x100 && state.xf_known[matrix + i];
    if (!screen) continue;
    const float* m = state.xf + matrix;
    const float x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    const float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    const float z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
    const float* j = state.projection;
    float cx, cy;
    if (j[6] == 1) {
      cx = j[0] * x + j[1];
      cy = j[2] * y + j[3];
    } else if (z < 0) {
      cx = (j[0] * x + j[1] * z) / -z;
      cy = (j[2] * y + j[3] * z) / -z;
    } else {
      screen = false;
      continue;
    }
    const float sx = (cx + 1) * 50, sy = (1 - cy) * 50;
    pct[0] = std::min(pct[0], sx);
    pct[1] = std::min(pct[1], sy);
    pct[2] = std::max(pct[2], sx);
    pct[3] = std::max(pct[3], sy);
  }
  box.boxed = true;
  std::copy(raw, raw + 4, box.raw);
  const float* j = state.projection;
  if (screen && j[0] != 0 && j[2] != 0) {
    // The projection is wider than 4:3 by |j2 / j0| / (4 / 3).
    const float widen = std::fabs(j[2] / j[0]) * 0.75f;
    box.authored_x[0] = 50 + (pct[0] - 50) * widen;
    box.authored_x[1] = 50 + (pct[2] - 50) * widen;
  } else {
    screen = false;
  }
  box.screen = screen;
  if (screen) std::copy(pct, pct + 4, box.pct);
  return box;
}

// A whole draw command. Returns true when it must not reach the renderer.
inline bool draw(const uint8_t* command, size_t length, const Position& layout) {
  if (!state.context) return false;
  state.context = false;
  if (!in_battle()) return false;
  ++state.seen;
  bool skip = false;
  const uint32_t start = skip_start.load(std::memory_order_relaxed);
  if (start) {
    const uint32_t stored_end = skip_end.load(std::memory_order_relaxed);
    const uint32_t end = stored_end > start ? stored_end : start + 4;
    skip = state.lr >= start && state.lr < end;
    for (uint32_t ret : state.chain) skip = skip || (ret >= start && ret < end);
  }
  // A replayed primitive takes the tag of its recording. 0x800D67BC saved the
  // interpreter's cursor (r31, just past the begin command's two words) at
  // +12 of its frame, the one above GXBegin's.
  Tag tag;
  bool tagged = false;
  if (state.chain[1] == kReplay) {
    ++state.replayed;
    uint32_t frame, cursor, opcode;
    if (guest32(state.begin_sp, &frame) && guest32(frame + 12, &cursor)) {
      // The saved cursor lies inside (or at the end of) the primitive's recording, whose length varies with the
      // commands the interpreter reads before GXBegin; the recording is the last one that starts before it.
      auto found = state.tags.lower_bound(cursor);
      if (found != state.tags.begin()) {
        --found;
        const uint32_t distance = cursor - found->first;
        static int dd = 0;
        if (dd < 0) { ++dd; HUD_TRACE_LOG("tagmatch cursor=%08X key=%08X d=%u id=%d", cursor, found->first, distance, found->second.id); }
        if (distance <= 0x100) {
          tag = found->second;
          tagged = true;
        }
      }
      (void)opcode;
    }
    if (!tagged) {
      ++state.untagged;
      static int diag = 0;
      if (diag < 40 && (state.untagged % 500) == 1) {
        ++diag;
        uint32_t f = 0, cur = 0, op = 0, f2 = 0, c2 = 0;
        const bool a1 = guest32(state.begin_sp, &f), a2 = a1 && guest32(f + 12, &cur), a3 = a2 && guest32(cur - 8, &op);
        if (a1) guest32(f, &f2);
        if (f2) guest32(f2 + 12, &c2);
        uint32_t near_key = 0, best = 0xffffffffu;
        for (const auto& kv : state.tags) { uint32_t d = kv.first > cur ? kv.first - cur : cur - kv.first; if (d < best) { best = d; near_key = kv.first; } }
        HUD_TRACE_LOG("untag sp=%08X frame=%08X(%d) cursor=%08X(%d) op=%08X(%d) up=%08X c2=%08X chain=%08X,%08X,%08X lr=%08X tags=%zu near=%08X d=%u",
                      state.begin_sp, f, a1, cur, a2, op, a3, f2, c2, state.chain[0], state.chain[1], state.chain[2], state.lr,
                      state.tags.size(), near_key, best);
      }
    }
  }
  if (const uint32_t from = queue_skip_start.load(std::memory_order_relaxed); from && tagged && !skip) {
    const uint32_t stored = queue_skip_end.load(std::memory_order_relaxed);
    const uint32_t to = stored > from ? stored : from + 4;
    for (uint32_t ret : tag.chain) skip = skip || (ret >= from && ret < to);
  }
  const uint32_t rect = skip_rect.load(std::memory_order_relaxed);
  const int current_mode = mode.load(std::memory_order_relaxed);
  const bool by_queue = current_mode == 3;
  const bool table = current_mode == 1 || (by_queue && tagged);
  const bool plates_owned = owns && owns(), menu_owned = owns_menu && owns_menu();
  const bool owned = plates_owned || menu_owned;
  Box box;
  if (rect || table || owned) box = measure(command, length, layout);
  const bool replayed = box.screen &&
      std::find(state.chain.begin(), state.chain.end(), kReplay) != state.chain.end();
  if (rect && !skip && replayed)
    skip = box.pct[0] >= float(rect >> 24) && box.pct[1] >= float((rect >> 16) & 255) &&
           box.pct[2] <= float((rect >> 8) & 255) && box.pct[3] <= float(rect & 255);
  // The lower screen owns the player's status. A draw recorded for an element
  // is dropped exactly when that element belongs to the player's plates. Only
  // batched text and draws without a tag (a module without the observer) fall
  // back to the place the plates occupy.
  // A full-screen input window (party, bag, action list) owned below also draws parts outside the recorded 2D buffer
  // (its panel and some text go straight to the pipe); everything under an orthographic projection is 2D, so it goes.
  // Latched for 20 frames: the feed reads these windows on the guest thread and can miss a sample.
  if (menu_owned && menu_ready && (menu_ready(4) || menu_ready(5) || menu_ready(7))) state.screen_frame = state.frame_number + 1;
  const bool full_screen = menu_owned && state.screen_frame && state.frame_number <= state.screen_frame + 20;
  // The battle scene behind these windows is shown as one full-screen picture; that backdrop stays.
  const auto backdrop = [&](const Box& b) {
    return b.screen && b.pct[2] - b.pct[0] >= 90.0f && b.pct[3] - b.pct[1] >= 90.0f;
  };
  if (full_screen && state.chain[1] != kReplay &&
      state.projection_known && state.projection[6] == 1 && !backdrop(measure(command, length, layout))) {
    ++state.skipped;
    if (menu_dropped) menu_dropped();
    return true;
  }
  if (owned && state.chain[1] == kReplay) {
    int group = 0;   // 1 the player's plates, 2 the battle input windows
    // The party screen, bag and a member's action list cover the whole battle screen; while one of them is open
    // and its feed read it completely, the lower screen shows it, so every 2D draw goes (sprites and its text).
    if (full_screen && !backdrop(box)) group = 2;
    if (group) {
      // already decided: a full-screen input window
    } else if (tagged && tag.id >= 0) {
      if ((tag.id >= 153 && tag.id <= 163) || (tag.id >= 1330 && tag.id <= 1337) || (tag.id >= 4534 && tag.id <= 4545))
        state.enemy_windows.insert(tag.window);
      // Elements 164-174 are the solo plate layout, which close-ups and the EXP panel also use for the opponent
      // and over the message box, so an id alone never withholds: it only says the standard layout is up, and
      // the plates' own area decides below.
      if (player_element(tag.id)) state.player_frame = state.frame_number;
      // Element ids repeat between windows, so the plates' place decides too: a sprite inside the player's plate
      // area is the player's, except while the game's own party screen (which draws there) is open.
      const bool party_open = state.party_frame && state.frame_number < state.party_frame + 30;
      // Only while the standard layout is up (a known player element was just drawn): close-ups draw the
      // opponent's plate alone, in this area.
      const bool standard = state.player_frame && state.frame_number <= state.player_frame + 2;
      // The opponent's plates, standard or close-up, are always right of x 50, so a sprite whose centre lies in
      // the player's area is the player's (name and HP glyphs included).
      const float cx = (box.authored_x[0] + box.authored_x[1]) / 2, cy = (box.pct[1] + box.pct[3]) / 2;
      // Single battles build the player's plate from the party-slot panel elements (952-971, also kind 4); outside an
      // open party screen they count as plate parts here.
      const int as_menu = menu_element(tag.id);
      if (plates_owned && standard && !group && !party_open && replayed && (as_menu == 0 || as_menu >= 4) && !(tag.id >= 0x1258 && tag.id <= 0x125B) &&
          cx >= kPlayerPlates[0] && cy >= kPlayerPlates[1] && cx <= kPlayerPlates[2] && cy <= kPlayerPlates[3])
        group = 1;
      if (menu_owned && menu_withheld(tag.id)) group = 2;
      if (group == 2 && menu_banded(tag.id)) state.menu_frame = state.frame_number + 1;
      // Remember where the sprite was, for the text drawn over it.
      if (group && replayed && (group == 2 || tag.id < 4522) && state.plates.size() < 256)
        state.plates.push_back({box.authored_x[0], box.pct[1], box.authored_x[1], box.pct[3],
                                state.frame_number, group});
    } else if (state.tags.empty()) {
      // No recordings at all (a module without the observer): the plates'
      // fixed place. The input windows are never withheld without tags.
      if (plates_owned && replayed && box.authored_x[0] >= kPlayerPlates[0] && box.pct[1] >= kPlayerPlates[1] &&
          box.authored_x[1] <= kPlayerPlates[2] && box.pct[3] <= kPlayerPlates[3])
        group = 1;
    } else if (replayed) {
      // Batched text carries no element: it goes with a sprite withheld in
      // this frame or the last when it lies on it.
      const float x = (box.authored_x[0] + box.authored_x[1]) / 2, y = (box.pct[1] + box.pct[3]) / 2;
      // Text on a withheld player sprite goes only inside the plates' own area: the message box text that
      // passes over a sliding plate stays whole.
      const bool in_plate_area = x >= kPlayerPlates[0] && x <= kPlayerPlates[2] && y >= kPlayerPlates[1] && y <= kPlayerPlates[3];
      for (const auto& p : state.plates)
        if (state.frame_number - p.frame <= 1 && x >= p.x0 && x <= p.x1 && y >= p.y0 && y <= p.y1 &&
            (p.group == 1 ? plates_owned && in_plate_area : menu_owned))
          group = p.group;
      // The plates' name and HP text, also while the plate sprites are between layouts.
      const bool party_open = state.party_frame && state.frame_number < state.party_frame + 30;
      const bool standard_text = state.player_frame && state.frame_number <= state.player_frame + 2;
      if (!group && plates_owned && in_plate_area && !party_open && standard_text) group = 1;
    }
    // The input windows sit in the band at the bottom of the frame and nothing else is drawn there while one waits
    // for input (battle messages come between turns). While any of their elements is withheld, everything in
    // that band goes with them: the move names (batched text), the PP and type box and its frame (1358). Only the
    // banded windows (menu_banded) open the band; the full-frame party screen and bag do not.
    if (!group && menu_owned && replayed && state.menu_frame && state.frame_number <= state.menu_frame + 1) {
      const float cy = (box.pct[1] + box.pct[3]) / 2;
      if (cy >= 64.0f && box.pct[3] <= 99.0f) group = 2;
    }
    if (group) {
      skip = true;
      if (group == 1 && dropped) dropped();
      if (group == 2 && menu_dropped) menu_dropped();
    }
  }
  if (skip) ++state.skipped;
  if (!table) return skip;

  uint64_t key = state.texture * 0x9E3779B97F4A7C15ull + (by_queue ? 1 : state.lr);
  for (uint32_t ret : by_queue ? tag.chain : state.chain) key = (key ^ ret) * 0x100000001B3ull;
  if (by_queue) key = ((key ^ tag.window) * 0x100000001B3ull ^ uint32_t(tag.id)) * 0x100000001B3ull;
  if (state.table.size() >= 4096 && !state.table.count(key)) return skip;
  Entry& entry = state.table[key];
  if (!entry.draws) {
    entry.chain = by_queue ? tag.chain : state.chain;
    entry.window = tag.window;
    entry.id = tag.id;
    entry.text = tag.text;
    entry.queued = by_queue;
    entry.lr = state.lr;
    entry.texture = state.texture;
  }
  entry.width = state.width;
  entry.height = state.height;
  entry.command = command[0];
  ++entry.draws;
  entry.vertices += uint32_t(command[1]) << 8 | command[2];
  entry.boxed = box.boxed;
  entry.screen = box.screen;
  entry.projection = !state.projection_known ? '?' : state.projection[6] == 1 ? 'O' : 'P';
  if (box.boxed) std::copy(box.raw, box.raw + 4, entry.raw);
  if (box.screen) std::copy(box.pct, box.pct + 4, entry.pct);
  return skip;
}

// At each finished frame: at most once a second, the callers that drew.
inline void frame() {
  ++state.frame_number;
  std::erase_if(state.plates, [](const State::Plate& p) { return state.frame_number - p.frame > 1; });
  static auto next_poll = std::chrono::steady_clock::time_point{};
  if (const auto now = std::chrono::steady_clock::now(); now >= next_poll) {
    next_poll = now + std::chrono::seconds(1);
    poll_properties();
  }
  {  // per battle, also without diagnostics: windows are reallocated between battles
    static bool was_battle = false;
    const bool now_battle = in_battle();
    if (now_battle != was_battle) { was_battle = now_battle; state.enemy_windows.clear(); }
  }
  if (!diagnostics()) {
    state.frames = state.seen = state.skipped = state.recorded = state.replayed = state.untagged = 0;
    return;
  }
  const bool battle = in_battle();
  if (battle != state.battle) {
    state.battle = battle;
    state.table.clear();
    state.reports = 0;
    HUD_TRACE_LOG("%s", battle ? "battle start" : "battle end");
  }
  if (!battle) return;
  ++state.frames;
  const auto now = std::chrono::steady_clock::now();
  if (now < state.next) return;
  state.next = now + std::chrono::seconds(1);
  HUD_TRACE_LOG("t frames=%llu draws=%llu skipped=%llu callers=%zu skip=%08X end=%08X rect=%08X recorded=%llu replayed=%llu untagged=%llu pending=%zu observed=%llu unreadable=%llu gs=%08X plates=%zu",
                (unsigned long long)state.frames, (unsigned long long)state.seen,
                (unsigned long long)state.skipped, state.table.size(), skip_start.load(), skip_end.load(), skip_rect.load(),
                (unsigned long long)state.recorded, (unsigned long long)state.replayed,
                (unsigned long long)state.untagged, state.tags.size(), (unsigned long long)state.observed,
                (unsigned long long)state.unreadable, state.last_gs, state.plates.size());
  state.observed = state.unreadable = 0;
  const uint64_t frames = std::max<uint64_t>(state.frames, 1);
  state.frames = state.seen = state.skipped = state.recorded = state.replayed = state.untagged = 0;
  if (mode.load(std::memory_order_relaxed) != 1 && mode.load(std::memory_order_relaxed) != 3) return;
  // A caller is printed when it is new or its rectangle moved, and everything
  // again every tenth report. At most 48 lines a second.
  const bool all = state.reports++ % 10 == 0;
  std::vector<Entry*> rows;
  for (auto& [key, entry] : state.table) {
    if (!entry.draws) continue;
    bool moved = false;
    for (int i = 0; i < 4; ++i) moved = moved || rounded(entry, i) != entry.last_box[i];
    if (all || !entry.printed || moved) rows.push_back(&entry);
  }
  std::sort(rows.begin(), rows.end(), [](const Entry* a, const Entry* b) { return a->draws > b->draws; });
  if (rows.size() > 48) rows.resize(48);
  for (Entry* e : rows) {
    char box[96] = "pct=?";
    if (e->boxed && e->screen)
      std::snprintf(box, sizeof box, "pct=%.0f,%.0f-%.0f,%.0f", e->pct[0], e->pct[1], e->pct[2], e->pct[3]);
    char raw[96] = "raw=?";
    if (e->boxed)
      std::snprintf(raw, sizeof raw, "raw=%.1f,%.1f-%.1f,%.1f", e->raw[0], e->raw[1], e->raw[2], e->raw[3]);
    if (e->queued) {
      HUD_TRACE_LOG("q n=%.1f/f v=%u cmd=%02X tex=%08X %ux%u %s %s el=%d%s win=%08X qra=%08X %08X %08X %08X %08X %08X %08X %08X",
                    double(e->draws) / double(frames), e->vertices / e->draws, e->command, e->texture, e->width,
                    e->height, box, raw, e->id, e->text ? " text" : "", e->window, e->chain[0], e->chain[1],
                    e->chain[2], e->chain[3], e->chain[4], e->chain[5], e->chain[6], e->chain[7]);
      e->printed = true;
      for (int i = 0; i < 4; ++i) e->last_box[i] = rounded(*e, i);
      continue;
    }
    HUD_TRACE_LOG("d n=%.1f/f v=%u cmd=%02X proj=%c tex=%08X %ux%u %s %s lr=%08X ra=%08X %08X %08X %08X %08X %08X %08X %08X",
                  double(e->draws) / double(frames), e->vertices / e->draws, e->command, e->projection,
                  e->texture, e->width, e->height, box, raw, e->lr, e->chain[0], e->chain[1], e->chain[2],
                  e->chain[3], e->chain[4], e->chain[5], e->chain[6], e->chain[7]);
    e->printed = true;
    for (int i = 0; i < 4; ++i) e->last_box[i] = rounded(*e, i);
  }
  for (auto& [key, entry] : state.table) entry.draws = entry.vertices = 0;
}
}  // namespace hud_trace
