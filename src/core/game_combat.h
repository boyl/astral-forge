#pragma once
#include "aamod/aamod.h"
namespace aamod {
void game_combat_status(uint32_t);
void game_combat_sample(void* frame,const AAModGameState&);
uint32_t game_combat_query(AAModCombatState*,uint32_t);
uint32_t game_combat_refill(void*,unsigned,double,double*,double*);
}
