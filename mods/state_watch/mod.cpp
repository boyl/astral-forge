#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api;
static HANDLE stop_event, worker;
static DWORD WINAPI watch(void*) {
    uint64_t epoch = UINT64_MAX;
    uint32_t status = UINT32_MAX, present = UINT32_MAX;
    AAModPlayerState previous[2] = {};
    while (WaitForSingleObject(stop_event, 250) == WAIT_TIMEOUT) {
        AAModGameState state = {};
        if (api->game_state(&state, sizeof(state)) != AAMOD_OK) return 1;
        uint32_t players = state.players[0].present | (state.players[1].present << 1);
        bool health_changed = false;
        for (unsigned i = 0; i != 2; ++i)
            health_changed |= state.players[i].valid_fields != previous[i].valid_fields ||
                state.players[i].health != previous[i].health || state.players[i].max_health != previous[i].max_health;
        if (state.status != status || state.scene_epoch != epoch || players != present || health_changed) {
            AAMOD_LOGI(api, "state_watch: status=%u sequence=%llu epoch=%llu scene=%d name=%s players=%u",
                state.status, (unsigned long long)state.sequence, (unsigned long long)state.scene_epoch,
                state.scene_index, state.scene_name, players);
            for (unsigned i = 0; i != 2; ++i) {
                const AAModPlayerState* p = &state.players[i];
                if (p->present && (p->valid_fields & AAMOD_PLAYER_POSITION))
                    AAMOD_LOGI(api, "state_watch: P%u layer_position=(%.2f,%.2f) health_available=%u",
                        i + 1, p->layer_x, p->layer_y, !!(p->valid_fields & AAMOD_PLAYER_HEALTH));
                if (p->present && (p->valid_fields & AAMOD_PLAYER_HEALTH))
                    AAMOD_LOGI(api, "state_watch: P%u health=%.3f max_health=%.3f", i + 1, p->health, p->max_health);
            }
            status = state.status; epoch = state.scene_epoch; present = players;
            previous[0] = state.players[0]; previous[1] = state.players[1];
        }
    }
    return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input, uint32_t size) {
    if (!input || size < offsetof(AAModAPI, game_state) + sizeof(input->game_state) ||
        input->api_version != AAMOD_ABI_VERSION || !input->game_state) return AAMOD_ERR_ABI;
    api = input;
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) return AAMOD_ERR_GENERIC;
    worker = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr);
    if (!worker) { CloseHandle(stop_event); stop_event = nullptr; return AAMOD_ERR_GENERIC; }
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) {
    SetEvent(stop_event); WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker); CloseHandle(stop_event); worker = stop_event = nullptr;
}
