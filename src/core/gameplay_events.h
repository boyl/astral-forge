#pragma once
#include "aamod/aamod.h"
namespace aamod {
void gameplay_events_status(uint32_t status,uint32_t supported);
void gameplay_events_record(AAModGameplayEvent event);
uint32_t gameplay_events_query(uint64_t after,AAModGameplayEvent* output,uint32_t capacity,
    uint32_t event_size,uint32_t* count,AAModGameplayInfo* info,uint32_t info_size);
}
