#pragma once
#include "aamod/aamod.h"
namespace aamod {
typedef void (*EquipmentApply)(void*,const AAModEquipmentCommand&,AAModEquipmentResult&);
typedef void (*CheckpointSync)(void*,const AAModEquipmentCommand&,AAModEquipmentResult&);
uint32_t equipment_submit(uint64_t,const AAModEquipmentCommand*,uint32_t,uint64_t*);
uint32_t equipment_result(uint64_t,uint64_t,AAModEquipmentResult*,uint32_t);
bool equipment_process(void*,const AAModGameState&,EquipmentApply,CheckpointSync);
void equipment_stop();
void equipment_start();
}
