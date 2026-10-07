#include "aamod/aamod.h"
#include <windows.h>
#include <cstdio>
static const AAModAPI* api;static HANDLE stop,worker;
static bool waiting(DWORD ms){return WaitForSingleObject(stop,ms)==WAIT_TIMEOUT;}
static bool equipment(uint32_t kind,uint32_t target,uint32_t expected,uint64_t epoch) {
    AAModEquipmentCommand request={sizeof(request),kind,epoch,0,target,expected,0};uint64_t id;
    uint32_t error=api->equipment_submit(api->command_owner,&request,sizeof(request),&id);
    if(error){AAMOD_LOGE(api,"content_smoke: equipment submit error=%u",error);return false;}
    for(unsigned i=0;i<100&&waiting(50);++i) {
        AAModEquipmentResult result={};error=api->equipment_result(api->command_owner,id,&result,sizeof(result));
        if(error)return false;if(result.status==AAMOD_EQUIPMENT_PENDING)continue;
        AAMOD_LOGI(api,"content_smoke: equipment kind=%u status=%u before=%u after=%u thread=%u checkpoint=%u",kind,result.status,result.before,result.after,result.update_thread_id,result.checkpoint_status);
        return result.status==AAMOD_EQUIPMENT_APPLIED&&result.after==target;
    }return false;
}
static bool command(uint32_t kind,double amount,uint64_t epoch) {
    AAModCommand request={sizeof(request),kind,epoch,0,0,amount};uint64_t id;
    if(api->command_submit(api->command_owner,&request,sizeof(request),&id))return false;
    for(unsigned i=0;i<100&&waiting(50);++i) {
        AAModCommandResult result={};if(api->command_result(api->command_owner,id,&result,sizeof(result)))return false;
        if(result.status==AAMOD_COMMAND_PENDING)continue;
        AAMOD_LOGI(api,"content_smoke: command kind=%u status=%u before=%.3f after=%.3f thread=%u",kind,result.status,result.before,result.after,result.update_thread_id);
        return result.status==AAMOD_COMMAND_APPLIED&&result.after>result.before;
    }return false;
}
static DWORD WINAPI run(void*) {
    AAModEquipment original={};AAModGameState state={};
    while(waiting(100)) {
        if(api->equipment(&original,sizeof(original))||api->game_state(&state,sizeof(state)))return 1;
        if(original.valid&&state.status==AAMOD_STATE_READY&&original.scene_epoch==state.scene_epoch&&state.players[0].health>0)break;
    }
    if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0)return 0;
    char preset[256];int length=std::snprintf(preset,sizeof(preset),"{\"version\":1,\"auras\":[%u,%u,%u,%u,%u],\"spells\":[%u,%u,%u,%u]}",original.auras[0],original.auras[1],original.auras[2],original.auras[3],original.auras[4],original.spells[0],original.spells[1],original.spells[2],original.spells[3]);
    if(api->data_write(api->data_owner,"original-build",preset,(uint32_t)length))return 1;
    double maximum=state.players[0].max_health;uint64_t epoch=original.scene_epoch;
    // Native Wild Breath I, an existing +30 maximum-health aura. Deliberately
    // choose a measurable effect rather than accepting only the changed id.
    if(!equipment(AAMOD_CONTENT_AURA,246,original.auras[0],epoch))return 1;
    bool observed=false;
    for(unsigned i=0;i<100&&waiting(50);++i) {
        api->game_state(&state,sizeof(state));
        if(state.players[0].max_health==maximum+30){observed=true;break;}
    }
    if(!observed){AAMOD_LOGE(api,"content_smoke: aura derived health maximum not updated");return 1;}
    AAMOD_LOGI(api,"content_smoke: phase=aura-effect max_before=%.3f max_after=%.3f",maximum,state.players[0].max_health);
    if(!waiting(12000))return 0;
    if(state.players[0].health<state.players[0].max_health && !command(AAMOD_COMMAND_HEAL,30,epoch))return 1;
    if(!waiting(3000))return 0;
    if(!equipment(AAMOD_CONTENT_AURA,original.auras[0],246,epoch))return 1;
    AAMOD_LOGI(api,"content_smoke: phase=aura-restored");
    if(!equipment(AAMOD_CONTENT_SPELL,31,original.spells[0],epoch))return 1;
    AAMOD_LOGI(api,"content_smoke: phase=spell-replaced id=31 gambits=cleared");
    AAModCombatState combat={};
    for(unsigned i=0;i<1200&&waiting(50);++i) {
        if(api->combat_state(&combat,sizeof(combat)))return 1;
        if(combat.valid&&combat.mana<combat.max_mana)break;
    }
    if(!combat.valid||combat.mana>=combat.max_mana){AAMOD_LOGE(api,"content_smoke: no real spell-resource consumption observed");return 1;}
    if(!waiting(2000)||!command(AAMOD_COMMAND_REFILL_MANA,combat.max_mana,combat.scene_epoch))return 1;
    if(!waiting(3000))return 0;
    AAModEquipment current={};if(api->equipment(&current,sizeof(current))||!current.valid)return 1;
    if(!equipment(AAMOD_CONTENT_SPELL,original.spells[0],31,current.scene_epoch))return 1;
    AAMOD_LOGI(api,"content_smoke: phase=completed aura/spell ids restored; selected spell gambits remain cleared by documented policy");return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input,uint32_t size) {
    if(!input||!AAMOD_API_HAS(input,equipment_result)||!input->data_owner)return AAMOD_ERR_ABI;api=input;
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop)return AAMOD_ERR_GENERIC;
    worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);if(!worker){CloseHandle(stop);return AAMOD_ERR_GENERIC;}return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void){SetEvent(stop);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stop);}
