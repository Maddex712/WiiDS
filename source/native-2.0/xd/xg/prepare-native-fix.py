from pathlib import Path

out = Path(__file__).resolve().parent
source = Path("build/xd-mod-2026-11-21/upstream/games/xd/src/data/main_program_map.cpp")
text = source.read_text()
text = text.replace(
    "std::vector<const XDMainProgramAddress*> sSorted;",
    """std::vector<const XDMainProgramAddress*> sSorted;

uintptr_t dol_host_address(uint32_t address) {
#if defined(__ANDROID__)
  // The Android DOL loader writes into the relocated MEM1 region.
  return XdMem1ToHost(address);
#else
  return address;
#endif
}

uintptr_t dol_guest_address(const void* pointer) {
  const auto address = reinterpret_cast<uintptr_t>(pointer);
#if defined(__ANDROID__)
  return XdMem1ToGuest(address);
#else
  return address;
#endif
}""")
old = "return reinterpret_cast<void*>(static_cast<uintptr_t>(gcAddress));"
assert text.count(old) == 2
text = text.replace(old, "return reinterpret_cast<void*>(dol_host_address(gcAddress));")
old = "const auto u = reinterpret_cast<uintptr_t>(p);"
assert text.count(old) == 1
text = text.replace(old, "const auto u = dol_guest_address(p);")
assert text.count("dol_host_address") == 3 and text.count("dol_guest_address") == 2
(out / source.name).write_text(text, newline="\n")

source = out / "xd_effectiveness.cpp"
text = source.read_text()
needle = """    if(!target){for(auto& move:frame->moves)if(move.id&&move.id!=0x176&&!wazaDB_GetIryoku((uint16_t)move.id))move.effectiveness_info=2;return;}
    const bool xg = xd::xg::On();"""
replacement = """    const bool xg = xd::xg::On();
    if (xg) {
        // Retail type abbreviations do not identify the right icon and color.
        static const char* const names[] = {
            "NORMAL", "FIGHTING", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST", "STEEL",
            "FAIRY", "FIRE", "WATER", "GRASS", "ELECTRIC", "PSYCHIC", "ICE", "DRAGON", "DARK"
        };
        for (int i = 0; i < 4; ++i) {
            const auto& move = frame->moves[i];
            if (!move.id || move.type < 0 || move.type >= 18 || wazaCheckDarkWaza((uint16_t)move.id)) continue;
            const char* name = names[move.type];
            auto& label = frame->move_type_names[i];
            for (auto& letter : label) letter = 0;
            for (int j = 0; name[j]; ++j) label[j] = (uint16_t)name[j];
        }
    }
    if(!target){for(auto& move:frame->moves)if(move.id&&move.id!=0x176&&!wazaDB_GetIryoku((uint16_t)move.id))move.effectiveness_info=2;return;}"""
assert text.count(needle)==1
source.write_text(text.replace(needle, replacement), newline="\n")
