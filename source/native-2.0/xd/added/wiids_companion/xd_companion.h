#pragma once
#include <stdint.h>

// Owned values only. The Android thread never receives or dereferences a game pointer.
enum XdAction { XD_SELECT=1, XD_CONFIRM, XD_CANCEL, XD_UP, XD_DOWN, XD_LEFT, XD_RIGHT,
    XD_MOVE, XD_PARTY_MEMBER, XD_SNAG, XD_RUN, XD_BAG, XD_PARTY, XD_PDA };
enum { XD_MAX_CHOICES=128, XD_MAX_SHADOW=128, XD_TEXT=48 };
struct XdPokemon {
    int32_t species, hp, max_hp, status, shadow_id, heart, heart_max;
    uint16_t name[XD_TEXT];
};
struct XdMove {
    int32_t id, type, pp, max_pp, legal;
    // -1: unknown. Do not substitute a generic type chart for live target/ability rules.
    int32_t effectiveness_num, effectiveness_den;
    // Packed into the previously reserved JNI header word 15, one byte/move.
    // 0 unknown, 1 damaging type match, 2 status (no damage multiplier),
    // 3 blocked by ability, 4 existing status, 5 type immunity,
    // 6 Substitute/Safeguard, 7 Uproar, 8 native battle clause.
    int32_t effectiveness_info;
    uint16_t name[XD_TEXT];
};
struct XdChoice {
    int32_t action, argument, item_id, cursor, enabled;
    uint16_t label[XD_TEXT];
};
struct XdShadow { int32_t id, species, snagged, missed, heart; };
struct XdSnapshot {
    uint32_t version, available, battle, field_menu, menu_id, phase, choice_count;
    uint32_t shadow_count, map_id, story_progress, objective_known, actor, target;
    uint64_t epoch, sequence, captured_ms;
    XdPokemon party[6];
    XdMove moves[4];
    XdShadow shadows[XD_MAX_SHADOW];
    XdChoice choices[XD_MAX_CHOICES];
    XdPokemon acting,targeted;
    int32_t actor_party_slot,target_index;
    uint16_t map_name[XD_TEXT],objective[96];
    uint16_t move_type_names[4][XD_TEXT];
};
struct XdRequest { uint64_t epoch; int32_t action, argument; };

#ifdef __cplusplus
extern "C" {
#endif
uint64_t WiidsXD_Now(void);
// Must be called at the native menu's input boundary, before dispatching its normal callbacks.
// physical_input cancels touch immediately, so physical controls always win.
void WiidsXD_Publish(XdSnapshot* frame, uintptr_t window_cookie, int physical_input);
int WiidsXD_Read(XdSnapshot* out);
int WiidsXD_Submit(uint64_t epoch, int action, int argument);
int WiidsXD_Take(XdRequest* out);
void WiidsXD_Suspend(void);
// A persistent admission barrier, distinct from one-shot epoch invalidation.
void WiidsXD_BlockInput(int blocked);
int WiidsXD_InputBlocked(void);
#ifdef __cplusplus
}
#endif
