#include "xd_companion.h"
#include <jni.h>
#include <vector>

namespace { constexpr int words=8444; }
static_assert(words==24+6*55+4*55+XD_MAX_SHADOW*5+XD_MAX_CHOICES*53+2*55+XD_TEXT+96+4*XD_TEXT,"JNI packet layout");
extern "C" JNIEXPORT jintArray JNICALL Java_dev_wiids_xd_XdNative_read(JNIEnv* env,jclass) {
    XdSnapshot s{};WiidsXD_Read(&s);
    std::vector<jint> p(words,0);
    p[0]=0x58444350;p[1]=2;p[2]=words;p[3]=s.available;p[4]=s.battle;p[5]=s.field_menu;
    p[6]=s.menu_id;p[7]=s.phase;p[8]=s.choice_count;p[9]=s.shadow_count;p[10]=s.map_id;
    p[11]=s.story_progress;p[12]=s.objective_known;p[13]=s.actor;p[14]=s.target;
    uint32_t info=0;for(int i=0;i<4;++i)info|=(uint32_t)(s.moves[i].effectiveness_info&255)<<(i*8);p[15]=(jint)info;
    p[16]=(uint32_t)s.epoch;p[17]=s.epoch>>32;p[18]=(uint32_t)s.sequence;p[19]=s.sequence>>32;
    p[20]=(uint32_t)s.captured_ms;p[21]=s.captured_ms>>32;
    p[22]=s.actor_party_slot;p[23]=s.target_index;
    int b=24;
    for(const auto& x:s.party) {
        p[b++]=x.species;p[b++]=x.hp;p[b++]=x.max_hp;p[b++]=x.status;p[b++]=x.shadow_id;p[b++]=x.heart;p[b++]=x.heart_max;
        for(auto c:x.name)p[b++]=c;
    }
    for(const auto& x:s.moves) {
        p[b++]=x.id;p[b++]=x.type;p[b++]=x.pp;p[b++]=x.max_pp;p[b++]=x.legal;p[b++]=x.effectiveness_num;p[b++]=x.effectiveness_den;
        for(auto c:x.name)p[b++]=c;
    }
    for(const auto& x:s.shadows) {p[b++]=x.id;p[b++]=x.species;p[b++]=x.snagged;p[b++]=x.missed;p[b++]=x.heart;}
    for(const auto& x:s.choices) {
        p[b++]=x.action;p[b++]=x.argument;p[b++]=x.item_id;p[b++]=x.cursor;p[b++]=x.enabled;
        for(auto c:x.label)p[b++]=c;
    }
    for(const XdPokemon* x:{&s.acting,&s.targeted}) {
        p[b++]=x->species;p[b++]=x->hp;p[b++]=x->max_hp;p[b++]=x->status;p[b++]=x->shadow_id;p[b++]=x->heart;p[b++]=x->heart_max;
        for(auto c:x->name)p[b++]=c;
    }
    for(auto c:s.map_name)p[b++]=c;
    for(auto c:s.objective)p[b++]=c;
    for(const auto& type:s.move_type_names)for(auto c:type)p[b++]=c;
    if(b!=words)return nullptr;
    jintArray result=env->NewIntArray(words);
    if(result)env->SetIntArrayRegion(result,0,words,p.data());
    return result;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_wiids_xd_XdNative_submit(JNIEnv*,jclass,jlong epoch,jint action,jint argument) {
    return WiidsXD_Submit((uint64_t)epoch,action,argument)?JNI_TRUE:JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL Java_dev_wiids_xd_XdNative_suspend(JNIEnv*,jclass) {WiidsXD_Suspend();}
