#ifndef AAMOD_COMMANDS_H
#define AAMOD_COMMANDS_H
#include <stdint.h>
#define AAMOD_COMMAND_HEAL 1u
#define AAMOD_COMMAND_REFILL_MANA 2u /* P1, single-player active run only */
#define AAMOD_COMMAND_PENDING 0u
#define AAMOD_COMMAND_APPLIED 1u
#define AAMOD_COMMAND_STALE 2u
#define AAMOD_COMMAND_UNAVAILABLE 3u
#define AAMOD_COMMAND_CANCELED 4u
#define AAMOD_COMMAND_FAULT 5u
#define AAMOD_ERR_COMMAND_FULL 6u
#define AAMOD_ERR_COMMAND_OWNER 7u
#define AAMOD_ERR_COMMAND_RESULT 8u
#define AAMOD_ERR_COMMAND_UNAVAILABLE 9u
typedef struct AAModCommand {
    uint32_t size;
    uint32_t kind;
    uint64_t scene_epoch;
    uint32_t player; /* 0=P1, 1=P2 */
    uint32_t reserved;
    double amount; /* finite positive amount, clamped to current maximum */
} AAModCommand;
typedef struct AAModCommandResult {
    uint64_t id;
    uint32_t status;
    uint32_t player;
    uint64_t scene_epoch;
    double before;
    double after;
    uint32_t update_thread_id;
    uint32_t reserved;
} AAModCommandResult;
/* owner is api->command_owner, a core-issued plugin token, not a game pointer.
 * OK means queued, not applied. Query the result; no callbacks are retained.
 * APIs are thread-safe. Owner revocation cancels queued work on unload/failure.
 * Result storage retains 128 request records globally; expired IDs explicitly
 * return COMMAND_RESULT. Never reuse tokens across a core lifetime. */
typedef uint32_t (*AAModCommandSubmitFn)(uint64_t owner, const AAModCommand*, uint32_t, uint64_t* id);
typedef uint32_t (*AAModCommandResultFn)(uint64_t owner, uint64_t id, AAModCommandResult*, uint32_t);
#endif
