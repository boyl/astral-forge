#include "game_combat.h"
#include <windows.h>
#include <cmath>
#include <algorithm>
namespace aamod {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
AAModCombatState cache={};
bool read_mana(void* frame,double* value,double* maximum) {
    __try {
        auto bytes=(unsigned char*)frame;
        // Native gameProgress bindings in exact 2.6.4 constructor:
        // player_1_mana__progress / player_1_manaMax__progress.
        *value=*(double*)(bytes+0x1c5010);*maximum=*(double*)(bytes+0x1c5018);
        return std::isfinite(*value)&&std::isfinite(*maximum)&&*maximum>0&&*value>=0&&*value<=*maximum;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void publish(const AAModCombatState& value){AcquireSRWLockExclusive(&lock);cache=value;ReleaseSRWLockExclusive(&lock);}
}
void game_combat_status(uint32_t status){AAModCombatState value={};value.size=sizeof(value);value.status=status;publish(value);}
void game_combat_sample(void* frame,const AAModGameState& state) {
    AAModCombatState value={};value.size=sizeof(value);value.status=state.status;value.sequence=state.sequence;value.scene_epoch=state.scene_epoch;
    if(state.status==AAMOD_STATE_READY&&state.scene_index==268&&state.players[0].present&&!state.players[1].present) {
        if(read_mana(frame,&value.mana,&value.max_mana))value.valid=1;
        else {value.mana=value.max_mana=0;value.status=AAMOD_STATE_FAULT;}
    }publish(value);
}
uint32_t game_combat_query(AAModCombatState* out,uint32_t size) {
    if(!out||size<sizeof(*out))return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock);*out=cache;ReleaseSRWLockShared(&lock);return AAMOD_OK;
}
uint32_t game_combat_refill(void* frame,unsigned player,double amount,double* before,double* after) {
    if(player)return AAMOD_COMMAND_UNAVAILABLE;
    double value,maximum;if(!read_mana(frame,&value,&maximum))return AAMOD_COMMAND_FAULT;
    __try {
        double result=(std::min)(maximum,value+amount);
        auto field=(double*)((unsigned char*)frame+0x1c5010);*field=result;
        if(*field!=result)return AAMOD_COMMAND_FAULT;
        *before=value;*after=result;return AAMOD_COMMAND_APPLIED;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return AAMOD_COMMAND_FAULT;}
}
}
