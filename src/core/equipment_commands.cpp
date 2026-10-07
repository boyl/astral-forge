#include "equipment_commands.h"
#include "game_commands.h"
#include "content_catalog.h"
#include <windows.h>
namespace aamod {
namespace {
SRWLOCK equipment_lock=SRWLOCK_INIT;
struct Record {uint64_t owner;AAModEquipmentCommand command;AAModEquipmentResult result;};
Record records[128]={};uint64_t newest;
bool accepting;
}
uint32_t equipment_submit(uint64_t owner,const AAModEquipmentCommand* request,uint32_t size,uint64_t* id) {
    if(!request||!id||size<sizeof(*request)||request->size!=sizeof(*request)||!request->scene_epoch||
       (request->kind!=AAMOD_CONTENT_AURA&&request->kind!=AAMOD_CONTENT_SPELL)||
       request->slot>=(request->kind==AAMOD_CONTENT_AURA?5u:4u)||request->flags&~AAMOD_EQUIPMENT_SAVE_CHECKPOINT)return AAMOD_ERR_ARGUMENT;
    if(!game_commands_owner_state(owner))return AAMOD_ERR_COMMAND_OWNER;
    if(!game_commands_available()||!content_catalog_available())return AAMOD_ERR_COMMAND_UNAVAILABLE;
    if(!content_catalog_contains(request->kind,request->id))return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockExclusive(&equipment_lock);
    if(!accepting){ReleaseSRWLockExclusive(&equipment_lock);return AAMOD_ERR_COMMAND_UNAVAILABLE;}
    unsigned pending=0;for(const auto& record:records)pending+=record.result.id&&record.result.status==AAMOD_EQUIPMENT_PENDING;
    auto& record=records[newest%128];
    if(pending>=64 || (record.result.id&&(record.result.status==AAMOD_EQUIPMENT_PENDING||record.result.checkpoint_status==AAMOD_CHECKPOINT_PENDING))) {
        ReleaseSRWLockExclusive(&equipment_lock);return AAMOD_ERR_COMMAND_FULL;
    }
    record={};record.owner=owner;record.command=*request;record.result.id=++newest;record.result.kind=request->kind;record.result.slot=request->slot;record.result.scene_epoch=request->scene_epoch;
    *id=newest;ReleaseSRWLockExclusive(&equipment_lock);return AAMOD_OK;
}
uint32_t equipment_result(uint64_t owner,uint64_t id,AAModEquipmentResult* output,uint32_t size) {
    if(!output||size<sizeof(*output)||!id)return AAMOD_ERR_ARGUMENT;
    if(!game_commands_owner_state(owner))return AAMOD_ERR_COMMAND_OWNER;
    AcquireSRWLockShared(&equipment_lock);const auto& record=records[(id-1)%128];
    if(record.result.id!=id||record.owner!=owner){ReleaseSRWLockShared(&equipment_lock);return AAMOD_ERR_COMMAND_RESULT;}
    *output=record.result;ReleaseSRWLockShared(&equipment_lock);return AAMOD_OK;
}
bool equipment_process(void* context,const AAModGameState& state,EquipmentApply apply,CheckpointSync sync) {
    AcquireSRWLockExclusive(&equipment_lock);bool applied_this_update=false;
    uint64_t first=newest>128?newest-127:1;
    for(uint64_t id=first;id<=newest;++id) {
        auto& record=records[(id-1)%128];auto& result=record.result;
        if(result.status!=AAMOD_EQUIPMENT_PENDING&&result.checkpoint_status!=AAMOD_CHECKPOINT_PENDING)continue;
        uint32_t owner=game_commands_owner_state(record.owner);
        if(!owner) {
            if(result.status==AAMOD_EQUIPMENT_PENDING)result.status=AAMOD_EQUIPMENT_CANCELED;
            if(result.checkpoint_status==AAMOD_CHECKPOINT_PENDING)result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;
            continue;
        }
        if(owner!=2)continue;
        if(result.status==AAMOD_EQUIPMENT_PENDING) {
            if(applied_this_update)continue;
            result.update_thread_id=state.update_thread_id;
            if(record.command.scene_epoch!=state.scene_epoch)result.status=AAMOD_EQUIPMENT_STALE;
            else if(state.status!=AAMOD_STATE_READY||state.scene_index!=268||!state.players[0].present||state.players[1].present||state.players[0].health<=0)result.status=AAMOD_EQUIPMENT_UNAVAILABLE;
            else {apply(context,record.command,result);applied_this_update=true;}
        } else if(result.checkpoint_status==AAMOD_CHECKPOINT_PENDING) {
            if(state.status!=AAMOD_STATE_READY||state.scene_index!=268||!state.players[0].present||state.players[1].present)result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;
            else sync(context,record.command,result);
        }
    }ReleaseSRWLockExclusive(&equipment_lock);return applied_this_update;
}
void equipment_stop() {
    AcquireSRWLockExclusive(&equipment_lock);
    accepting=false;
    for(auto& record:records){if(record.result.id&&record.result.status==AAMOD_EQUIPMENT_PENDING)record.result.status=AAMOD_EQUIPMENT_CANCELED;if(record.result.checkpoint_status==AAMOD_CHECKPOINT_PENDING)record.result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;}
    ReleaseSRWLockExclusive(&equipment_lock);
}
void equipment_start(){AcquireSRWLockExclusive(&equipment_lock);accepting=true;ReleaseSRWLockExclusive(&equipment_lock);}
}
