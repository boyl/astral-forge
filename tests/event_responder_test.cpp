#include "aamod/aamod.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#undef NDEBUG
#include <cassert>
static DWORD fake_wait(HANDLE,DWORD);
static ULONGLONG fake_tick(){return 10000;}
#define WaitForSingleObject fake_wait
#define GetTickCount64 fake_tick
#include "../mods/event_responder/mod.cpp"
#undef WaitForSingleObject
#undef GetTickCount64
static unsigned iteration,queries,submissions,result_queries,scenario;
static uint64_t observed_cursor;
static AAModCommand submitted;
static std::string messages;
static DWORD fake_wait(HANDLE,DWORD){return iteration++<2?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static void logging(int,const char* format,...) {
    char text[1024];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);messages+=text;messages+='\n';
}
static uint32_t events_query(uint64_t cursor,AAModGameplayEvent* values,uint32_t capacity,uint32_t size,uint32_t* count,AAModGameplayInfo* info,uint32_t) {
    assert(capacity==16&&size==sizeof(*values));observed_cursor=cursor;++queries;*count=0;info->supported_kinds=3;
    if(scenario==8&&queries==1){info->newest=70;return AAMOD_ERR_EVENT_CURSOR;}
    if(queries!=1)return 0;
    auto& value=values[0];value={};value.id=51;value.kind=AAMOD_GAMEPLAY_DAMAGE;value.tick_ms=9900;value.amount=20;*count=1;
    if(scenario==1)value.tick_ms=7000;
    if(scenario==2)value.player=1;
    if(scenario==3)info->supported_kinds=1;
    if(scenario==9){value.kind=AAMOD_GAMEPLAY_PICKUP;info->supported_kinds=7;}
    if(scenario==10){values[1]=value;values[1].id=52;*count=2;}
    return 0;
}
static uint32_t state_query(AAModGameState* value,uint32_t) {
    *value={};value->status=2;value->scene_index=268;value->scene_epoch=17;value->players[0].present=1;value->players[0].health=80;value->players[0].valid_fields=AAMOD_PLAYER_HEALTH;
    if(scenario==4)value->players[0].health=0;
    if(scenario==5)value->players[1].present=1;
    if(scenario==6)value->scene_index=267;
    if(scenario==7)value->status=AAMOD_STATE_WAITING;
    return 0;
}
static uint32_t submit(uint64_t owner,const AAModCommand* value,uint32_t size,uint64_t* id) {
    assert(owner==5&&size==sizeof(*value));++submissions;submitted=*value;*id=99;return 0;
}
static uint32_t command_query(uint64_t owner,uint64_t id,AAModCommandResult* value,uint32_t size) {
    assert(owner==5&&id==99&&size==sizeof(*value));++result_queries;*value={};value->status=1;value->before=80;value->after=100;value->update_thread_id=27;return 0;
}
int main() {
    AAModAPI sdk={};sdk.log=logging;sdk.command_owner=5;sdk.gameplay_events=events_query;sdk.game_state=state_query;sdk.command_submit=submit;sdk.command_result=command_query;api=&sdk;
    for(scenario=0;scenario<=10;++scenario) {
        iteration=queries=submissions=result_queries=0;observed_cursor=0;submitted={};messages.clear();initial_cursor=50;
        assert(run(nullptr)==0);
        const bool expected=scenario==0||scenario==9||scenario==10;
        assert(submissions==(expected?1u:0u));assert(result_queries==(expected?1u:0u));
        if(expected){assert(submitted.scene_epoch==17&&submitted.player==0);assert(submitted.kind==(scenario==9?2u:1u));assert(submitted.amount==(scenario==9?1.0:20.0));assert(messages.find("status=1")!=std::string::npos);}
        if(scenario==8){assert(observed_cursor==70&&messages.find("lost history")!=std::string::npos);}
        else assert(observed_cursor==(scenario==10?52u:51u));
        if(scenario==10)assert(messages.find("skipped while request pending")!=std::string::npos);
    }
    puts("event responder: recent supported P1 events, no historical replay, dead/multiplayer/scene/status exclusions, cursor recovery and one pending request passed");
}

