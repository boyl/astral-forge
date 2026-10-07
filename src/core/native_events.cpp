#include "native_events.h"
#include "gameplay_events.h"
#include "game_profile.h"
#include "game_state.h"
#include "hook.h"
#include "log.h"
#include <cmath>
#include <cstring>
#include <atomic>
#include <intrin.h>
namespace aamod {
namespace {
void* target;void* damage_target;void* defeat_target;void* pickup_target;void* victory_target;void* abandon_target;unsigned char* game_base;void(*original_abandon)(void*);void(*original_start)(void*);void(*original_damage)(void*);double(*original_defeat)(void*);double(*original_victory)(void*,double);void(*original_pickup)(void*,void*,void*);std::atomic<bool> available{false};std::atomic<uint64_t> ended_key{UINT64_MAX};
bool run_key(void* frame,double* value) {
    __try {*value=*(double*)((unsigned char*)frame+0x1c4768);return std::isfinite(*value)&&*value>=0;}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void start(void* frame) {
    original_start(frame);
    if(!available.load())return;
    AAModGameplayEvent event={};if(!run_key(frame,&event.run_key)){gameplay_events_status(AAMOD_STATE_FAULT,0);available.store(false);return;}
    event.kind=AAMOD_GAMEPLAY_RUN_START;event.source_rva=0x31ddff0;event.update_thread_id=GetCurrentThreadId();event.tick_ms=GetTickCount64();gameplay_events_record(event);
}
bool damage_context(void* frame,double* health,double* key) {
    __try {
        auto bytes=(unsigned char*)frame;
        if(*(uint32_t*)(bytes+0x720)!=3||*(double*)(bytes+0x728)!=1.0)return false;
        *health=*(double*)(bytes+0x1c4ff0);*key=*(double*)(bytes+0x1c4768);
        return std::isfinite(*health)&&std::isfinite(*key);
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void damage(void* frame) {
    AAModGameState state={};double before=0,key=0;
    bool capture=_ReturnAddress()==game_base+0x311fbf6&&available.load()&&game_state_query(&state,sizeof(state))==0&&state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present&&damage_context(frame,&before,&key);
    original_damage(frame);
    if(!capture||!available.load())return;
    double after=0,current_key=0;
    bool after_valid=damage_context(frame,&after,&current_key);
    static std::atomic<unsigned> samples{0};
    if(samples.fetch_add(1)<8)AAMOD_INFO("native events: hit boundary capture=%u after_valid=%u scene=%u p1=%u p2=%u before=%.3f after=%.3f",capture,after_valid,state.scene_index,state.players[0].present,state.players[1].present,before,after);
    if(!after_valid||current_key!=key||before<=after||before<=0)return;
    AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_DAMAGE;event.source_rva=0x311fbf1;event.update_thread_id=GetCurrentThreadId();event.tick_ms=GetTickCount64();event.run_key=key;event.before=before;event.after=after;event.amount=before-after;gameplay_events_record(event);
}
bool pickup_context(void* frame,void* player,double* crystals,double* key) {
    __try {
        if(!player||*(void**)((unsigned char*)player+8)!=game_base+0xc407b80)return false;
        *crystals=*(double*)((unsigned char*)frame+0x1c3f30);
        return std::isfinite(*crystals)&&run_key(frame,key);
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void pickup(void* frame,void* player,void* collectible) {
    AAModGameState state={};double before=0,key=0;
    bool capture=_ReturnAddress()==game_base+0x200ade2&&available.load()&&game_state_query(&state,sizeof(state))==0&&state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present&&pickup_context(frame,player,&before,&key);
    // The native proximity collector consumes frame, player and collectible.
    // Forward all three incoming arguments, including calls outside capture.
    original_pickup(frame,player,collectible);
    if(!capture||!available.load())return;
    double after=0,current_key=0;
    if(!pickup_context(frame,player,&after,&current_key)||key!=current_key||after<=before)return;
    AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_PICKUP;event.source_rva=0x200ae00;event.run_key=key;event.before=before;event.after=after;event.amount=after-before;event.tick_ms=GetTickCount64();event.update_thread_id=GetCurrentThreadId();event.content_kind=AAMOD_GAMEPLAY_CONTENT_CURRENCY;event.content_id=AAMOD_GAMEPLAY_CURRENCY_CRYSTALS;gameplay_events_record(event);
}
bool victory_context(void* frame,double* key) {
    __try {
        auto bytes=(unsigned char*)frame;double health=*(double*)(bytes+0x1c4ff0);
        return (*(unsigned char*)(bytes+0x1c14c2)&4)&&std::isfinite(health)&&health>0&&run_key(frame,key);
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
double victory(void* frame,double outcome) {
    AAModGameState state={};double key=0;
    bool capture=_ReturnAddress()==game_base+0x13680c1&&outcome==1.0&&available.load()&&game_state_query(&state,sizeof(state))==0&&state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present&&victory_context(frame,&key);
    static std::atomic<unsigned> victory_samples{0};
    if(victory_samples.fetch_add(1)<6)AAMOD_INFO("native events: victory boundary caller=%x outcome=%.0f capture=%u state=%u scene=%d p1=%u p2=%u native_context=%u run=%.0f",(unsigned)((unsigned char*)_ReturnAddress()-game_base),outcome,capture,state.status,state.scene_index,state.players[0].present,state.players[1].present,victory_context(frame,&key),key);
    double result=original_victory(frame,outcome);
    if(capture&&available.load()) {
        double current_key=0;uint64_t bits;memcpy(&bits,&key,sizeof(bits));
        if(run_key(frame,&current_key)&&current_key==key&&ended_key.exchange(bits)!=bits) {
            AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_RUN_END;event.source_rva=0x35057b0;event.run_key=key;event.update_thread_id=GetCurrentThreadId();event.tick_ms=GetTickCount64();event.content_kind=AAMOD_GAMEPLAY_CONTENT_RUN_RESULT;event.content_id=AAMOD_GAMEPLAY_RUN_VICTORY;gameplay_events_record(event);
        }
    }return result;
}
void abandon(void* frame) {
    AAModGameState state={};double key=0;
    bool capture=_ReturnAddress()==game_base+0x1d5ed9f&&available.load()&&game_state_query(&state,sizeof(state))==0&&state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present&&run_key(frame,&key);
    // RCX is the sole input; both callers ignore the return registers.
    // Only the confirmed pause-menu caller is abandonment. The shared defeat
    // caller at 31117e1 must forward without emitting another ending.
    original_abandon(frame);
    if(capture&&available.load()) {
        double current_key=0;uint64_t bits;memcpy(&bits,&key,sizeof(bits));
        if(run_key(frame,&current_key)&&current_key==key&&ended_key.exchange(bits)!=bits) {
            AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_RUN_END;event.source_rva=0x15a8b80;event.run_key=key;event.update_thread_id=GetCurrentThreadId();event.tick_ms=GetTickCount64();event.content_kind=AAMOD_GAMEPLAY_CONTENT_RUN_RESULT;event.content_id=AAMOD_GAMEPLAY_RUN_ABANDONED;gameplay_events_record(event);
        }
    }
}
bool defeated_context(void* frame,double* key) {
    __try {
        double health=*(double*)((unsigned char*)frame+0x1c4ff0);
        return std::isfinite(health)&&health<=0&&run_key(frame,key);
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
double defeat(void* frame) {
    AAModGameState state={};double key=0;
    bool capture=_ReturnAddress()==game_base+0x1dfd6a4&&available.load()&&game_state_query(&state,sizeof(state))==0&&state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present&&defeated_context(frame,&key);
    // This wrapper returns a native double in XMM0. Preserve that ABI exactly.
    double result=original_defeat(frame);
    if(capture&&available.load()) {
        double current_key=0;uint64_t bits;memcpy(&bits,&key,sizeof(bits));
        if(run_key(frame,&current_key)&&current_key==key&&ended_key.exchange(bits)!=bits) {
            AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_RUN_END;event.source_rva=0x3505770;event.content_kind=AAMOD_GAMEPLAY_CONTENT_RUN_RESULT;event.content_id=AAMOD_GAMEPLAY_RUN_DEFEAT;event.run_key=key;event.update_thread_id=GetCurrentThreadId();event.tick_ms=GetTickCount64();gameplay_events_record(event);
        }
    }
    return result;
}
}
bool native_events_available(){return available.load();}
void native_events_initialize() {
    AAModGameInfo identity={};game_profile_query(&identity,sizeof(identity));
    if(identity.identity_status!=AAMOD_GAME_IDENTITY_MATCH){gameplay_events_status(AAMOD_STATE_UNSUPPORTED,0);return;}
    char enable[8]={};if(GetEnvironmentVariableA("AAMOD_EXPERIMENTAL_EVENTS",enable,sizeof(enable))!=1||enable[0]!='1'){gameplay_events_status(AAMOD_STATE_WAITING,0);return;}
    auto base=(unsigned char*)GetModuleHandleW(nullptr);game_base=base;target=base+0x31ddff0;
    // RCX is the frame. The sole direct caller at 0x31ddfa2 ignores the
    // return value; entry consumes no other incoming argument. Verified
    // prologue and full function evidence are kept outside distribution.
    const unsigned char prefix[]={0x48,0x8b,0xc4,0x56,0x48,0x81,0xec,0xb0,0,0,0,0x0f,0x29,0x78,0xd8};
    if(memcmp(target,prefix,sizeof(prefix))||!hook::install(target,(void*)start,(void**)&original_start)) {
        gameplay_events_status(AAMOD_STATE_FAULT,0);target=nullptr;AAMOD_ERROR("native events: run start entry changed or hook rejected");return;
    }
    uint32_t supported=AAMOD_GAMEPLAY_RUN_START;damage_target=base+0x14b8a20;
    const unsigned char damage_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x99,0xc0,0,0,0};
    const unsigned char damage_call[]={0xe8,0x2a,0x8e,0x39,0xfe};
    // The generic dispatcher is frame-only. Publish only its call at 311fbf1
    // from the native hit function; rounding/stat/menu/mod callers are excluded.
    // Hardware writes and independent dispatcher traces verified this source.
    if(!memcmp(base+0x311fbf1,damage_call,sizeof(damage_call))&&!memcmp(damage_target,damage_prefix,sizeof(damage_prefix))&&hook::install(damage_target,(void*)damage,(void**)&original_damage))supported|=AAMOD_GAMEPLAY_DAMAGE;
    else {damage_target=nullptr;AAMOD_ERROR("native events: damage entry changed or hook rejected; run start remains available");}
    defeat_target=base+0x3505770;
    const unsigned char defeat_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x99,0x18,0x07,0,0};
    const unsigned char defeat_call[]={0xe8,0xcc,0x80,0x70,0x01};
    // The sole native caller is the observed P1 death path. This only supports
    // defeat endings; victory/abandonment are not inferred from this source.
    if(!memcmp(base+0x1dfd69f,defeat_call,sizeof(defeat_call))&&!memcmp(defeat_target,defeat_prefix,sizeof(defeat_prefix))&&hook::install(defeat_target,(void*)defeat,(void**)&original_defeat))supported|=AAMOD_GAMEPLAY_RUN_END;
    else {defeat_target=nullptr;AAMOD_ERROR("native events: defeat entry changed or hook rejected");}
    pickup_target=base+0x200ae00;
    const unsigned char pickup_prefix[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57};
    const unsigned char pickup_call[]={0xe8,0x1e,0,0,0};
    // Hardware writes at 200b2eb and its sole proximity caller establish the
    // crystal collector source. Currency rewards/spending callers are excluded.
    if(!memcmp(base+0x200addd,pickup_call,sizeof(pickup_call))&&!memcmp(pickup_target,pickup_prefix,sizeof(pickup_prefix))&&hook::install(pickup_target,(void*)pickup,(void**)&original_pickup))supported|=AAMOD_GAMEPLAY_PICKUP;
    else {pickup_target=nullptr;AAMOD_ERROR("native events: crystal collector entry changed or hook rejected");}
    victory_target=base+0x35057b0;
    const unsigned char victory_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    const unsigned char victory_call[]={0xe8,0xef,0xd6,0x19,0x02};
    // The sole wrapper supplies native outcome 1 in XMM1 to lastRunState.
    // This source is installed experimentally; live victory certification is separate.
    if(!memcmp(base+0x13680bc,victory_call,sizeof(victory_call))&&!memcmp(victory_target,victory_prefix,sizeof(victory_prefix))&&hook::install(victory_target,(void*)victory,(void**)&original_victory))supported|=AAMOD_GAMEPLAY_RUN_END;
    else {victory_target=nullptr;AAMOD_ERROR("native events: victory wrapper changed or hook rejected");}
    abandon_target=base+0x15a8b80;
    const unsigned char abandon_prefix[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    const unsigned char abandon_call[]={0xe8,0xe1,0x9d,0x84,0xff};
    if(!memcmp(base+0x1d5ed9a,abandon_call,sizeof(abandon_call))&&!memcmp(abandon_target,abandon_prefix,sizeof(abandon_prefix))&&hook::install(abandon_target,(void*)abandon,(void**)&original_abandon))supported|=AAMOD_GAMEPLAY_RUN_END;
    else {abandon_target=nullptr;AAMOD_ERROR("native events: abandonment entry changed or hook rejected");}
    gameplay_events_status(AAMOD_STATE_READY,supported);available.store(true);AAMOD_INFO("native events: hooks ready, supported=%u",supported);
}
void native_events_shutdown() {
    available.store(false);gameplay_events_status(AAMOD_STATE_STOPPED,0);
    if(abandon_target&&!hook::remove(abandon_target)){AAMOD_ERROR("native events: abandonment hook removal failed; core and trampoline retained");}else abandon_target=nullptr;
    if(victory_target&&!hook::remove(victory_target)){AAMOD_ERROR("native events: victory hook removal failed; core and trampoline retained");}else victory_target=nullptr;
    if(pickup_target&&!hook::remove(pickup_target)){AAMOD_ERROR("native events: pickup hook removal failed; core and trampoline retained");}else pickup_target=nullptr;
    if(defeat_target&&!hook::remove(defeat_target)){AAMOD_ERROR("native events: defeat hook removal failed; core and trampoline retained");}else defeat_target=nullptr;
    if(damage_target&&!hook::remove(damage_target)){AAMOD_ERROR("native events: damage hook removal failed; core and trampoline retained");}else damage_target=nullptr;
    if(target&&!hook::remove(target)){AAMOD_ERROR("native events: hook removal failed; core and trampoline retained");return;}
    // A detour already entered before removal may still forward through this
    // pointer. The core and retired trampoline live until process exit.
    target=nullptr;
}
}
