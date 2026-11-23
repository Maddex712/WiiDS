#include "xd_companion.h"
#include "xd_xg.h"
#include <stdint.h>
extern "C" {
uint16_t fightOutPokemonGetZokuseiDataId(void*,uint8_t);
uint16_t fightOutPokemonGetTokuseiDataId(void*);
uint8_t fightOutPokemonIsJoutaiDataId(void*,uint16_t);
uint16_t zokuseiGetWazaJoutai(uint16_t,uint16_t);
uint8_t wazaCheckDarkWaza(uint16_t);
uint16_t wazaDB_GetIryoku(uint16_t);
uint8_t wazaDB_GetBouon(uint16_t);
void* fightOutPokemonGetPokemonPtr(void*);
uint8_t pokemonIsDarkPokemon(void*);
uint8_t fightFloorGetNowTenkouDataId(void*,uint8_t);
void pokemonGetMezamerupower(void*,uint16_t*,uint16_t*);
uint16_t wazaDB_GetSeqId(uint16_t);
uint8_t fightOutPokemonIsJoutaiNormal(void*);
void* fightTargetGetPtrAsNowFightType(uint16_t,void*);
uint8_t fightSideIsJoutaiDataId(void*,uint16_t);
uint16_t fightFloorGetFightOutPokemonPtrAry(void*,bool,uint8_t,void*,void**);
uint8_t fightSeqRegulationOverTwoCheck(uint16_t,uint32_t);
uint16_t PortXD_Gen4GetType(void*,uint8_t);
uint32_t WiidsXD_ReadGen4FieldFlags(void*);
}
namespace xd::challenges {bool Active(const char*);}
extern "C" uint16_t WiidsXD_ResolveMoveType(void* pokemon,uint16_t id,uint16_t base) {
    if(id==0xed&&pokemon){uint16_t power=0,type=base;pokemonGetMezamerupower(pokemon,&power,&type);return type;}
    if(id==0x137){switch(fightFloorGetNowTenkouDataId(nullptr,1)){
        case 1:return 0xa;case 2:return 0xb;case 3:return 5;case 4:return 0xf;default:return 0;}}
    return base;
}
extern "C" uint16_t WiidsXD_ResolveBattleMoveType(void* actor,void* pokemon,uint16_t id,uint16_t base){
    if(actor&&!xd::xg::On()&&xd::challenges::Active("gen4_rules")&&fightOutPokemonGetTokuseiDataId(actor)==96)return 0;
    return WiidsXD_ResolveMoveType(pokemon,id,base);
}
// Read-only native type chart. Never run fightSeqTypeCheck: even apply=0 writes
// ServerWork, move flags and battle globals. Mirror its read-only conditions.
extern "C" void WiidsXD_ReadBattleEffectiveness(void* actor,void* target,XdSnapshot* frame) {
    const bool xg = xd::xg::On();
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
    if(!target){for(auto& move:frame->moves)if(move.id&&move.id!=0x176&&!wazaDB_GetIryoku((uint16_t)move.id))move.effectiveness_info=2;return;}
    const bool modified = !xg && xd::challenges::Active("gen4_rules");
    // XG uses the original typeless slot for Fairy. Its disc supplies the chart.
    const auto hasType = [xg](uint16_t type) { return xg || type != 9; };
    const uint16_t targetAbility=fightOutPokemonGetTokuseiDataId(target);
    const uint16_t actorAbility=actor?fightOutPokemonGetTokuseiDataId(actor):0;
    const bool mold=modified&&actor&&actor!=target&&actorAbility==104;
    const uint16_t ability=mold?77:targetAbility;
    const uint16_t types[]={modified?PortXD_Gen4GetType(target,0):fightOutPokemonGetZokuseiDataId(target,0),
                            modified?PortXD_Gen4GetType(target,1):fightOutPokemonGetZokuseiDataId(target,1)};
    const uint32_t fieldFlags=modified?WiidsXD_ReadGen4FieldFlags(target):0;
    auto match=[&](uint16_t moveType,uint16_t type){
        uint16_t result=zokuseiGetWazaJoutai(moveType,type);
        if(!modified)return result;
        // Mirror the pinned optional implementation, using selected values,
        // never its execution-global wrappers or mutation-bearing getters.
        if(moveType==4){
            if(type==2&&result==0x43&&(fieldFlags&1))return (uint16_t)0x3f;
            if(fieldFlags&2)return (uint16_t)0x43;
        }else if(moveType==17&&type==14&&result==0x43&&(fieldFlags&4))return (uint16_t)0x3f;
        if(result==0x43&&type==7&&moveType<=1&&actorAbility==113)return (uint16_t)0x3f;
        return result;
    };
    for(auto& move:frame->moves) {
        if(!move.id||move.id==0x176)continue;
        const uint16_t id=(uint16_t)move.id,power=wazaDB_GetIryoku(id);
        // fightSeq.cpp's before-hit ability checks, without invoking their writes.
        bool immune=(modified&&!mold&&actor&&actor!=target&&ability==78&&move.type==0xd&&power!=0)||
            (modified&&ability==87&&move.type==0xb&&power!=0)||
            (ability==0x2b&&wazaDB_GetBouon(id)==1)||
            (id!=0x176&&((ability==0xa&&move.type==0xd&&power!=0)||
              (ability==0xb&&move.type==0xb&&power!=0)||
              (ability==0x12&&move.type==0xa&&!fightOutPokemonIsJoutaiDataId(target,7))));
        if(immune){move.effectiveness_info=3;if(power){move.effectiveness_num=0;move.effectiveness_den=1;}continue;}
        if(!power){
            // These classifications only assert a known blocker. N/A is never
            // a promise that a status move will hit or succeed.
            move.effectiveness_info=2;
            const uint16_t ability=targetAbility; // status scripts run outside MoldScope
            const uint16_t seq=wazaDB_GetSeqId(id);
            const bool sleep=seq==1,poison=seq==0x21||seq==0x42,
                paralysis=seq==0x43,burn=seq==0xa7,confusion=seq==0x31;
            if((sleep&&(ability==0x48||ability==0xf))||(poison&&ability==0x11)||
               (paralysis&&ability==7)||(burn&&ability==0x29)||(confusion&&ability==0x14))
                move.effectiveness_info=3;
            else if(((sleep||poison||paralysis||burn)&&!fightOutPokemonIsJoutaiNormal(target))||
                    (confusion&&fightOutPokemonIsJoutaiDataId(target,9)))move.effectiveness_info=4;
            else if((poison&&(types[0]==3||types[1]==3||types[0]==8||types[1]==8))||
                    (burn&&(types[0]==0xa||types[1]==0xa)))move.effectiveness_info=5;
            if(sleep||poison||paralysis||burn||confusion){
                const bool foresight=fightOutPokemonIsJoutaiDataId(target,0x19)&&(types[0]==7||types[1]==7)&&move.type<=1;
                if(paralysis&&!foresight)for(auto type:types)if(hasType(type)&&match((uint16_t)move.type,type)==0x43)move.effectiveness_info=5;
                if(fightOutPokemonIsJoutaiDataId(target,0x14)||
                   fightSideIsJoutaiDataId(fightTargetGetPtrAsNowFightType(2,target),0x4b))move.effectiveness_info=6;
                if(sleep&&ability!=0x2b){void* active[8]={};
                    uint16_t count=fightFloorGetFightOutPokemonPtrAry(nullptr,true,0,nullptr,active);
                    if(count>8)count=8;
                    for(uint16_t i=0;i<count;++i)if(active[i]&&fightOutPokemonIsJoutaiDataId(active[i],0xb))move.effectiveness_info=7;
                }
                if(sleep&&fightSeqRegulationOverTwoCheck(8,(uint32_t)(uintptr_t)target))move.effectiveness_info=8;
                if(modified&&actor&&actor!=target&&ability==102&&fightFloorGetNowTenkouDataId(nullptr,1)==1)move.effectiveness_info=3;
            }
            continue;
        }
        int num=1,den=1;bool known=true;
        const bool dark=wazaCheckDarkWaza(id)==1;
        const bool skip=id==0xa5||id==0xf8||id==0x161||(id==0x137&&fightFloorGetNowTenkouDataId(nullptr,1)==5);
        const bool foresight=fightOutPokemonIsJoutaiDataId(target,0x19)&&(types[0]==7||types[1]==7)&&move.type<=1;
        if(dark){num=pokemonIsDarkPokemon(fightOutPokemonGetPokemonPtr(target))?1:2;den=num==1?2:1;}
        else if(targetAbility==0x1a&&move.type==4&&!skip){num=0;}
        else if(!skip&&!foresight)for(int i=0;i<2;i++) {
            if(!hasType(types[i])||(i==1&&types[0]==types[1]))continue;
            switch(match((uint16_t)move.type,types[i])) {
                case 0x3f:break;
                case 0x41:num*=2;break;
                case 0x42:den*=2;break;
                case 0x43:num=0;break;
                default:known=false;break;
            }
        }
        if(known){
            // Wonder Guard's native type check applies only to ordinary damaging moves.
            if(!dark&&!skip&&targetAbility==0x19&&num<=den)num=0;
            if(!num)den=1;while(num&&num%2==0&&den%2==0){num/=2;den/=2;}
            move.effectiveness_num=num;move.effectiveness_den=den;
            move.effectiveness_info=1;
        }
    }
}
extern "C" void WiidsXD_ReadEffectiveness(void* target,XdSnapshot* frame){WiidsXD_ReadBattleEffectiveness(nullptr,target,frame);}
