#include "../src/core/game_combat.cpp"
#include "../src/core/game_commands.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <limits>
static bool unused_health(void*,unsigned,double){assert(false);return false;}
int main() {
    using namespace aamod;
    std::vector<unsigned char> frame(0x1c5020);auto mana=(double*)(frame.data()+0x1c5010);mana[0]=2;mana[1]=6;
    AAModGameState state={};state.status=AAMOD_STATE_READY;state.scene_index=268;state.scene_epoch=5;state.sequence=10;
    state.players[0]={1,AAMOD_PLAYER_HEALTH,0,0,25,100};state.update_thread_id=GetCurrentThreadId();
    game_combat_sample(frame.data(),state);AAModCombatState snapshot={};assert(game_combat_query(&snapshot,sizeof(snapshot))==0);
    assert(snapshot.valid&&snapshot.mana==2&&snapshot.max_mana==6&&snapshot.scene_epoch==5&&snapshot.sequence==10);
    auto owner=game_commands_owner();game_commands_activate(owner);game_commands_start();
    AAModCommand request={sizeof(request),AAMOD_COMMAND_REFILL_MANA,5,0,0,3};uint64_t id;AAModCommandResult result={};
    auto submit=[&](){assert(game_commands_submit(owner,&request,sizeof(request),&id)==0);game_commands_process(frame.data(),&state,unused_health,game_combat_refill);assert(game_commands_result(owner,id,&result,sizeof(result))==0);};
    submit();assert(result.status==AAMOD_COMMAND_APPLIED&&result.before==2&&result.after==5&&mana[0]==5);
    request.amount=100;submit();assert(result.status==AAMOD_COMMAND_APPLIED&&mana[0]==6);
    request.scene_epoch=4;submit();assert(result.status==AAMOD_COMMAND_STALE&&mana[0]==6);request.scene_epoch=5;
    state.players[1].present=1;submit();assert(result.status==AAMOD_COMMAND_UNAVAILABLE);state.players[1].present=0;
    state.scene_index=266;submit();assert(result.status==AAMOD_COMMAND_UNAVAILABLE);state.scene_index=268;
    state.players[0].health=0;submit();assert(result.status==AAMOD_COMMAND_UNAVAILABLE);state.players[0].health=25;
    mana[0]=-1;submit();assert(result.status==AAMOD_COMMAND_FAULT&&mana[0]==-1);
    mana[0]=2;mana[1]=std::numeric_limits<double>::quiet_NaN();submit();assert(result.status==AAMOD_COMMAND_FAULT&&mana[0]==2);
    game_combat_sample(frame.data(),state);game_combat_query(&snapshot,sizeof(snapshot));assert(snapshot.status==AAMOD_STATE_FAULT&&!snapshot.valid&&snapshot.mana==0);
    request.player=1;assert(game_commands_submit(owner,&request,sizeof(request),&id)==AAMOD_ERR_ARGUMENT);
    game_commands_revoke(owner);game_commands_stop();game_combat_status(AAMOD_STATE_STOPPED);game_combat_query(&snapshot,sizeof(snapshot));assert(!snapshot.valid&&snapshot.status==AAMOD_STATE_STOPPED);
    assert(game_combat_query(&snapshot,sizeof(snapshot)-1)==AAMOD_ERR_ARGUMENT);
    puts("combat: mana clamp, native binding, owner queue, stale/dead/multiplayer/hub refusal and invalid native values passed");
}
