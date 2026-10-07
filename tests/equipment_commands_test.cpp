#include "../src/core/equipment_commands.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
static bool commands_ready=true,catalog_ready=true;
static uint32_t owner_state=1,slot_value=7,calls,sync_calls;
namespace aamod {
uint32_t game_commands_owner_state(uint64_t owner){return owner==1?owner_state:owner==2?2u:0u;}
bool game_commands_available(){return commands_ready;}
bool content_catalog_available(){return catalog_ready;}
bool content_catalog_contains(uint32_t kind,uint32_t id){return kind==1?(id==0||id==7||id==8):id==30;}
}
static void apply(void*,const AAModEquipmentCommand& request,AAModEquipmentResult& result) {
    ++calls;result.before=slot_value;result.after=slot_value;
    if(slot_value!=request.expected_id){result.status=AAMOD_EQUIPMENT_CONFLICT;return;}
    slot_value=request.id;result.after=request.id;result.status=AAMOD_EQUIPMENT_APPLIED;
    if(request.flags)result.checkpoint_status=AAMOD_CHECKPOINT_PENDING;
}
static void sync(void*,const AAModEquipmentCommand&,AAModEquipmentResult& result){++sync_calls;result.checkpoint_status=AAMOD_CHECKPOINT_SAVE_DISPATCHED;}
int main() {
    using namespace aamod;
    AAModGameState state={};state.status=2;state.scene_index=268;state.scene_epoch=5;state.players[0].present=1;state.players[0].health=10;state.update_thread_id=17;
    AAModEquipmentCommand request={sizeof(request),1,5,0,8,7,1};uint64_t id=999;AAModEquipmentResult result={};
    assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_COMMAND_UNAVAILABLE&&id==999);equipment_start();
    assert(equipment_submit(1,&request,sizeof(request),&id)==0);
    assert(!equipment_process(nullptr,state,apply,sync)&&!calls);owner_state=2;
    assert(equipment_process(nullptr,state,apply,sync)&&calls==1&&slot_value==8);
    assert(equipment_result(1,id,&result,sizeof(result))==0&&result.status==1&&result.checkpoint_status==1&&result.before==7&&result.after==8&&result.update_thread_id==17);
    assert(equipment_result(2,id,&result,sizeof(result))==AAMOD_ERR_COMMAND_RESULT);
    assert(!equipment_process(nullptr,state,apply,sync)&&sync_calls==1&&calls==1);
    assert(equipment_result(1,id,&result,sizeof(result))==0&&result.checkpoint_status==AAMOD_CHECKPOINT_SAVE_DISPATCHED);
    request.flags=0;request.expected_id=7;
    assert(equipment_submit(1,&request,sizeof(request),&id)==0);equipment_process(nullptr,state,apply,sync);equipment_result(1,id,&result,sizeof(result));assert(result.status==AAMOD_EQUIPMENT_CONFLICT&&slot_value==8);
    request.scene_epoch=4;assert(equipment_submit(1,&request,sizeof(request),&id)==0);assert(!equipment_process(nullptr,state,apply,sync));equipment_result(1,id,&result,sizeof(result));assert(result.status==AAMOD_EQUIPMENT_STALE);request.scene_epoch=5;
    state.players[1].present=1;assert(equipment_submit(1,&request,sizeof(request),&id)==0);equipment_process(nullptr,state,apply,sync);equipment_result(1,id,&result,sizeof(result));assert(result.status==AAMOD_EQUIPMENT_UNAVAILABLE);state.players[1].present=0;
    request.id=99;assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_ARGUMENT);request.id=8;
    request.slot=5;assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_ARGUMENT);request.slot=0;
    request.flags=2;assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_ARGUMENT);request.flags=0;
    commands_ready=false;assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_COMMAND_UNAVAILABLE);commands_ready=true;
    owner_state=1;
    for(int i=0;i<64;++i)assert(equipment_submit(1,&request,sizeof(request),&id)==0);
    assert(equipment_submit(1,&request,sizeof(request),&id)==AAMOD_ERR_COMMAND_FULL);
    equipment_stop();assert(equipment_result(1,id,&result,sizeof(result))==0&&result.status==AAMOD_EQUIPMENT_CANCELED);
    owner_state=0;assert(equipment_result(1,id,&result,sizeof(result))==AAMOD_ERR_COMMAND_OWNER);
    puts("equipment commands: init activation, ownership, conflict/stale/multiplayer, pending checkpoint and queue bounds passed");
}
