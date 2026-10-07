#pragma once
#include "aamod/aamod.h"
namespace aamod {
uint64_t game_commands_owner();
void game_commands_activate(uint64_t);
void game_commands_revoke(uint64_t);
void game_commands_start();
void game_commands_stop();
bool game_commands_available();
uint32_t game_commands_owner_state(uint64_t); /* 0 revoked, 1 init, 2 active */
uint32_t game_commands_submit(uint64_t, const AAModCommand*, uint32_t, uint64_t*);
uint32_t game_commands_result(uint64_t, uint64_t, AAModCommandResult*, uint32_t);
typedef bool (*HealthWrite)(void*, unsigned, double);
typedef uint32_t (*ManaApply)(void*,unsigned,double,double*,double*);
void game_commands_process(void*, AAModGameState*, HealthWrite,ManaApply=nullptr);
}
