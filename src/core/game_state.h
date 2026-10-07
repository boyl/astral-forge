#pragma once
#include "aamod/state.h"
#include "aamod/events.h"
namespace aamod {
void game_state_initialize();
// Requires the same quiescent game-thread boundary as ShutdownCore.
void game_state_shutdown();
uint32_t game_state_query(AAModGameState*, uint32_t);
uint32_t game_state_events(uint64_t, AAModStateEvent*, uint32_t, uint32_t, uint32_t*, uint64_t*);
}
