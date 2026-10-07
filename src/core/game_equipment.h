#pragma once
#include "aamod/aamod.h"
namespace aamod {
void game_equipment_sample(unsigned char* base,const AAModGameState& state);
void game_equipment_status(uint32_t status);
uint32_t game_equipment_query(AAModEquipment*,uint32_t);
bool game_equipment_fields(unsigned char* base,double** slots,void** gambits);
}
