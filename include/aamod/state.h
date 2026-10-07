#ifndef AAMOD_STATE_H
#define AAMOD_STATE_H
#include <stdint.h>
#define AAMOD_STATE_VERSION 1u
#define AAMOD_STATE_UNSUPPORTED 0u
#define AAMOD_STATE_WAITING 1u
#define AAMOD_STATE_READY 2u
#define AAMOD_STATE_FAULT 3u
#define AAMOD_STATE_STOPPED 4u
#define AAMOD_PLAYER_POSITION 1u
#define AAMOD_PLAYER_HEALTH 2u
/* Copied values only. Coordinates use the collider's native layer space.
 * No object pointer or persistent object handle is part of this contract. */
typedef struct AAModPlayerState {
    uint32_t present;
    uint32_t valid_fields;
    double layer_x;
    double layer_y;
    double health;
    double max_health;
} AAModPlayerState;
typedef struct AAModGameState {
    uint32_t size;
    uint32_t version;
    uint32_t status;
    uint32_t update_thread_id;
    uint64_t sequence;
    uint64_t scene_epoch;
    uint64_t sampled_at_ms; /* GetTickCount64, not wall-clock time */
    int32_t scene_index; /* native engine layout, not procedural room ID */
    uint32_t reserved;
    char scene_name[128]; /* UTF-8 native layout name */
    AAModPlayerState players[2]; /* P1, P2 */
} AAModGameState;
/* Callable from any thread. Copies one coherent snapshot into caller memory.
 * Returns argument error for null/short output, otherwise OK; check status
 * and valid_fields before using values. Bytes after sizeof are untouched. */
typedef uint32_t (*AAModGameStateFn)(AAModGameState*, uint32_t);
#endif
