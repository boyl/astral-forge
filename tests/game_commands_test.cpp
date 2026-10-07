#include "../src/core/game_commands.cpp"
#include <cstdio>
#include <limits>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed %d: %s\n",__LINE__,#x); return 1; } } while(0)
static unsigned writes;
static bool fail_write;
static bool write(void* frame, unsigned player, double value) {
    ++writes; if (fail_write) return false; ((double*)frame)[player] = value; return true;
}
int main() {
    using namespace aamod;
    auto owner = game_commands_owner(), other = game_commands_owner();
    AAModCommand command = {sizeof(command), AAMOD_COMMAND_HEAL, 4, 0, 0, 25};
    AAModGameState state = {}; state.status = AAMOD_STATE_READY; state.scene_index = 268;
    state.scene_epoch = 4; state.update_thread_id = GetCurrentThreadId();
    state.players[0] = {1,AAMOD_PLAYER_HEALTH,0,0,50,100};
    state.players[1] = {1,AAMOD_PLAYER_HEALTH,0,0,10,200};
    double native[2] = {50,10}; uint64_t id = 999; AAModCommandResult result = {};
    CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == AAMOD_ERR_COMMAND_UNAVAILABLE && id == 999);
    game_commands_start();
    CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0);
    game_commands_process(native,&state,write); CHECK(!writes);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_PENDING);
    game_commands_activate(owner); game_commands_process(native,&state,write);
    CHECK(writes == 1 && native[0] == 75 && state.players[0].health == 75);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_APPLIED && result.before == 50 && result.after == 75 && result.update_thread_id == GetCurrentThreadId());
    game_commands_process(native,&state,write); CHECK(writes == 1);
    CHECK(game_commands_result(other,id,&result,sizeof(result)) == AAMOD_ERR_COMMAND_RESULT);
    command.amount = 1000; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0);
    game_commands_process(native,&state,write); CHECK(native[0] == 100);
    command.player = 1; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0);
    game_commands_process(native,&state,write); CHECK(native[1] == 200 && native[0] == 100);
    command.scene_epoch = 3; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0);
    game_commands_process(native,&state,write);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_STALE);
    command.scene_epoch = 4; command.player = 0; state.players[0].health = 0;
    CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0); game_commands_process(native,&state,write);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_UNAVAILABLE && native[0] == 100);
    state.players[0].health = 50; fail_write = true;
    CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0); game_commands_process(native,&state,write);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_FAULT);
    fail_write = false;
    for (double value : {0.0,-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        command.amount = value; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == AAMOD_ERR_ARGUMENT);
    }
    command.amount = 1; command.player = 2; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == AAMOD_ERR_ARGUMENT);
    command.player = 0; command.kind = 999; CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == AAMOD_ERR_ARGUMENT);
    command.kind = AAMOD_COMMAND_HEAL;
    for (unsigned i = 0; i < 64; ++i) CHECK(game_commands_submit(owner,&command,sizeof(command),&id) == 0);
    uint64_t untouched = 777; CHECK(game_commands_submit(owner,&command,sizeof(command),&untouched) == AAMOD_ERR_COMMAND_FULL && untouched == 777);
    game_commands_revoke(owner); auto before = writes; game_commands_process(native,&state,write); CHECK(writes == before);
    CHECK(game_commands_result(owner,id,&result,sizeof(result)) == AAMOD_ERR_COMMAND_OWNER);
    game_commands_activate(other);
    CHECK(game_commands_submit(other,&command,sizeof(command),&id) == 0); auto expired = id;
    game_commands_process(native,&state,write);
    for (unsigned i = 0; i < 130; ++i) { CHECK(game_commands_submit(other,&command,sizeof(command),&id) == 0); game_commands_process(native,&state,write); }
    CHECK(game_commands_result(other,expired,&result,sizeof(result)) == AAMOD_ERR_COMMAND_RESULT);
    CHECK(game_commands_submit(other,&command,sizeof(command),&id) == 0); game_commands_stop();
    CHECK(game_commands_result(other,id,&result,sizeof(result)) == 0 && result.status == AAMOD_COMMAND_CANCELED);
    CHECK(game_commands_submit(other,&command,sizeof(command),&id) == AAMOD_ERR_COMMAND_UNAVAILABLE);
    game_commands_revoke(other); puts("controlled command contracts passed"); return 0;
}
