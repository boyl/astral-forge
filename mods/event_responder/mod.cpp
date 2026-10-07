#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api;
static HANDLE stop,worker;
static uint64_t initial_cursor;
static DWORD WINAPI run(void*) {
    uint64_t cursor=initial_cursor,pending=0,source=0;
    while(WaitForSingleObject(stop,50)==WAIT_TIMEOUT) {
        if(pending) {
            AAModCommandResult result={};
            auto error=api->command_result(api->command_owner,pending,&result,sizeof(result));
            if(error){AAMOD_LOGE(api,"event_responder: result error=%u",error);return 1;}
            if(result.status==AAMOD_COMMAND_PENDING)continue;
            AAMOD_LOGI(api,"event_responder: event=%llu request=%llu status=%u before=%.3f after=%.3f thread=%u",(unsigned long long)source,(unsigned long long)pending,result.status,result.before,result.after,result.update_thread_id);
            pending=0;
        }
        AAModGameplayEvent events[16]={};AAModGameplayInfo info={};uint32_t count=0;
        auto error=api->gameplay_events(cursor,events,16,sizeof(events[0]),&count,&info,sizeof(info));
        if(error==AAMOD_ERR_EVENT_CURSOR){AAMOD_LOGW(api,"event_responder: lost history; cursor=%llu",(unsigned long long)info.newest);cursor=info.newest;continue;}
        if(error){AAMOD_LOGE(api,"event_responder: events error=%u",error);return 1;}
        for(uint32_t i=0;i<count;++i) {
            const auto& event=events[i];cursor=event.id;
            if(event.player||GetTickCount64()-event.tick_ms>2000||!(info.supported_kinds&event.kind))continue;
            uint32_t kind=event.kind==AAMOD_GAMEPLAY_DAMAGE?AAMOD_COMMAND_HEAL:event.kind==AAMOD_GAMEPLAY_PICKUP?AAMOD_COMMAND_REFILL_MANA:0;
            if(!kind)continue;
            if(pending){AAMOD_LOGW(api,"event_responder: event=%llu skipped while request pending",(unsigned long long)event.id);continue;}
            AAModGameState state={};error=api->game_state(&state,sizeof(state));
            if(error){AAMOD_LOGE(api,"event_responder: state error=%u",error);return 1;}
            const auto& player=state.players[0];
            if(state.status!=AAMOD_STATE_READY||state.scene_index!=268||!player.present||state.players[1].present||!(player.valid_fields&AAMOD_PLAYER_HEALTH)||player.health<=0)continue;
            AAModCommand command={sizeof(command),kind,state.scene_epoch,0,0,kind==AAMOD_COMMAND_HEAL?event.amount:1.0};
            error=api->command_submit(api->command_owner,&command,sizeof(command),&pending);
            if(error){AAMOD_LOGW(api,"event_responder: event=%llu submit error=%u",(unsigned long long)event.id,error);continue;}
            source=event.id;AAMOD_LOGI(api,"event_responder: event=%llu kind=%u queued=%llu amount=%.3f",(unsigned long long)source,kind,(unsigned long long)pending,command.amount);
        }
    }
    return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* value,uint32_t size) {
    if(!value||size<328||value->api_version!=AAMOD_ABI_VERSION)return AAMOD_ERR_ABI;
    api=value;AAModGameplayInfo info={};uint32_t count=0;
    auto error=api->gameplay_events(0,nullptr,0,sizeof(AAModGameplayEvent),&count,&info,sizeof(info));
    if(error)return error;initial_cursor=info.newest; // Do not replay earlier events when loading.
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop)return AAMOD_ERR_GENERIC;
    worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);
    if(!worker){CloseHandle(stop);stop=nullptr;return AAMOD_ERR_GENERIC;}
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown() {
    SetEvent(stop);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stop);worker=stop=nullptr;
}
