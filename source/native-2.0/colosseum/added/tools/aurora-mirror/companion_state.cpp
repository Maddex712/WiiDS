#ifdef __ANDROID__
#include <android/log.h>
#endif
#include "companion_state.h"

#include <jni.h>

#include <aurora/aurora.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kMemoryBase = 0x80000000;
constexpr uint32_t kSavePointer = 0x8047ADB8;
constexpr uint32_t kSpeciesCount = 0x80478F90;
constexpr uint32_t kSpeciesTable = 0x80478F94;
constexpr uint32_t kMessageRoot = 0x80478B08;
constexpr uint32_t kBattleParty = 0x8046de3c;
constexpr uint32_t kBattleActors = 0x8046e928;
constexpr uint32_t kPartyRecordSize = 0x138;
constexpr uint32_t kBattleActorSize = 0x154;
constexpr int kPartySlots = 6;
constexpr int kShadowIds = 97;
constexpr int kMapSegmentLimit = 16384;

// GC6E01 battle layout, read from the generated code of the imported disc:
// 0x801F6B48 returns the battle root 0x8046DD90 and 0x801F6ADC its fight data
// at +0x14. 0x801F78D4 indexes two trainers of 0x28E4 bytes from +100, so the
// first trainer is 0x8046DE08. Within a trainer, 0x801FCE60 (field 69) gives
// six 0x154-byte battle Pokemon records from +0xB1C and 0x801FCE2C (field 70)
// two 0x6E0-byte active positions from +0x1314. 0x802062FC reads a position's
// record pointer at +4, 0x801FDB48 (field 204) the record's Pokemon at +4 and
// 0x801FDAFC (field 206) its signed index at +0x14C. kBattleParty and
// kBattleActors above are this trainer's +0x34 and +0xB1C + 4.
constexpr uint32_t kBattleTrainer = 0x8046de08;
constexpr uint32_t kBattleRecords = kBattleTrainer + 0xb1c;
constexpr uint32_t kBattleRecordSize = 0x154;
constexpr uint32_t kBattlePositions = kBattleTrainer + 0x1314;
constexpr uint32_t kBattlePositionSize = 0x6e0;
constexpr int kBattlePositionCount = 2;
// ColosseumState.readBattle layout. See companion_state.h.
constexpr int kBattleHeader = 6;
constexpr int kBattleFields = 8;
constexpr int kBattleBase = kBattleHeader + kBattlePositionCount * kBattleFields;  // version 1 length
constexpr int kBattleExp = kBattleBase;                               // two per position
constexpr int kBattleHeart = kBattleExp + 2 * kBattlePositionCount;   // one per position
constexpr int kBattlePartyScreen = kBattleHeart + kBattlePositionCount;
constexpr int kBattleInts = kBattlePartyScreen + 1;
constexpr int kUiInts = 6;
constexpr int kUiIntsMax = 11;
// Experience needed to reach a level: 0x801229F4 indexes this table of
// 101-word rows (0x8011CE74, 0x8011CE44) by the first byte of the species
// record (field 17) and the level. The row count is at r13 - 31936.
constexpr uint32_t kGrowthTable = 0x8035e940;
constexpr uint32_t kGrowthCount = 0x80478b60;
static_assert(kBattleParty == kBattleTrainer + 0x34 && kBattleActors == kBattleRecords + 4);

// Java asks for the player's status plates to move to the lower screen here.
// The renderer withholds the original plates while companion_lower_owns_status
// holds, and the upper copy counts as gone only while draws are being dropped.
std::atomic<bool> lower_status_requested{false};
std::atomic<bool> battle_feed_valid{false};
std::atomic<int64_t> last_plate_drop{-1};
std::atomic<int64_t> last_party_screen{-1};
// The battle's input windows: what the game drew, and whether the lower
// screen asked for them.
std::atomic<bool> lower_menu_requested{false};
std::atomic<int64_t> last_menu_drop{-1};
std::atomic<int64_t> last_menu_drawn[3]{{-1}, {-1}, {-1}};
constexpr int kMenuInts = 8;

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Guest {
public:
  Guest(const uint8_t* data, uint32_t size) : data(data), size(size) {}
  bool valid(uint32_t address, uint32_t length) const {
    return address >= kMemoryBase && address - kMemoryBase <= size &&
           length <= size - (address - kMemoryBase);
  }
  uint8_t u8(uint32_t address) const {
    return valid(address, 1) ? data[address - kMemoryBase] : 0;
  }
  uint16_t u16(uint32_t address) const {
    return uint16_t(u8(address) << 8 | u8(address + 1));
  }
  uint32_t u32(uint32_t address) const {
    return uint32_t(u16(address)) << 16 | u16(address + 2);
  }
  float f32(uint32_t address) const { return std::bit_cast<float>(u32(address)); }

private:
  const uint8_t* data;
  uint32_t size;
};

// The game's message lookup, 0x800FA280: the root at r13 - 0x7D18
// (kMessageRoot) lists groups by key >> 20, each a sorted table of
// {local id, offset} pairs from +0x10. Returns the text's address, or 0.
uint32_t message_address(const Guest& guest, uint32_t key) {
  const uint32_t root = guest.u32(kMessageRoot);
  if (!key || !guest.valid(root, 12))
    return 0;
  const uint32_t group = key >> 20;
  const uint32_t local = key & 0xfffff;
  uint32_t node = guest.u32(root + 8);
  for (int guard = 0; guard < 64 && guest.valid(node, 0x10); ++guard) {
    const uint32_t count = guest.u16(node + 4);
    if (guest.u16(node) == group && guest.valid(node, 0x10 + count * 8)) {
      uint32_t start = 0, end = count;
      while (start < end) {
        const uint32_t mid = start + (end - start) / 2;
        const uint32_t entry = node + 0x10 + mid * 8;
        const uint32_t candidate = guest.u32(entry);
        if (candidate == local) {
          const uint32_t offset = guest.u32(entry + 4);
          if (offset > 0x1800000 || !guest.valid(node, offset + 2))
            return 0;
          return node + offset;
        }
        if (candidate < local)
          start = mid + 1;
        else
          end = mid;
      }
    }
    node = guest.u32(node + 8);
  }
  return 0;
}

// Big-endian UTF-16 text as the game stores it, up to the first terminator
// or control code.
std::u16string guest_text(const Guest& guest, uint32_t address, uint32_t limit = 32) {
  std::u16string text;
  if (!guest.valid(address, 2))
    return text;
  for (uint32_t i = 0; i < limit; ++i) {
    const uint16_t code = guest.u16(address + i * 2);
    if (code < 32 || code >= 0xd800)
      break;
    text.push_back(char16_t(code));
  }
  return text;
}

std::u16string species_name(const Guest& guest, uint16_t species) {
  static std::unordered_map<uint16_t, std::u16string> names;
  if (const auto cached = names.find(species); cached != names.end())
    return cached->second;
  const uint32_t count_ptr = guest.u32(kSpeciesCount);
  const uint32_t table = guest.u32(kSpeciesTable);
  if (species == 0 || species >= guest.u32(count_ptr) || species > 1024 ||
      !guest.valid(table, (species + 1) * 0x11c))
    return {};
  const uint32_t key = guest.u32(table + species * 0x11c + 0x18);
  auto name = guest_text(guest, message_address(guest, key));
  if (!name.empty()) names.emplace(species, name);
  return name;
}

// Active Colosseum save and GSflag layouts from ModernGekko's shadow_tracker.
bool flag(const Guest& guest, uint16_t id) {
  const uint32_t definitions = guest.u32(0x80478f9c);
  const uint32_t banks = guest.u32(0x80478eec);
  if (!id || !guest.valid(definitions, (uint32_t(id) + 1) * 8) || !guest.valid(banks, 32))
    return false;
  const uint32_t definition = definitions + id * 8;
  const uint32_t bit = guest.u16(definition + 4);
  const uint32_t bank = banks + (guest.u8(definition) >> 6) * 8;
  const uint32_t bits = guest.u32(bank + 4);
  return (bit >> 5) < guest.u32(bank) && guest.valid(bits, ((bit >> 5) + 1) * 4) &&
         (guest.u32(bits + (bit >> 5) * 4) & (1u << (bit & 31)));
}

struct Point {
  float x, y, z;
};
using Map = std::vector<float>;

bool sane(Point value, float limit) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
         std::abs(value.x) < limit && std::abs(value.y) < limit && std::abs(value.z) < limit;
}

Point point(const Guest& guest, uint32_t address) {
  return {guest.f32(address), guest.f32(address + 4), guest.f32(address + 8)};
}

Point radians(Point value) {
  auto angle = [](float v) { return std::abs(v) > 6.2931853f ? v * 0.0174532925f : v; };
  return {angle(value.x), angle(value.y), angle(value.z)};
}

struct Transform {
  Point origin, scale, sin, cos;

  Point apply(Point p) const {
    p = {p.x * scale.x, p.y * scale.y, p.z * scale.z};
    p = {p.x, p.y * cos.x - p.z * sin.x, p.y * sin.x + p.z * cos.x};
    p = {p.x * cos.y + p.z * sin.y, p.y, -p.x * sin.y + p.z * cos.y};
    return {p.x * cos.z - p.y * sin.z + origin.x,
            p.x * sin.z + p.y * cos.z + origin.y, p.z + origin.z};
  }
};

// ModernGekko's GSColSysState descriptor/model layout, using only the
// walkable mesh's outer edges and solid collision edges for the lower map.
Map read_map(const Guest& guest, uint32_t mesh) {
  if (!guest.valid(mesh, 0x10))
    return {};
  const uint32_t descriptors = guest.u32(mesh), count = guest.u32(mesh + 4);
  if (descriptors != mesh + 0x10 || count == 0 || count > 256 ||
      !guest.valid(descriptors, count * 0x40))
    return {};
  std::map<std::array<int, 5>, unsigned> edges;
  unsigned budget = 32768;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t descriptor = descriptors + i * 0x40;
    const Point origin = point(guest, descriptor);
    const Point rotation = radians(point(guest, descriptor + 12));
    const Point scale = point(guest, descriptor + 24);
    if (!sane(origin, 100000) || !sane(rotation, 1000) || !sane(scale, 1000) ||
        scale.x == 0 || scale.y == 0 || scale.z == 0)
      continue;
    const Transform transform{origin, scale,
        {std::sin(rotation.x), std::sin(rotation.y), std::sin(rotation.z)},
        {std::cos(rotation.x), std::cos(rotation.y), std::cos(rotation.z)}};
    for (unsigned slot : {0, 1, 4}) {
      const uint32_t model = guest.u32(descriptor + 0x24 + slot * 4);
      if (model < descriptors + count * 0x40 || model - mesh >= 0x800000 ||
          !guest.valid(model, 8))
        continue;
      const uint32_t triangles = guest.u32(model), length = guest.u32(model + 4);
      if (length == 0 || length > 16384 || length > budget ||
          !guest.valid(triangles, length * 0x34))
        continue;
      budget -= length;
      for (uint32_t j = 0; j < length; ++j) {
        const uint32_t tri = triangles + j * 0x34;
        std::array<Point, 3> p;
        for (unsigned v = 0; v < 3; ++v)
          p[v] = transform.apply(point(guest, tri + v * 12));
        for (unsigned v = 0; v < 3; ++v) {
          const Point from = p[v], to = p[(v + 1) % 3];
          if (!sane(from, 100000) || !sane(to, 100000))
            continue;
          std::array<int, 2> a{int(std::lround(from.x * 10)), int(std::lround(from.z * 10))};
          std::array<int, 2> b{int(std::lround(to.x * 10)), int(std::lround(to.z * 10))};
          if (a == b) continue;
          if (b < a) std::swap(a, b);
          ++edges[{slot != 0, a[0], a[1], b[0], b[1]}];
        }
      }
    }
  }
  Map out;
  for (const auto& [edge, count] : edges) {
    if (edge[0] == 0 && count != 1)
      continue;
    if (out.size() == 4 * kMapSegmentLimit)
      return {};
    out.insert(out.end(), {edge[1] / 10.f, edge[2] / 10.f, edge[3] / 10.f, edge[4] / 10.f});
  }
  return out;
}

struct Snapshot {
  int scene = 0, room = 0, map_revision = 0;
  float x = 0, z = 0;
  std::string archive;
  std::array<std::u16string, kPartySlots + kShadowIds - 1> names;
  std::array<jint, 3 * kPartySlots> party_stats{};
  std::array<jint, kShadowIds - 1> shadow_status{};
  std::array<jint, kShadowIds - 1> shadow_heart{};
  std::array<jint, kBattleInts> battle{};
  std::array<std::u16string, kBattlePositionCount> battle_names;
  std::shared_ptr<const Map> map;
};

// The player's active battlers, from the positions the battle engine itself
// uses. A battler is reported only when its record pointer is one of the six
// records of the first trainer, its identity is in that trainer's party
// mirror and in the saved party, and its level and HP are in range. Nothing
// is read from the second trainer.
void read_battle(const Guest& guest, uint32_t save, Snapshot& out) {
  int count = 0;
  for (int position = 0; position < kBattlePositionCount; ++position) {
    const uint32_t record = guest.u32(kBattlePositions + position * kBattlePositionSize + 4);
    if (record < kBattleRecords || record >= kBattleRecords + kPartySlots * kBattleRecordSize ||
        (record - kBattleRecords) % kBattleRecordSize != 0)
      continue;
    // 0x802062FC, the engine's own test of a position, requires the record's
    // signed index (field 206) not to be negative. A record that only holds a
    // benched party member is therefore not an active battler.
    if (int16_t(guest.u16(record + 0x14c)) < 0)
      continue;
    const uint32_t mon = record + 4;
    const uint16_t species = guest.u16(mon);
    const uint32_t pid = guest.u32(mon + 4);
    const int level = guest.u8(mon + 0x60);
    const int hp = guest.u16(mon + 0x8a), max_hp = guest.u16(mon + 0x8c);
    if (!species || level < 1 || level > 100 || max_hp < 1 || max_hp > 999 || hp > max_hp)
      continue;
    int slot = -1;
    bool saved = false;
    for (int i = 0; i < kPartySlots; ++i) {
      const uint32_t mirror = kBattleParty + i * kPartyRecordSize;
      if (slot < 0 && guest.u16(mirror) == species && guest.u32(mirror + 4) == pid) slot = i;
      const uint32_t own = save + 0xa0 + i * kPartyRecordSize;
      saved = saved || (guest.u16(own) == species && guest.u32(own + 4) == pid);
    }
    auto name = species_name(guest, species);
    // The trainer's own party mirror already proves the battler is the player's. Battle Now and Colosseum battles
    // use preset or registered teams that are not in the story save, so the save match is not required.
    (void)saved;
    if (slot < 0 || name.empty())
      continue;
    // 0x8011B67C tests a status by comparing its id with this word (field
    // 124): 3 poison, 4 bad poison, 5 paralysis, 6 burn, 7 freeze, 8 sleep.
    const int status = guest.u16(mon + 0x64);
    const unsigned shadow_id = guest.u16(mon + 0xd8);
    const bool shadow = shadow_id && shadow_id < kShadowIds && out.shadow_status[shadow_id - 1] == 2;
    jint* fields = out.battle.data() + kBattleHeader + position * kBattleFields;
    fields[0] = 1;
    fields[1] = slot;
    fields[2] = species;
    fields[3] = level;
    fields[4] = hp;
    fields[5] = max_hp;
    fields[6] = status == 0 || (status >= 3 && status <= 8) ? status : -1;
    fields[7] = shadow;
    // The battle's experience code (0x802232F4) sends the status plate the
    // experience held minus the amount needed for the current level
    // (0x802238F8-0x80223904), and levels up when the total reaches the
    // amount for the next level (0x802237D4-0x802237E4).
    // The player's own Shadow Pokemon: the same gauge as the Shadow list.
    if (shadow) {
      const auto current = static_cast<int32_t>(guest.u32(mon + 0xdc));
      const int initial = guest.u16(save + 0x1b4ba + shadow_id * 0x18);
      if (current >= 0 && initial > 0 && guest.valid(save, 0x1b4ba + kShadowIds * 0x18))
        out.battle[kBattleHeart + position] = std::min(100, current / initial);
    }
    jint* exp = out.battle.data() + kBattleExp + position * 2;
    exp[0] = exp[1] = -1;
    const uint32_t rate = guest.u8(guest.u32(kSpeciesTable) + species * 0x11c);
    if (level == 100) {
      exp[0] = exp[1] = 0;
    } else if (rate < guest.u32(kGrowthCount) && guest.valid(kGrowthTable + rate * 404, 404)) {
      const uint32_t row = kGrowthTable + rate * 404;
      const uint32_t held = guest.u32(mon + 0x5c);
      const uint32_t from = guest.u32(row + level * 4), to = guest.u32(row + (level + 1) * 4);
      if (from < to && held >= from && held < to && to - from < 0x7fffffffu) {
        exp[0] = jint(held - from);
        exp[1] = jint(to - from);
      }
    }
    out.battle_names[position] = std::move(name);
    ++count;
  }
  out.battle[1] = count > 0;
  out.battle[4] = count;
}

std::mutex snapshot_lock;
std::shared_ptr<const Snapshot> published;

// The battle's input windows, read from the game's own window system.
//
// Windows: 0x80104704 finds an open window by menu id in the list whose head
// is at 0x80404AD8 (0x80404ACC + 0xC), linked through +0x10, the menu id at
// +4. A window waits for input until +0x98 (decided, 0x80102ED4) or +0x99
// (cancelled) is set (0x801045A8). Its runtime elements are listed from +0x1C
// through +0; an element's id is the halfword at +6 and its message, drawn as
// its text by 0x80108C14, the word at +0x4C (copied from the item at
// 0x80104C34). +0xB0 holds the window's copy of its parameter block
// (0x80103FE4, filled by the update callbacks 0x80010B30 and 0x800109A0).
// 0x801022B8 turns the cursor into the item under it: it counts the menu's
// enabled items (bit 0x80 of the item's first byte) from the menu's first item
// (+4 of the 0x1C-byte menu record at 0x802E2DB8, 0x8005DA18) along +0x18
// until the item with bit 0x40 (last), and stops at s8(+0x95) + s8(+0x94).
// Items are 0x1C bytes at 0x802EF0A8 (0x8005D934): +2 x, +4 y, +0x10 message,
// +0x14 callback, +0x18 next.
//
// Menus (all from the battle's own callers):
//   0x4B command window, items 0xAF-0xB8, opened by 0x80011A1C. Its choices
//        are 0xB5-0xB8; 0x80010B30 returns 0 for 0xB5, 2 for 0xB7, 3 for 0xB8
//        and 1 or 3 for 0xB6 as byte +0x16 of the parameter block is set or
//        not (0x801F1918: set unless floor status 0x22 is 1); +0..0x15 is the
//        acting Pokemon's name. 0xF6 replaces it when floor status 0x1A is
//        0x10 (0x801F18DC, byte +0x17).
//   0x4C move window, items 0xB9-0xC7, opened by 0x800117BC with the block
//        0x801FE3F8 fills: +0 the acting Pokemon's name, then four 12-byte
//        rows from +4 (0x801204A8): +0 move name, +4 type name (both text
//        addresses, 0x800FA280), +8 type icon (0x8010C46C), +0xA maximum PP,
//        +0xB PP; +0x40 the acting battle record. Rows 0xC4-0xC7 (0x80010588).
//        The PP (0x80010294) and type (0x8001047C) boxes show the row under the
//        cursor; for move 0x164 (Shadow Rush) they show message 0x2BE1 and no
//        type, for 0x165 (a sealed Shadow move) message 0x2B6D and icon 0x5D.
//        +0x9C: byte 0 is 1 while a row picked with the swap button waits for
//        its new place, byte 2 that row (0x80010844). 0xF7 replaces it when
//        byte +0x44 is set (the same floor status as 0xF6).
//   0xFF target window, items 0x1258-0x125B, opened by 0x80011288; they stand
//        for field positions 0x47, 0x48, 0x46 and 0x45 (0x800113BC). 0x104 is
//        opened by the same function instead when byte +0x21 is set.
//   0xFB is NOT the party screen: 0x80011A1C opens it after the command
//        window only when the result is 3 and byte +0x16 of the block is 0,
//        and reopens the command window when it returns item 0x1207 (4615)
//        (0x80011B2C). It is a two-way choice: items 0x1206 (x 51) and 0x1207
//        (x 148) are the enabled choices at y 413, their texts are items 0x1204
//        (message 0x67, x 63) and 0x1203 (message 0x66, x 160), 12 px right of
//        each, and 0x1205 (message 0x1A3) is the question above them.
//   0xF6 has no cursor: its input callback 0x8000FE38 maps three buttons
//        straight to results 0, 2 and 3 (window +0x80); the texts are items
//        0x11CC (0x13C), 0x11CB (0x13E) and 0x11CA (0x13F), the command
//        window's own messages.
//   0xF8 is opened by 0x800114A4 with the 0xF7 items. 0x80263BC8 sends
//        command result 2 there (0x80263DE4); in the ordinary layout the same
//        call runs the party screen (0x8001BD80), and the 0xF8 update callback
//        0x8000ED34 opens 0xFC / 0xFD, whose rows (callbacks 0x8000EA10,
//        0x8000E290) read each member: 0xF8 is most likely that layout's party
//        screen, not a move list. It keeps kind 2 for compatibility.
//
// The ordinary party screen (command result 2, 0x80263DE4 -> 0x800114A4 ->
// 0x8001BD80 -> 0x8001BAC4 mode 2 -> 0x8001B1EC):
//   0x63 the party screen, opened at 0x8001B51C. Its enabled items 0x3B0-0x3B6
//        are the six slots and CANCEL (0x3B6, 0x8001B54C), the cursor counted
//        the standard way (0x801022B8 at 0x8001B548; the navigation callback
//        0x8001AB70 keeps it in +0x95, 0-6). Slot s is drawn by child window
//        0x64-0x6A (table 0x802E4E58, 0x8001AF44) with item 0x3B0 + s. The
//        state block at 0x803A1D40: +4 layout (0 one active battler, 1 two),
//        +8 1 when the members are the battle's (modes 2 and 4, 0x8001BB40),
//        +0xC the trainer (0: 0x801F2A7C(0)), +0x15 which action list follows.
//        0x8001AB70 lets the cursor stop on slot s when 0x801F986C(trainer, s)
//        (field 69 of the trainer, the record at +0xB1C + s * 0x154, kept only
//        while 0x80206A04 accepts it: signed index +0x14C >= 0) holds a
//        Pokemon (field 204, +4) that 0x80123FBC accepts.
//   0x6D / 0x108 the member's action list, opened by 0x8010264C at 0x8001B5E0
//        / 0x8001B5FC (0x6D while +0x15 of the state is set). Three enabled
//        items; 0x8010264C returns the choice's place: 0 goes on to the switch
//        check 0x80010C98 (0x8001B634), 1 closes the party screen and goes on
//        (0x8001B660), 2 and -1 go back (0x8001B868).
//
// The bag (command result 1, 0x80264488 -> 0x80018F88 mode 1, then the bag
// scene 0x80018F30 -> 0x80018A68):
//   0x59 the bag, opened at 0x80018D60. Its six enabled items 0x20B-0x210 are
//        the pocket tabs; +0x95 is the pocket (0x800170A0) in the 76-byte table
//        at 0x80266918 (+4 the pocket type, -1 for all, +0x1C the cursor
//        memory slot, +0x20 the pocket's name). The item under the cursor is
//        the n-th filled slot of the pocket, n the sum of the two signed bytes
//        of the cursor memory halfword at 0x80404A98 + 2 * slot (0x80103E68,
//        0x80018D90-0x80018E3C). The bag is r13 - 25896 (0x8047A2F8; in a
//        battle the trainer + 4, 0x801FCEAC), the bag's mode r13 - 25920 (1 in
//        a battle). A pocket type picks its slot array (0x80129BC8 /
//        0x801297D8 -> 0x8012A5B0 -> the getters 0x8012AA9C-0x8012ABD4): four
//        bytes each, +0 the item id (0x80143C68), +2 the quantity (0x80143C50);
//        a slot is filled when 0x801429E8 accepts it (id below r13 - 31816 and
//        the item's name, field 1, not 0). r13 - 25912 is the place of the
//        item picked with the move button, -1 when none (0x80017150).
//   0x5A the chosen item's action list (USE / CANCEL in a battle), opened at
//        0x80018748 by 0x80018594 (from 0x800188E0 after 0x59 returns, bag
//        mode 1). Its parameter block (window +0x60, as the row callbacks
//        0x80014C38 / 0x80014BA0 read it): +4 the actions, 12 bytes each
//        (+0 the text's message, +4 the action, +8 flags; bit 0 grey), copied
//        by 0x80017CB8 from the pocket table (+0x24 + 8 * mode: the list, +0x28
//        + 8 * mode: their count), +8 their count. Row r is action r; the
//        cursor is +0x95 (0x80014BA0) and 0x801026A4 returns the row. A row
//        without an action (CANCEL, message 0x134) goes back to the bag
//        (0x800187A4); USE (0x131, 0x80013F80) runs the item's effect, which
//        asks for the Pokemon through 0x800141BC: the bag closes and, in a
//        battle, the party screen 0x63 opens in mode 4 (0x8001BD3C) with the
//        bag's trainer (r13 - 25900) as state +0xC, so +8 is 1 and the members
//        are the battle's, read as for a switch.
//   Items: 0x801440A0 maps an item id through the halfwords at 0x803681E8 to
//        a 40-byte record at 0x80363CE8 (count r13 - 31856); +0x10 is the name
//        (field 1, 0x80144088).
namespace menu_reader {
constexpr uint32_t kWindowList = 0x80404ad8;
constexpr uint32_t kMenuTable = 0x802e2db8, kMenuCount = 0x80478848, kMenuSize = 0x1c;
constexpr uint32_t kItemTable = 0x802ef0a8, kItemCount = 0x80478968, kItemSize = 0x1c;
// Moves: 0x8011CA34, r13 - 0x7A28 (pointer to the count) and r13 - 0x7A24
// (the 0x38-byte records; +0x20 the name message, field 1 of 0x8011BEB4).
constexpr uint32_t kMoveCountPointer = 0x80478df8, kMoveTable = 0x80478dfc, kMoveSize = 0x38;
// Types: 0x8010C46C, 0x2C-byte records at 0x8035B500, count at r13 - 0x7CE8;
// +2 the icon.
constexpr uint32_t kTypeTable = 0x8035b500, kTypeCount = 0x80478b38, kTypeSize = 0x2c;
constexpr uint16_t kShadowRush = 0x164, kSealedMove = 0x165, kSealedIcon = 0x5d;
constexpr uint32_t kShadowRushPp = 0x2be1, kSealedPp = 0x2b6d;
constexpr int kRows = 8;
// Menu ids.
constexpr int32_t kCommand = 0x4b, kCommandButtons = 0xf6, kMoves = 0x4c;
constexpr int32_t kChoice = 0xfb, kParty = 0x63, kBag = 0x59;
constexpr int32_t kPartyAction = 0x6d, kPartyActionAlt = 0x108, kBagAction = 0x5a;
// The party screen.
constexpr uint32_t kPartyState = 0x803a1d40;
constexpr int kPartyFirstItem = 0x3b0, kPartyCancelText = 0x3b7;
// The bag.
constexpr uint32_t kBagPointer = 0x8047a2f8, kBagMode = 0x8047a2e0, kBagMoving = 0x8047a2e8;
constexpr uint32_t kPocketTable = 0x80266918, kPocketSize = 76;
constexpr uint32_t kCursorMemory = 0x80404a98;
constexpr uint32_t kBagItemCount = 0x80478bd8, kItemRecordCount = 0x80478bb0;
constexpr uint32_t kItemIndex = 0x803681e8, kItemRecords = 0x80363ce8, kItemRecordSize = 40;
// The battle's choice window 0xFB.
constexpr int kChoiceYes = 0x1206, kChoiceNo = 0x1207;
// The message box: 0x80106D3C opens one of these menus by the box's type
// (the jump table at 0x8035B3C8) and 0x801069FC closes it through 0x80102568;
// only the message module (0x80105E5C-0x80106F80) opens them.
constexpr int32_t kMessageBoxMenus[] = {0x0f, 0x10, 0x40, 0x50, 0x51, 0xe6, 0xe8, 0x107, 0x10c};
}  // namespace menu_reader

// ColosseumState.readMenu layout, see companion_state.h.
constexpr int kMenuHeader = 12;
constexpr int kMenuRowInts = 10;
constexpr int kMenuIntsFull = kMenuHeader + menu_reader::kRows * kMenuRowInts;
constexpr int kMenuLabels = 1 + 2 * menu_reader::kRows;

struct MenuSnapshot {
  std::array<jint, kMenuIntsFull> ints{};
  std::array<std::u16string, kMenuLabels> labels;
  // Bit k: the window of kind k was read completely enough for the lower
  // screen to stand in for it (the renderer may then withhold its upper copy).
  unsigned ready = 0;
  bool operator==(const MenuSnapshot&) const = default;
};

std::mutex menu_lock;
std::shared_ptr<const MenuSnapshot> menu_published;
std::atomic<unsigned> menu_ready_mask{0};
std::atomic<bool> field_message_box{false};

int menu_kind(int32_t id) {
  switch (id) {
  case 0x4b: case 0xf6: return 1;
  case 0x4c: case 0xf7: case 0xf8: return 2;
  case 0xff: case 0x104: return 3;
  case 0x63: return 4;
  case 0x59: return 5;
  case 0xfb: return 6;
  case 0x6d: case 0x108: case 0x5a: return 7;
  default: return 0;
  }
}

uint32_t menu_item(const Guest& guest, int id) {
  if (id < 0 || uint32_t(id) >= guest.u32(menu_reader::kItemCount)) return 0;
  const uint32_t item = menu_reader::kItemTable + uint32_t(id) * menu_reader::kItemSize;
  return guest.valid(item, menu_reader::kItemSize) ? item : 0;
}

// The first item of menu id, as 0x8005DA18 picks the record.
int menu_first_item(const Guest& guest, int32_t id) {
  const uint32_t count = guest.u32(menu_reader::kMenuCount);
  const uint32_t index = id < 0 ? 0 : uint32_t(id) >= count ? 1 : uint32_t(id);
  const uint32_t record = menu_reader::kMenuTable + index * menu_reader::kMenuSize;
  return guest.valid(record, menu_reader::kMenuSize) ? int16_t(guest.u16(record + 4)) : -1;
}

uint32_t window_element(const Guest& guest, uint32_t window, int id) {
  uint32_t element = guest.u32(window + 0x1c);
  for (int guard = 0; guard < 256 && guest.valid(element, 0x50); ++guard) {
    if (int16_t(guest.u16(element + 6)) == id) return element;
    element = guest.u32(element);
  }
  return 0;
}

std::u16string element_label(const Guest& guest, uint32_t window, int id) {
  const uint32_t element = window_element(guest, window, id);
  return element ? guest_text(guest, message_address(guest, guest.u32(element + 0x4c))) : std::u16string();
}

uint32_t move_name_address(const Guest& guest, uint16_t move) {
  const uint32_t count_pointer = guest.u32(menu_reader::kMoveCountPointer);
  const uint32_t table = guest.u32(menu_reader::kMoveTable);
  if (!guest.valid(count_pointer, 4) || move >= guest.u32(count_pointer) ||
      !guest.valid(table + move * menu_reader::kMoveSize, menu_reader::kMoveSize))
    return 0;
  return message_address(guest, guest.u32(table + move * menu_reader::kMoveSize + 0x20));
}

int type_from_icon(const Guest& guest, uint16_t icon) {
  const uint32_t count = guest.u32(menu_reader::kTypeCount);
  if (!icon || count > 64 || !guest.valid(menu_reader::kTypeTable, count * menu_reader::kTypeSize)) return -1;
  int found = -1;
  for (uint32_t type = 0; type < count; ++type) {
    if (guest.u16(menu_reader::kTypeTable + type * menu_reader::kTypeSize + 2) != icon) continue;
    if (found >= 0) return -1;   // not unique
    found = int(type);
  }
  return found;
}

// The player's active battle record at field position p, as read_battle
// accepts it, or 0.
uint32_t player_record(const Guest& guest, int position) {
  const uint32_t record = guest.u32(kBattlePositions + position * kBattlePositionSize + 4);
  if (record < kBattleRecords || record >= kBattleRecords + kPartySlots * kBattleRecordSize ||
      (record - kBattleRecords) % kBattleRecordSize != 0 || int16_t(guest.u16(record + 0x14c)) < 0)
    return 0;
  return record;
}

// The open window of menu id, as 0x80104704 finds it, or 0.
uint32_t find_window(const Guest& guest, int32_t id) {
  uint32_t at = guest.u32(menu_reader::kWindowList);
  for (int guard = 0; guard < 64 && guest.valid(at, 0xb4); ++guard, at = guest.u32(at + 0x10))
    if (int32_t(guest.u32(at + 4)) == id) return at;
  return 0;
}

// The party screen's slot s in a battle, filled into row (see the readMenu
// layout, kind 4). Returns true when the slot holds a member.
bool party_member(const Guest& guest, int slot, jint* row, std::u16string& name, std::u16string& species_label) {
  using namespace menu_reader;
  // +0xC: the trainer (0: 0x801F2A7C(0), the player's), whose field 69
  // (0x801FCE60) is the record at +0xB1C + s * 0x154.
  uint32_t trainer = guest.u32(kPartyState + 0xc);
  if (!trainer) trainer = kBattleTrainer;
  if (guest.u32(kPartyState + 8) != 1 || !guest.valid(trainer, 0xb1c + kPartySlots * kBattleRecordSize))
    return false;
  const uint32_t record = trainer + 0xb1c + uint32_t(slot) * kBattleRecordSize;
  if (int16_t(guest.u16(record + 0x14c)) < 0) return false;   // 0x80206A04
  const uint32_t mon = record + 4;                              // field 204
  const uint16_t species = guest.u16(mon);
  const int level = guest.u8(mon + 0x60);
  const int hp = guest.u16(mon + 0x8a), max_hp = guest.u16(mon + 0x8c);
  if (!species || level < 1 || level > 100 || max_hp < 1 || max_hp > 999 || hp > max_hp) return false;
  species_label = species_name(guest, species);
  if (species_label.empty()) return false;
  bool active = false;
  for (int p = 0; p < kBattlePositionCount; ++p) active = active || player_record(guest, p) == record;
  const int status = guest.u16(mon + 0x64);
  row[2] = slot;
  row[3] = species;
  row[4] = hp;
  row[5] = max_hp;
  row[6] = status == 0 || (status >= 3 && status <= 8) ? status : -1;
  row[9] = level | (active ? 0x100 : 0) | (hp == 0 ? 0x200 : 0);
  name = guest_text(guest, mon + 0x2e, 11);
  return true;
}

// The name of bag item id (0x801440A0, field 1), or 0.
uint32_t item_name(const Guest& guest, uint16_t id) {
  using namespace menu_reader;
  if (!id || id >= guest.u32(kBagItemCount)) return 0;
  const uint16_t index = guest.u16(kItemIndex + uint32_t(id) * 2);
  if (index >= guest.u32(kItemRecordCount)) return 0;
  return guest.u32(kItemRecords + uint32_t(index) * kItemRecordSize + 0x10);
}

// The slot array of a pocket type (0x80129BC8, 0x801297D8 -> 0x8012A5B0).
bool pocket_slots(int32_t type, uint32_t& offset, uint32_t& count) {
  switch (type) {
  case -1: offset = 2714; count = 10; return true;
  case 1: offset = 2172; count = 16; return true;
  case 2: offset = 1920; count = 20; return true;
  case 3: offset = 2492; count = 46; return true;
  case 4: offset = 2236; count = 64; return true;
  case 5: offset = 2000; count = 43; return true;
  case 6: offset = 2676; count = 3; return true;
  default: return false;
  }
}

// The bag window (kind 5): the rows around the item under the cursor.
bool read_bag(const Guest& guest, uint32_t window, MenuSnapshot& out) {
  using namespace menu_reader;
  jint* h = out.ints.data();
  const uint32_t bag = guest.u32(kBagPointer);
  const int pocket = int8_t(guest.u8(window + 0x95));
  if (guest.u32(kBagMode) != 1 || pocket < 0 || pocket > 5) return false;
  const uint32_t entry = kPocketTable + uint32_t(pocket) * kPocketSize;
  uint32_t offset = 0, capacity = 0;
  if (!pocket_slots(int32_t(guest.u32(entry + 4)), offset, capacity) || !guest.valid(bag + offset, capacity * 4))
    return false;
  int ordinal = -1;
  const uint32_t memory = guest.u32(entry + 0x1c) & 0xffff;
  if (memory < 12) {
    const uint16_t packed = guest.u16(kCursorMemory + memory * 2);
    ordinal = int(int8_t(packed >> 8)) + int(int8_t(packed & 0xff));
  }
  std::array<uint32_t, 64> filled{};
  int total = 0;
  for (uint32_t s = 0; s < capacity; ++s) {
    const uint32_t slot = bag + offset + s * 4;
    if (item_name(guest, guest.u16(slot))) filled[total++] = slot;
  }
  const int32_t moving = int32_t(guest.u32(kBagMoving));
  const int start = std::max(0, std::min(ordinal - 3, total - kRows));
  const int count = std::min(kRows, total - start);
  out.labels[0] = guest_text(guest, message_address(guest, guest.u32(entry + 0x20)));
  h[6] = count;
  h[9] = ordinal;
  h[11] = start;
  for (int r = 0; r < count; ++r) {
    jint* row = h + kMenuHeader + r * kMenuRowInts;
    const uint32_t slot = filled[start + r];
    const uint16_t id = guest.u16(slot);
    row[0] = id;
    row[1] = 1;
    row[2] = start + r;
    row[3] = pocket;
    row[4] = guest.u16(slot + 2);
    row[5] = total;
    row[6] = moving == start + r ? 1 : 0;
    out.labels[1 + 2 * r] = guest_text(guest, message_address(guest, item_name(guest, id)));
    if (start + r == ordinal) {
      h[7] = r;
      h[10] = id;
    }
  }
  return true;
}

void read_menu(const Guest& guest, MenuSnapshot& out) {
  using namespace menu_reader;
  jint* h = out.ints.data();
  std::fill(out.ints.begin(), out.ints.end(), -1);
  h[0] = 2;
  h[1] = 0;
  h[2] = 0;
  h[3] = 0;
  h[4] = 0;
  h[6] = 0;
  // The window waiting for input. When several wait, the inner one in the
  // battle's own order (target over party over moves over commands; a
  // choice or a party member's action list over the window that led to it).
  static constexpr int kPriority[8] = {0, 1, 2, 4, 3, 3, 5, 5};
  uint32_t window = 0;
  int kind = 0;
  uint32_t at = guest.u32(kWindowList);
  for (int guard = 0; guard < 64 && guest.valid(at, 0xb4); ++guard, at = guest.u32(at + 0x10)) {
    const int k = menu_kind(int32_t(guest.u32(at + 4)));
    if (!k || guest.u8(at + 0x98) || guest.u8(at + 0x99)) continue;
    if (!window || kPriority[k] > kPriority[kind]) {
      window = at;
      kind = k;
    }
  }
  if (!window) return;
  const int32_t id = int32_t(guest.u32(window + 4));
  const uint32_t block = guest.u32(window + 0xb0);
  const bool has_block = guest.valid(block, 0x48);
  h[1] = 1;
  h[2] = kind;
  h[8] = id;
  // The item under the cursor, exactly as 0x801022B8 finds it.
  const int ordinal = int(int8_t(guest.u8(window + 0x95))) + int(int8_t(guest.u8(window + 0x94)));
  h[9] = ordinal;
  int under = -1;
  {
    int item_id = menu_first_item(guest, id);
    for (int n = 0, guard = 0; guard < 256 && ordinal >= 0; ++guard) {
      const uint32_t item = menu_item(guest, item_id);
      if (!item) break;
      const uint8_t flags = guest.u8(item);
      if (flags & 0x80) {
        if (n == ordinal) {
          under = item_id;
          break;
        }
        ++n;
      }
      if (flags & 0x40) break;
      item_id = int16_t(guest.u16(item + 0x18));
    }
  }
  h[10] = under;

  if (id == kBagAction) {
    // The rows are the actions in the window's parameter block.
    h[7] = h[9] = h[10] = -1;
    const uint32_t block = guest.u32(window + 0x60);
    const uint32_t actions = guest.u32(block + 4);
    const uint32_t total = guest.u32(block + 8);
    if (!guest.valid(block, 12) || total > 5 || !guest.valid(actions, total * 12)) return;
    const int cursor = int8_t(guest.u8(window + 0x95));
    h[6] = int(total);
    h[9] = cursor;
    bool labelled = total > 0;
    for (int r = 0; r < int(total); ++r) {
      jint* row = h + kMenuHeader + r * kMenuRowInts;
      const uint32_t action = actions + uint32_t(r) * 12;
      row[1] = 1;
      row[2] = r;
      row[3] = guest.u32(action + 4) ? 1 : 0;
      row[6] = guest.u16(action + 8) & 1;
      out.labels[1 + 2 * r] = guest_text(guest, message_address(guest, guest.u32(action)));
      labelled = labelled && !out.labels[1 + 2 * r].empty();
      if (r == cursor) h[7] = r;
    }
    if (labelled) out.ready |= 1u << kind;
    // The bag stays open (decided) behind the list: the item it is for.
    if (const uint32_t bag = find_window(guest, kBag)) {
      MenuSnapshot scratch;
      std::fill(scratch.ints.begin(), scratch.ints.end(), -1);
      if (read_bag(guest, bag, scratch)) {
        h[11] = scratch.ints[10];
        out.ready |= 1u << 5;
      }
    }
    return;
  }
  if (id == kBag) {
    // The bag lists the pocket's items, not menu items.
    h[7] = h[9] = h[10] = -1;
    if (read_bag(guest, window, out)) out.ready |= 1u << kind;
    return;
  }

  std::array<int, kRows> rows{};
  int count = 0;
  auto fixed = [&](std::initializer_list<int> ids) {
    for (int item : ids) rows[count++] = item;
  };
  switch (id) {
  case 0x4b: fixed({0xb5, 0xb6, 0xb7, 0xb8}); break;
  case 0x4c: fixed({0xc4, 0xc5, 0xc6, 0xc7}); break;
  case 0xff: fixed({0x1258, 0x1259, 0x125a, 0x125b}); break;
  case 0x104: fixed({0x125c, 0x125d, 0x125e}); break;
  case kCommandButtons:
    // No cursor (0x8000FE38): each choice has its own button.
    fixed({0x11cc, 0x11cb, 0x11ca});
    h[9] = h[10] = -1;
    break;
  default: {
    // Other windows: their enabled items in menu order, the order the cursor
    // counts them in.
    int item_id = menu_first_item(guest, id);
    for (int guard = 0; guard < 256 && count < kRows; ++guard) {
      const uint32_t item = menu_item(guest, item_id);
      if (!item) break;
      const uint8_t flags = guest.u8(item);
      if (flags & 0x80) rows[count++] = item_id;
      if (flags & 0x40) break;
      item_id = int16_t(guest.u16(item + 0x18));
    }
  }
  }
  h[6] = count;

  // The special moves' names, to tell their rows apart as the PP and type
  // boxes do.
  const uint32_t shadow_rush = id == 0x4c ? move_name_address(guest, kShadowRush) : 0;
  const uint32_t sealed = id == 0x4c ? move_name_address(guest, kSealedMove) : 0;
  for (int r = 0; r < count; ++r) {
    jint* row = h + kMenuHeader + r * kMenuRowInts;
    const int item_id = rows[r];
    const uint32_t item = menu_item(guest, item_id);
    row[0] = item_id;
    row[1] = item ? (guest.u8(item) & 0x80 ? 1 : 0) : -1;
    if (item) {
      row[7] = int16_t(guest.u16(item + 2));
      row[8] = int16_t(guest.u16(item + 4));
    }
    if (item_id == under) h[7] = r;
    auto& label = out.labels[1 + 2 * r];
    auto& detail = out.labels[2 + 2 * r];
    if (id == 0x4b) {
      static constexpr int kCodes[4] = {0, -1, 2, 3};
      row[2] = r == 1 ? (has_block ? (guest.u8(block + 0x16) ? 1 : 3) : -1) : kCodes[r];
      label = element_label(guest, window, item_id);
    } else if (id == 0x4c) {
      row[2] = r;
      if (!has_block) continue;
      const uint32_t entry = block + 4 + uint32_t(r) * 12;
      const uint32_t name = guest.u32(entry);
      if (!name) {
        row[6] = -1;
        continue;
      }
      label = guest_text(guest, name);
      const int special = shadow_rush && name == shadow_rush ? 1 : sealed && name == sealed ? 2 : 0;
      row[6] = special;
      if (special == 1) {
        detail = guest_text(guest, message_address(guest, kShadowRushPp));
      } else if (special == 2) {
        row[3] = type_from_icon(guest, kSealedIcon);
        detail = guest_text(guest, message_address(guest, kSealedPp));
      } else {
        row[3] = type_from_icon(guest, guest.u16(entry + 8));
        row[4] = guest.u8(entry + 0xb);
        row[5] = guest.u8(entry + 0xa);
        detail = guest_text(guest, guest.u32(entry + 4));
      }
    } else if (id == 0xff) {
      static constexpr int kPositions[4] = {0x47, 0x48, 0x46, 0x45};
      row[2] = kPositions[r];
      // The Pokemon standing at that place: 0x47 / 0x48 the player's positions 0 / 1, 0x46 / 0x45 the
      // opponent's positions 0 / 1, in the order their plates stand. The opponent's trainer block is 0x5230
      // bytes after the player's (found on the Thor: its position records hold the foes' battle records,
      // same layout). Its nickname is +0x2E of the Pokemon (0x8011F4F0).
      const int code = kPositions[r];
      const uint32_t trainer = kBattleTrainer + (code >= 0x47 ? 0 : 0x5230);
      const int position = code == 0x47 || code == 0x46 ? 0 : 1;
      const uint32_t records = trainer + 0xb1c;
      const uint32_t record = guest.u32(trainer + 0x1314 + position * kBattlePositionSize + 4);
      if (record >= records && record < records + kPartySlots * kBattleRecordSize &&
          (record - records) % kBattleRecordSize == 0 && int16_t(guest.u16(record + 0x14c)) >= 0)
        label = guest_text(guest, record + 4 + 0x2e, 11);
    } else if (id == kCommandButtons) {
      static constexpr int kCodes[3] = {0, 2, 3};
      row[2] = kCodes[r];
      label = element_label(guest, window, item_id);
    } else if (id == kParty) {
      const int slot = item_id - kPartyFirstItem;
      if (slot >= 0 && slot < kPartySlots) {
        row[2] = slot;
        // The cursor stops only on a slot that holds a member (0x8001AB70).
        row[1] = party_member(guest, slot, row, label, detail) ? 1 : 0;
      } else {
        label = element_label(guest, window, kPartyCancelText);   // CANCEL (0x3B6)
      }
    } else if (id == kChoice) {
      // The text of each choice is the item 12 px to its right.
      row[2] = item_id == kChoiceYes ? 1 : item_id == kChoiceNo ? 0 : -1;
      label = element_label(guest, window, item_id == kChoiceYes ? 0x1204 : item_id == kChoiceNo ? 0x1203 : item_id);
    } else if (id == kPartyAction || id == kPartyActionAlt) {
      row[2] = r;
      label = element_label(guest, window, item_id);
    } else {
      label = element_label(guest, window, item_id);
    }
  }
  // Completeness, for the renderer.
  auto all_labelled = [&] {
    for (int r = 0; r < count; ++r)
      if (out.labels[1 + 2 * r].empty()) return false;
    return count > 0;
  };
  if (id == kCommand || id == kMoves) {
    out.ready |= 1u << kind;
  } else if (id == kParty) {
    bool any = false;
    for (int r = 0; r < count; ++r) any = any || (rows[r] != kPartyFirstItem + 6 && h[kMenuHeader + r * kMenuRowInts + 1] == 1);
    if (any) out.ready |= 1u << kind;
  } else if (id == kChoice) {
    out.labels[0] = element_label(guest, window, 0x1205);
    if (all_labelled()) out.ready |= 1u << kind;
  } else if (id == kPartyAction || id == kPartyActionAlt) {
    // The party screen stays open (decided) behind the list: the slot it was
    // decided on, and whether its members can be read too.
    if (const uint32_t party = find_window(guest, kParty)) {
      h[11] = int(int8_t(guest.u8(party + 0x95))) + int(int8_t(guest.u8(party + 0x94)));
      jint scratch[kMenuRowInts];
      std::u16string a, b;
      for (int s = 0; s < kPartySlots; ++s)
        if (party_member(guest, s, scratch, a, b)) {
          out.ready |= 1u << 4;
          break;
        }
    }
    if (all_labelled()) out.ready |= 1u << kind;
  }
  // The title: the acting Pokemon's name as the window shows it.
  if (has_block && (id == 0x4b || id == 0x4c)) {
    const std::u16string name = id == 0x4b ? guest_text(guest, block, 11) : guest_text(guest, guest.u32(block), 11);
    out.labels[0] = name;
    if (id == 0x4c) {
      const uint32_t record = guest.u32(block + 0x40);
      for (int p = 0; p < kBattlePositionCount; ++p)
        if (record && player_record(guest, p) == record) h[5] = p;
      if (guest.u8(window + 0x9c) == 1) h[11] = int8_t(guest.u8(window + 0x9e));
    } else if (!name.empty()) {
      // The command window keeps only a copy of the name: the position whose
      // nickname (+0x2E of the Pokemon, 0x8011F4F0) equals it, when unique.
      int match = -1, matches = 0;
      for (int p = 0; p < kBattlePositionCount; ++p) {
        const uint32_t record = player_record(guest, p);
        if (record && guest_text(guest, record + 4 + 0x2e, 11) == name) {
          match = p;
          ++matches;
        }
      }
      if (matches == 1) h[5] = match;
    }
  }
}
}  // namespace

std::atomic<uint64_t> ui_packed{0};
std::atomic<uint32_t> ui_sequence{0};

std::atomic<unsigned> ui_extra{0};
void companion_ui_extra(bool message_window, bool movie) {
  ui_extra.store(unsigned(message_window) | unsigned(movie) << 1, std::memory_order_relaxed);
}

// Who drives the field. The camera state (r13 - 0x7BE0) holds the mode byte
// at +0 that cameraSetMode (0x80177A44, also the script native at 0x802E1EA0)
// stores. A floor's start (0x80113C7C) aims the camera at the player
// (0x801779CC(0, 100)) in mode 0, and 0x80117164 sets the field camera byte
// 0x8047AD71. The field camera (0x801171C8, and 0x80177DB4 in cameraUpdate
// 0x80177A64) steers only while that byte is set and the mode is 0. Scripts
// clear the byte with the native 0x801174F4 (table 0x802E2350), set it again
// with 0x80117500 (0x802E2344) and switch to their own modes through
// cameraSetMode (cameraUpdate's table 0x8036C254: 3 eye and interest set by
// the caller, 4 left to an animation, 5, 6; 7 and 8 are the battle's). So a
// mode other than 0, or a cleared byte, is an event camera.
// Player movement is the script native 0x8012EBD4 (table 0x802E1E64). It
// returns before reading the stick while window 0x110 (r13 - 0x7FD8,
// 0x801337B0) or window 0xCA is open, in camera mode 6, while its delay
// counter 0x80426BD0 + 0x188 is positive and while a call is queued at
// + 0x18C. Scripts that do not call it at all are not visible here.
constexpr uint32_t kCameraState = 0x80478c40, kFieldCameraOn = 0x8047ad71;
constexpr uint32_t kMovementState = 0x80426bd0;
constexpr int32_t kMovementWindows[] = {0x110, 0xca};
std::atomic<unsigned> field_control{0};

void companion_field_control(const uint8_t* ram, uint32_t size, bool field, bool menu, bool message_window) {
  if (!field) {
    field_control.store(0, std::memory_order_relaxed);
    return;
  }
  const Guest guest(ram, size);
  const uint32_t camera = guest.u32(kCameraState);
  const bool event_camera =
      guest.valid(camera, 1) && (guest.u8(camera) != 0 || guest.u8(kFieldCameraOn) == 0);
  // 0x80102620 / 0x80104704: a window is open while its id is in the list.
  bool window = false;
  uint32_t at = guest.u32(menu_reader::kWindowList);
  for (int guard = 0; guard < 64 && guest.valid(at, 0x14) && !window; ++guard, at = guest.u32(at + 0x10))
    for (const int32_t id : kMovementWindows) window = window || int32_t(guest.u32(at + 4)) == id;
  const bool held =
      int32_t(guest.u32(kMovementState + 0x188)) > 0 || guest.u32(kMovementState + 0x18c) != 0;
  const bool no_control = event_camera || window || held || menu || message_window ||
                          field_message_box.load(std::memory_order_relaxed);
  field_control.store(unsigned(event_camera) | unsigned(no_control) << 1, std::memory_order_relaxed);
}

void companion_ui(bool menu, bool pda, bool start_menu, bool overlay, unsigned overlay_mode, unsigned message) {
  ui_packed.store(uint64_t(menu) | uint64_t(pda) << 1 | uint64_t(start_menu) << 2 | uint64_t(overlay) << 3 |
                  uint64_t(overlay_mode & 0xff) << 8 | uint64_t(message & 0xffff) << 16, std::memory_order_relaxed);
  ui_sequence.fetch_add(1, std::memory_order_relaxed);
}

bool companion_lower_owns_menu() {
  return lower_menu_requested.load(std::memory_order_relaxed) && battle_feed_valid.load(std::memory_order_relaxed);
}

bool companion_menu_ready(int kind) {
  return kind > 0 && kind < 32 && (menu_ready_mask.load(std::memory_order_relaxed) >> kind & 1);
}

void companion_menu_dropped() {
  last_menu_drop.store(now_ms(), std::memory_order_relaxed);
}

void companion_menu_drawn(int kind) {
  if (kind >= 1 && kind <= 2) last_menu_drawn[kind].store(now_ms(), std::memory_order_relaxed);
}

void companion_menu_sample(const uint8_t* ram, uint32_t size) {
  const Guest guest(ram, size);
  auto next = std::make_shared<MenuSnapshot>();
  const bool battle = companion_story_battle(ram, size);
  {
    // A message box is on screen while one of its menus is in the window
    // list (0x80104704), from 0x80106D3C's open to the end of 0x801069FC's
    // close. Outside battles only.
    bool box = false;
    uint32_t at = guest.u32(menu_reader::kWindowList);
    for (int guard = 0; guard < 64 && guest.valid(at, 0xb4) && !box; ++guard, at = guest.u32(at + 0x10))
      for (const int32_t id : menu_reader::kMessageBoxMenus) box = box || int32_t(guest.u32(at + 4)) == id;
    field_message_box.store(box && !battle, std::memory_order_relaxed);
  }
  if (battle) {
    read_menu(guest, *next);
  } else {
    std::fill(next->ints.begin(), next->ints.end(), -1);
    next->ints[0] = 2;
    next->ints[1] = next->ints[2] = next->ints[3] = next->ints[4] = next->ints[6] = 0;
  }
  static MenuSnapshot last;
  static jint revision = 0;
  static bool first = true;
  menu_ready_mask.store(next->ready, std::memory_order_relaxed);
  if (!first && *next == last) return;
  first = false;
  last = *next;
  next->ints[4] = ++revision;
  std::lock_guard lock(menu_lock);
  menu_published = std::move(next);
}

void companion_party_screen_drawn() {
  last_party_screen.store(now_ms(), std::memory_order_relaxed);
}

bool companion_lower_owns_status() {
  return lower_status_requested.load(std::memory_order_relaxed) &&
         battle_feed_valid.load(std::memory_order_relaxed);
}

void companion_player_plate_dropped() {
  last_plate_drop.store(now_ms(), std::memory_order_relaxed);
}

bool companion_story_battle(const uint8_t* ram, uint32_t size) {
  // GC6E01's battle-in-progress getter at 0x801EF63C loads this byte
  // from r13 - 0x5246 after startup establishes r13 = 0x80480820.
  // The game's encounter code writes it on entry and clears it on exit.
  return Guest(ram, size).u8(0x8047b5da) == 1;
}

void companion_publish(const uint8_t* ram, uint32_t size, int scene, int room,
                       std::string_view archive, uint32_t mesh, float x, float z) {
  const Guest guest(ram, size);
  static uint32_t last_mesh;
  static int last_room = -1, revision = 0, retry = 0;
  static auto geometry = std::make_shared<const Map>();
  if (scene == 2 && (room != last_room || mesh != last_mesh || (geometry->empty() && --retry <= 0))) {
    last_room = room;
    last_mesh = mesh;
    geometry = std::make_shared<const Map>(read_map(guest, mesh));
    ++revision;
    retry = 10;
  }
  auto next = std::make_shared<Snapshot>();
  next->scene = scene;
  next->room = room;
  next->archive = archive;
  next->map_revision = scene == 2 ? revision : 0;
  next->map = scene == 2 ? geometry : nullptr;
  next->x = x;
  next->z = z;

  const uint32_t save = guest.u32(kSavePointer);
  if (scene >= 2 && guest.valid(save, 0xa0 + kPartySlots * 0x138)) {
    // The battle party mirror uses party order. Battle actors use send-out
    // order, so match their current HP by Pokémon identity rather than slot.
    const bool battle_party = scene == 3 && guest.u16(kBattleParty) != 0;
    const uint32_t party = battle_party ? kBattleParty : save + 0xa0;
    next->shadow_heart.fill(-1);
    for (unsigned i = 0; i < kPartySlots; ++i) {
      const uint32_t mon = party + i * kPartyRecordSize;
      const uint16_t species = guest.u16(mon);
      next->names[i] = species_name(guest, species);
      if (next->names[i].empty()) continue;
      uint32_t stats = mon;
      const uint32_t pid = guest.u32(mon + 4);
      if (battle_party && pid) {
        for (unsigned slot = 0; slot < kPartySlots; ++slot) {
          const uint32_t actor = kBattleActors + slot * kBattleActorSize;
          const uint16_t actor_max = guest.u16(actor + 0x8c);
          if (guest.u16(actor) == species && guest.u32(actor + 4) == pid &&
              actor_max != 0 && guest.u16(actor + 0x8a) <= actor_max) {
            stats = actor;
            break;
          }
        }
      }
      // CK3 fields match the in-memory record. Hide uninitialised stats.
      const int level = guest.u8(stats + 0x60);
      const int hp = guest.u16(stats + 0x8a), max_hp = guest.u16(stats + 0x8c);
      if (level >= 1 && level <= 100) next->party_stats[i * 3] = level;
      if (hp <= max_hp && max_hp <= 999) {
        next->party_stats[i * 3 + 1] = hp;
        next->party_stats[i * 3 + 2] = max_hp;
      }
    }
    const uint32_t table = guest.u32(0x80478f6c);
    if (guest.valid(table, kShadowIds * 0x38)) {
      for (unsigned id = 1; id < kShadowIds; ++id) {
        const uint32_t row = table + id * 0x38;
        const auto species = guest.u16(row + 2);
        if (!species) continue;
        const int status = flag(guest, guest.u16(row + 0x14)) ? 3 :
                           flag(guest, guest.u16(row + 0x12)) ? 2 :
                           flag(guest, guest.u16(row + 0x16)) ? 1 : 0;
        next->shadow_status[id - 1] = status;
        if (status)
          next->names[kPartySlots + id - 1] = species_name(guest, species);
      }
      if (guest.valid(save, 0x1b4ba + kShadowIds * 0x18)) {
        auto read_heart = [&](uint32_t mon) {
          if (!guest.u16(mon)) return;
          const unsigned id = guest.u16(mon + 0xd8);
          if (!id || id >= kShadowIds || next->shadow_status[id - 1] != 2) return;
          const auto current = static_cast<int32_t>(guest.u32(mon + 0xdc));
          const int initial = guest.u16(save + 0x1b4ba + id * 0x18);
          if (current >= 0 && initial > 0)
            next->shadow_heart[id - 1] = std::min(100, current / initial);
        };
        for (unsigned box = 0; box < 3; ++box)
          for (unsigned slot = 0; slot < 30; ++slot)
            read_heart(save + 0xb9c + box * 0x24a4 + slot * 0x138);
        for (unsigned slot = 0; slot < kPartySlots; ++slot)
          read_heart(save + 0xa0 + slot * 0x138);
      }
    }
  }
  // The battle section needs the engine's own in-progress byte as well as
  // the presentation's battle scene, and the shadow table read above.
  std::fill(next->battle.begin() + kBattleBase, next->battle.end(), -1);
  if (scene == 3 && companion_story_battle(ram, size) && guest.valid(save, 0xa0 + kPartySlots * 0x138))
    read_battle(guest, save, *next);
  static std::array<jint, kBattleInts> last_battle{};
  static jint battle_revision = 0, sequence = 0;
  if (next->battle != last_battle) {
    last_battle = next->battle;
    ++battle_revision;
  }
  next->battle[0] = 1;
  next->battle[2] = battle_revision;
  battle_feed_valid.store(next->battle[1] == 1, std::memory_order_relaxed);
  next->battle[5] = ++sequence;
  std::lock_guard lock(snapshot_lock);
  published = std::move(next);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_colosseum_ColosseumState_readBattle(JNIEnv* env, jclass, jintArray ints, jobjectArray names) {
  std::shared_ptr<const Snapshot> frame;
  {
    std::lock_guard lock(snapshot_lock);
    frame = published;
  }
  if (!frame || !ints || !names || env->GetArrayLength(ints) < kBattleBase ||
      env->GetArrayLength(names) < kBattlePositionCount)
    return JNI_FALSE;
  const jsize length = std::min<jsize>(env->GetArrayLength(ints), kBattleInts);
  for (int i = 0; i < kBattlePositionCount; ++i) {
    const auto& name = frame->battle_names[i];
    const auto value = name.empty() ? nullptr :
        env->NewString(reinterpret_cast<const jchar*>(name.data()), name.size());
    if (!name.empty() && !value) return JNI_FALSE;
    env->SetObjectArrayElement(names, i, value);
    if (value) env->DeleteLocalRef(value);
  }
  auto values = frame->battle;
  const bool requested = lower_status_requested.load(std::memory_order_relaxed);
  const int64_t drop = last_plate_drop.load(std::memory_order_relaxed);
  const bool withheld = requested && values[1] == 1 && battle_feed_valid.load(std::memory_order_relaxed) &&
                        drop >= 0 && now_ms() - drop < 500;
  values[3] = (requested ? 1 : 0) | (withheld ? 2 : 0);
  const int64_t party = last_party_screen.load(std::memory_order_relaxed);
  values[kBattlePartyScreen] = party >= 0 && now_ms() - party < 500;
  env->SetIntArrayRegion(ints, 0, length, values.data());
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_colosseum_ColosseumGameActivity_setLowerOwnsBattleMenu(JNIEnv*, jclass, jboolean owns) {
  lower_menu_requested.store(owns == JNI_TRUE, std::memory_order_relaxed);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_colosseum_ColosseumState_readMenu(JNIEnv* env, jclass, jintArray ints, jobjectArray labels) {
  if (!ints || env->GetArrayLength(ints) < kMenuInts) return JNI_FALSE;
  std::shared_ptr<const MenuSnapshot> frame;
  {
    std::lock_guard lock(menu_lock);
    frame = menu_published;
  }
  MenuSnapshot values;
  if (frame) {
    values = *frame;
  } else {
    std::fill(values.ints.begin(), values.ints.end(), -1);
    values.ints[0] = 2;
    values.ints[1] = values.ints[2] = values.ints[4] = values.ints[6] = 0;
  }
  const int64_t drop = last_menu_drop.load(std::memory_order_relaxed);
  const bool requested = lower_menu_requested.load(std::memory_order_relaxed);
  const bool withheld = requested && battle_feed_valid.load(std::memory_order_relaxed) && drop >= 0 &&
                        now_ms() - drop < 500;
  values.ints[3] = (requested ? 1 : 0) | (withheld ? 2 : 0);
  const jsize length = std::min<jsize>(env->GetArrayLength(ints), kMenuIntsFull);
  env->SetIntArrayRegion(ints, 0, length, values.ints.data());
  if (labels) {
    const jsize count = std::min<jsize>(env->GetArrayLength(labels), kMenuLabels);
    for (jsize i = 0; i < count; ++i) {
      const auto& text = values.labels[i];
      jstring value = text.empty() ? nullptr :
          env->NewString(reinterpret_cast<const jchar*>(text.data()), jsize(text.size()));
      if (!text.empty() && !value) return JNI_FALSE;
      env->SetObjectArrayElement(labels, i, value);
      if (value) env->DeleteLocalRef(value);
    }
  }
  return JNI_TRUE;
}

// 0..255: the smoothed mean luminance of the game image the renderer last
// read back; -1 until the first reading. The first call turns the probe on.
extern "C" JNIEXPORT jint JNICALL
Java_dev_wiids_colosseum_ColosseumState_topLevel(JNIEnv*, jclass) {
  static std::once_flag enabled;
  std::call_once(enabled, [] { aurora_luma_probe_enable(true); });
  return aurora_luma_probe_level();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_wiids_colosseum_ColosseumState_readUi(JNIEnv* env, jclass, jintArray out) {
  if (!out || env->GetArrayLength(out) < kUiInts || !ui_sequence.load(std::memory_order_relaxed))
    return JNI_FALSE;
  const uint64_t packed = ui_packed.load(std::memory_order_relaxed);
  const bool menu = packed & 1, pda = packed & 2, start_menu = packed & 4, overlay = packed & 8;
  const unsigned extra = ui_extra.load(std::memory_order_relaxed);
  const jint values[kUiIntsMax] = {menu, pda ? 1 : overlay ? 2 : menu && start_menu ? 3 : 0,
                                   jint((packed >> 16) & 0xffff), jint((packed >> 8) & 0xff), start_menu,
                                   jint(ui_sequence.load(std::memory_order_relaxed)), jint(extra & 1),
                                   jint((extra >> 1) & 1),
                                   jint(field_message_box.load(std::memory_order_relaxed)),
                                   jint(field_control.load(std::memory_order_relaxed) & 1),
                                   jint((field_control.load(std::memory_order_relaxed) >> 1) & 1)};
  env->SetIntArrayRegion(out, 0, std::min<jsize>(env->GetArrayLength(out), kUiIntsMax), values);
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_wiids_colosseum_ColosseumGameActivity_setLowerOwnsPlayerStatus(JNIEnv*, jclass, jboolean owns) {
  lower_status_requested.store(owns == JNI_TRUE, std::memory_order_relaxed);
}

extern "C" JNIEXPORT jobject JNICALL
Java_dev_wiids_colosseum_ColosseumState_read(JNIEnv* env, jclass state_class, jint known_map) {
  std::shared_ptr<const Snapshot> frame;
  {
    std::lock_guard lock(snapshot_lock);
    frame = published;
  }
  if (!frame) return nullptr;

  const auto constructor = env->GetMethodID(state_class, "<init>", "(IIILjava/lang/String;FF[Ljava/lang/String;[I[I[I[F)V");
  const auto string_class = env->FindClass("java/lang/String");
  if (!constructor || !string_class) return nullptr;
  const auto archive = env->NewStringUTF(frame->archive.c_str());
  const auto names = env->NewObjectArray(frame->names.size(), string_class, nullptr);
  const auto stats = env->NewIntArray(frame->party_stats.size());
  const auto status = env->NewIntArray(frame->shadow_status.size());
  const auto heart = env->NewIntArray(frame->shadow_heart.size());
  if (!archive || !names || !stats || !status || !heart) return nullptr;
  for (size_t i = 0; i < frame->names.size(); ++i) {
    const auto& name = frame->names[i];
    if (name.empty()) continue;
    const auto value = env->NewString(reinterpret_cast<const jchar*>(name.data()), name.size());
    if (!value) return nullptr;
    env->SetObjectArrayElement(names, i, value);
    env->DeleteLocalRef(value);
  }
  env->SetIntArrayRegion(stats, 0, frame->party_stats.size(), frame->party_stats.data());
  env->SetIntArrayRegion(status, 0, frame->shadow_status.size(), frame->shadow_status.data());
  env->SetIntArrayRegion(heart, 0, frame->shadow_heart.size(), frame->shadow_heart.data());
  jfloatArray map = nullptr;
  if (known_map != frame->map_revision && frame->map) {
    map = env->NewFloatArray(frame->map->size());
    if (!map) return nullptr;
    if (!frame->map->empty())
      env->SetFloatArrayRegion(map, 0, frame->map->size(), frame->map->data());
  }
  return env->NewObject(state_class, constructor, frame->scene, frame->room, frame->map_revision,
                        archive, frame->x, frame->z, names, stats, status, heart, map);
}
