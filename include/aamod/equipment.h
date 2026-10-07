#ifndef AAMOD_EQUIPMENT_H
#define AAMOD_EQUIPMENT_H
#include <stdint.h>
/* Copied native P1 equipment, sampled at the same post-update boundary as
 * game_state. Single player only. No native pointers. Status uses STATE_*;
 * READY with valid=0 means a run equipment dictionary is not available.
 * IDs are game component ids, not catalog indices. Zero aura means empty.
 * This query alone does not certify spell level/cooldown or mutation support. */
typedef struct AAModEquipment {
    uint32_t size;
    uint32_t status;
    uint64_t sequence;
    uint64_t scene_epoch;
    uint32_t valid;
    uint32_t auras[5];
    uint32_t spells[4];
} AAModEquipment;
typedef uint32_t (*AAModEquipmentFn)(AAModEquipment*,uint32_t);
#endif
