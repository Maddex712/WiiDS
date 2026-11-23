// Included in the native optional-rules TU. No Tick(), MonOf(), script calls,
// logging or state writes: use the selected target and existing owned state.
extern "C" uint32_t WiidsXD_ReadGen4FieldFlags(void* target){
    if(!Gen4On())return 0;
    const int turn=Turn();
    if(turn<sLastTurn)return 0; // the native reset is pending for a new battle
    const auto* mon=FindMon(target);
    auto active=[&](int start){return start>=0&&turn>=start&&turn<=start+4;};
    uint32_t flags=active(sGravityTurn)?1u:0u;
    if(mon&&active(mon->magnetTurn))flags|=2u;
    if(mon&&mon->miracleEye)flags|=4u;
    return flags;
}
