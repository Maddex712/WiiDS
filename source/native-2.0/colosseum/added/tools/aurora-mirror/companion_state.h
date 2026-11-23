#pragma once

#include <cstdint>
#include <string_view>

// The guest thread supplies verified GC6E01 observations. The Android UI only
// receives copies, and never reads guest memory from another thread.
bool companion_story_battle(const uint8_t* ram, uint32_t size);
void companion_publish(const uint8_t* ram, uint32_t size, int scene, int room,
                       std::string_view archive, uint32_t mesh, float x, float z);
// True while the lower screen asked to own the player's status and the last
// snapshot had a player battler. The renderer then withholds the original
// plates and reports each withheld draw.
// Once per field: the game's own menu state as the presentation reads it.
void companion_ui(bool menu, bool pda, bool start_menu, bool overlay, unsigned overlay_mode, unsigned message);
bool companion_lower_owns_status();
void companion_player_plate_dropped();
void companion_party_screen_drawn();
// The battle's input windows, for the renderer.
bool companion_lower_owns_menu();
void companion_menu_dropped();
void companion_menu_drawn(int kind);
// True while the last guest sample read the open window of this kind (see
// readMenu ints[2]) completely enough for the lower screen to stand in for it.
// Kinds 1 and 2 hold for the ordinary command and move windows only.
bool companion_menu_ready(int kind);
void companion_ui_extra(bool message_window, bool movie);
// Every field, after companion_menu_sample: who drives the field (readUi
// out[9], out[10]). field is false outside the field and in battles.
void companion_field_control(const uint8_t* ram, uint32_t size, bool field, bool menu, bool message_window);
// Every field, on the guest thread: reads the battle's input windows for
// ColosseumState.readMenu.
void companion_menu_sample(const uint8_t* ram, uint32_t size);

// ColosseumState.readBattle(int[] ints, String[] names) copies the battle
// section of the last snapshot. ints needs 22 entries (28 to receive the
// experience, heart and party-screen fields: 29) and names 2. It returns
// false, leaving both untouched, before the first snapshot or when an array
// is too short.
//   ints[0] layout version (1)
//   ints[1] 1 when at least one of the player's active battlers was read
//   ints[2] battle state revision, raised whenever an entry other than
//           ints[3] and ints[5] changes
//   ints[3] bit 0: the lower screen asked to own the player's status
//           bit 1: the original player plates are being withheld: bit 0 is
//           set, ints[1] is 1 and a plate draw was dropped in the last 0.5 s.
//           Evaluated at the call, not at the sample.
//   ints[4] number of battlers present
//   ints[5] snapshot sequence, raised on every guest sample (about 10 Hz)
//   ints[6 + 8 * p ...] for field position p (0, 1):
//     +0 present, +1 party slot (0-5, the order of the party arrays),
//     +2 species id, +3 level, +4 current HP, +5 maximum HP,
//     +6 status (0 none, 3 poison, 4 bad poison, 5 paralysis, 6 burn,
//        7 freeze, 8 sleep, -1 unknown), +7 1 for an unpurified Shadow Pokemon
//   ints[22 + 2 * p], ints[23 + 2 * p] experience bar of position p: points
//     gained within the current level and points the level spans. 0, 0 at
//     level 100; -1, -1 when absent or unknown.
//   ints[26 + p] heart gauge of position p in percent, as in the Shadow
//     list, for the player's own unpurified Shadow Pokemon; otherwise -1.
//   ints[28] 1 while the battle's own party screen (Pokemon choice, summary)
//     was drawn in the last 0.5 s. Known only while bit 0 of ints[3] is set
//     or a trace is on, and only with a module that reports recordings.
//   names[p] species name, or null when position p is absent
// Opponents are never included.
//
// ColosseumState.readUi(int[] out) reports the game's own menus. out needs 6
// entries; false before the first field.
//   out[0] 1 while the game's menu system is active (word 0x8040836C == 1):
//          the Start menu and every page under it, the PC and shops.
//   out[1] which, as far as the host can prove: 1 PDA (message ids 0x36xx),
//          2 a panel over the live field (shop or field overlay 0x18),
//          3 a page of the Start menu session, 0 unknown or none.
//   out[2] the active UI message id (0x80402436), raw
//   out[3] the field overlay mode byte (0x80402504), raw
//   out[4] 1 while the menu was opened from the Start menu's root page
//   out[5] sample sequence
//   out[6] (with 8 entries) 1 while the game's message window is open
//          (byte 0x80402418 == 2): field dialogue and battle messages alike
//   out[7] 1 while a movie is playing
//   out[8] (with 9 entries) 1 while a message box is on screen outside a
//          battle (NPC talk, story text, signs, the text over a field yes/no):
//          one of the box menus 0x0F, 0x10, 0x40, 0x50, 0x51, 0xE6, 0xE8,
//          0x107, 0x10C is in the game's window list. 0x80106D3C opens the box
//          as one of these by its type and 0x801069FC closes it; out[6] stays 1
//          after the first text, this one does not.
//   out[9] (with 10 entries) 1 while an event camera drives the field: the
//          camera mode byte (*(0x80478C40)) is not 0, or the field camera byte
//          0x8047AD71 is cleared (scripts do both; see companion_state.cpp).
//          0 in free movement, outside the field and in battles.
//   out[10] (with 11 entries) 1 while the player cannot walk, as far as the
//          host can tell: out[9], the menu system (out[0]), the message
//          window (out[6]) or a message box (out[8]), or one of the reasons the
//          movement native 0x8012EBD4 ignores the stick (windows 0x110 and
//          0xCA, its delay counter, a queued call). A script that simply stops
//          calling that native without any of these is not seen. 0 outside the
//          field and in battles.
//
// ColosseumState.readMenu(int[] ints, String[] labels): the battle input
// window that is waiting for the player, read every field on the guest thread
// from the game's own window system (addresses in companion_state.cpp). ints
// needs at least 8 entries and is filled up to 92; labels may be null and is
// filled up to 17. Returns false only when ints is null or too short.
//   ints[0]  layout version (2)
//   ints[1]  1 while such a window is open and was read
//   ints[2]  kind: 0 none, 1 command window (FIGHT and the rest), 2 move
//            list, 3 target selection, 4 party screen (0x63), 5 bag (0x59),
//            6 two-way choice (0xFB, after the command window's third or
//            fourth choice in some battles), 7 a party member's action list
//            (0x6D / 0x108: shift, summary, cancel) or the chosen bag item's
//            action list (0x5A: USE / CANCEL). 0xFB was reported as 4
//            before; it is not the party screen.
//   ints[3]  bit 0: the lower screen asked for the input windows
//            bit 1: their upper drawing is being withheld (a draw of an
//            input window was dropped in the last 0.5 s). The command and
//            move windows are withheld whenever bit 0 is set; the party
//            screen, bag, choice and action list only while this feed reads
//            them completely (companion_menu_ready).
//   ints[4]  revision, raised whenever any other entry or label changes
//   ints[5]  acting field position (0, 1) of the player's Pokemon, or -1.
//            Move list: the window's own battle record. Command window: the
//            position whose nickname equals the window's name, when unique.
//   ints[6]  row count (0-8)
//   ints[7]  row under the native cursor, or -1
//   ints[8]  the game's menu id: 0x4B command, 0x4C moves, 0xFF / 0x104
//            target, 0x63 party, 0x59 bag, 0xFB choice, 0x6D / 0x108 / 0x5A
//            action list; 0xF6 (command: three rows, no cursor, each choice on its
//            own button) and 0xF7 / 0xF8 (kind 2, rows only; 0xF8 is most
//            likely that layout's party screen) are the game's alternative
//            layouts.
//   ints[9]  the native cursor: the game's count of enabled items before it
//            (bag: the place of the item under the cursor in the pocket;
//            0xF6: -1)
//   ints[10] item id under the cursor, or -1 (bag: the bag item's id)
//   ints[11] move list: the row picked with the swap button that waits
//            for its new place, or -1; bag: the pocket place of row 0;
//            action list 0x6D / 0x108: the party slot it is for; 0x5A: the
//            bag item id it is for (-1 when the bag cannot be read);
//            otherwise -1
//   ints[12 + 10 * r ...] row r:
//     +0 item id (command 0xB5-0xB8, moves 0xC4-0xC7, target 0x1258-0x125B)
//     +1 enabled: the game lets the cursor stop on it (1) or not (0)
//     +2 command window: the action code the game returns (0 FIGHT, 1 and 3
//        as the window's labels say, 2 the third choice); move list: the move
//        slot 0-3; target window: the field position code (0x45-0x48);
//        otherwise -1
//     +3 move list: type id (0 Normal, 1 Fighting, 2 Flying, 3 Poison,
//        4 Ground, 5 Rock, 6 Bug, 7 Ghost, 8 Steel, 9 ???, 10 Fire, 11 Water,
//        12 Grass, 13 Electric, 14 Psychic, 15 Ice, 16 Dragon, 17 Dark) from
//        the type icon the window shows, -1 when it shows none
//     +4, +5 move list: PP and maximum PP as the window shows them, -1 when
//        it shows text instead
//     +6 move list: 0 an ordinary move, 1 Shadow Rush, 2 a sealed Shadow
//        move, -1 an empty slot
//     +7, +8 the item's authored position (x, y) in the game's 640x480 frame
//     +9 party screen: the level | 0x100 while the member is on the field
//        | 0x200 when it has fainted; otherwise -1
//   Kind 4 (0x63), rows 0-5 are party slots 0-5 and row 6 CANCEL: +0 item
//     0x3B0-0x3B6, +1 1 when the cursor can stop on it (the slot holds a
//     member), +2 the slot, +3 species id, +4 HP, +5 maximum HP, +6 status
//     (as readBattle), +7 / +8 the slot's place as the game set it. Members
//     are read only while the screen shows the battle's party (state +8 == 1);
//     otherwise +3..+6 and +9 are -1 and +1 is 0.
//   Kind 5 (0x59): up to 8 of the pocket's filled slots around the cursor:
//     +0 the bag item id, +1 1, +2 its place in the pocket, +3 the pocket
//     (0-5, the tab), +4 quantity, +5 the pocket's item count, +6 1 for the
//     item picked with the move button, +7 / +8 -1.
//   Kind 6 (0xFB): rows 0x1206, 0x1207; +2 1 goes ahead, 0 returns to the
//     command window (0x80011B2C).
//   Kind 7: +2 the value the game returns for the row. 0x6D / 0x108: 0
//     switch, 1 the second choice, 2 back. 0x5A: the row's place; +3 1 when
//     the row goes on (USE: the item's effect, then the party screen 0x63,
//     reported as kind 4 with the battle's members), 0 when it goes back to
//     the bag (CANCEL); +6 1 when the row is drawn grey; +7 / +8 -1.
//   0xF6: +2 the command result (0, 2, 3) as for the command window.
//   labels[0]  title: the acting Pokemon's name as the window shows it;
//            bag: the pocket's name; choice: the question
//   labels[1 + 2 * r]  row r as drawn (command: its message; move: the move's
//            name; party: the member's nickname; bag: the item's name);
//            null when the game draws no text for it (target rows)
//   labels[2 + 2 * r]  move list: the type's name, or the text the PP box
//            shows for Shadow Rush and sealed moves; party: the species name
//
// ColosseumState.topLevel(): 0..255, the mean luminance of the game image as
// presented above (before the on-screen UI), from nine rows of every sixth
// frame, read back asynchronously and lightly smoothed; -1 until the first
// reading. The first call turns the probe on.
