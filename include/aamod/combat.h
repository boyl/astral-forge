#ifndef AAMOD_COMBAT_H
#define AAMOD_COMBAT_H
#include <stdint.h>
typedef struct AAModCombatState {
    uint32_t size;
    uint32_t status; /* STATE_* */
    uint64_t sequence;
    uint64_t scene_epoch;
    uint32_t valid; /* 1: P1 mana and maximum available in a single-player run */
    uint32_t reserved;
    double mana;
    double max_mana;
} AAModCombatState;
typedef uint32_t (*AAModCombatStateFn)(AAModCombatState*,uint32_t);
#endif
