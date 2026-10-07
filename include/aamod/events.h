#ifndef AAMOD_EVENTS_H
#define AAMOD_EVENTS_H
#include "state.h"
#define AAMOD_EVENT_STATUS 1u
#define AAMOD_EVENT_SCENE 2u
#define AAMOD_EVENT_PLAYERS 4u
#define AAMOD_EVENT_HEALTH 8u
#define AAMOD_EVENT_FIELDS 16u
#define AAMOD_ERR_EVENT_CURSOR 5u
/* Snapshot differences, not native damage/kill/pickup notifications.
 * Position-only updates do not generate events. One record may have multiple
 * flags. IDs are monotonic for the lifetime of the loaded core. */
typedef struct AAModStateEvent {
    uint64_t id;
    uint32_t changes;
    uint32_t reserved;
    AAModGameState state;
} AAModStateEvent;
/* Thread-safe copied stream; no callbacks or subscription ownership.
 * Retains 128 records. after=0 reads oldest retained records. Otherwise an
 * expired/future cursor returns EVENT_CURSOR, count=0 and current newest.
 * Resume from newest after explicitly handling lost history. Success copies
 * at most capacity records in ascending order; advance to last copied id.
 * capacity=0 with events=NULL queries newest without consuming anything.
 * event_size must equal sizeof(AAModStateEvent). Invalid arguments write none.
 * Calling while the game is unsupported returns status events only. */
typedef uint32_t (*AAModStateEventsFn)(uint64_t after, AAModStateEvent* events,
    uint32_t capacity, uint32_t event_size, uint32_t* count, uint64_t* newest);
#endif
