#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api;
static HANDLE stop_event, worker;
static DWORD WINAPI watch(void*) {
    uint64_t cursor = 0;
    while (WaitForSingleObject(stop_event, 100) == WAIT_TIMEOUT) {
        AAModStateEvent events[16]; uint32_t count = 0; uint64_t newest = 0;
        uint32_t result = api->state_events(cursor, events, 16, sizeof(events[0]), &count, &newest);
        if (result == AAMOD_ERR_EVENT_CURSOR) {
            AAMOD_LOGW(api, "event_watch: history lost cursor=%llu newest=%llu",
                (unsigned long long)cursor, (unsigned long long)newest);
            cursor = newest; continue;
        }
        if (result != AAMOD_OK) { AAMOD_LOGE(api, "event_watch: query failed=%u", result); return 1; }
        for (uint32_t i = 0; i < count; ++i) {
            const auto& event = events[i]; const auto& state = event.state;
            AAMOD_LOGI(api, "event_watch: id=%llu changes=%u status=%u epoch=%llu scene=%d p1=%u health=%.3f max=%.3f",
                (unsigned long long)event.id, event.changes, state.status,
                (unsigned long long)state.scene_epoch, state.scene_index,
                state.players[0].present, state.players[0].health, state.players[0].max_health);
            cursor = event.id;
        }
    }
    return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input, uint32_t size) {
    if (!input || input->api_version != AAMOD_ABI_VERSION ||
        size < offsetof(AAModAPI, state_events) + sizeof(input->state_events) || !input->state_events)
        return AAMOD_ERR_ABI;
    api = input; stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) return AAMOD_ERR_GENERIC;
    worker = CreateThread(nullptr, 0, watch, nullptr, 0, nullptr);
    if (!worker) { CloseHandle(stop_event); stop_event = nullptr; return AAMOD_ERR_GENERIC; }
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) {
    SetEvent(stop_event); WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker); CloseHandle(stop_event); worker = stop_event = nullptr;
}
