#ifndef AAMOD_EQUIPMENT_COMMANDS_H
#define AAMOD_EQUIPMENT_COMMANDS_H
#include <stdint.h>
#define AAMOD_ERR_EQUIPMENT_CONFLICT 32u
#define AAMOD_EQUIPMENT_SAVE_CHECKPOINT 1u
#define AAMOD_EQUIPMENT_PENDING 0u
#define AAMOD_EQUIPMENT_APPLIED 1u
#define AAMOD_EQUIPMENT_STALE 2u
#define AAMOD_EQUIPMENT_UNAVAILABLE 3u
#define AAMOD_EQUIPMENT_CONFLICT 4u
#define AAMOD_EQUIPMENT_FAULT 5u
#define AAMOD_EQUIPMENT_CANCELED 6u
#define AAMOD_CHECKPOINT_NOT_REQUESTED 0u
#define AAMOD_CHECKPOINT_PENDING 1u
#define AAMOD_CHECKPOINT_SAVE_DISPATCHED 2u /* native save called; not disk durability proof */
#define AAMOD_CHECKPOINT_FAILED 3u
typedef struct AAModEquipmentCommand {
    uint32_t size;
    uint32_t kind; /* CONTENT_AURA or CONTENT_SPELL */
    uint64_t scene_epoch;
    uint32_t slot; /* zero based: aura 0..4, common spell 0..3 */
    uint32_t id; /* zero clears an aura; spells must be in the common catalog */
    uint32_t expected_id; /* compare current slot before mutation */
    uint32_t flags;
} AAModEquipmentCommand;
typedef struct AAModEquipmentResult {
    uint64_t id;
    uint32_t status;
    uint32_t checkpoint_status;
    uint64_t scene_epoch;
    uint32_t kind;
    uint32_t slot;
    uint32_t before;
    uint32_t after;
    uint32_t update_thread_id;
    uint32_t error;
} AAModEquipmentResult;
/* Queued on the native update thread. Single-player only, commands opt-in.
 * APPLIED describes current-run equipment; CHECKPOINT_PENDING is not saved.
 * Spell replacement clears the four gambit IDs, preserves native slot states, upgrade/identity suffix and character cooldown state, and refreshes the native icon definition.
 * Never repeat a request just because checkpoint synchronization is pending.
 * Owner is command_owner. Results retain the latest 128 records globally. */
typedef uint32_t (*AAModEquipmentSubmitFn)(uint64_t,const AAModEquipmentCommand*,uint32_t,uint64_t*);
typedef uint32_t (*AAModEquipmentResultFn)(uint64_t,uint64_t,AAModEquipmentResult*,uint32_t);
#endif
