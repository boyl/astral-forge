#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api;
static HANDLE stop_event, worker;
static DWORD WINAPI watch(void*) {
    uint64_t healed_epoch = 0, pending = 0, stale = 0;
    while (WaitForSingleObject(stop_event, 100) == WAIT_TIMEOUT) {
        if (pending) {
            AAModCommandResult result = {};
            auto error = api->command_result(api->command_owner,pending,&result,sizeof(result));
            if (error != AAMOD_OK) { AAMOD_LOGE(api,"heal_once: result error=%u",error); return 1; }
            if (result.status != AAMOD_COMMAND_PENDING) {
                AAMOD_LOGI(api,"heal_once: result id=%llu status=%u before=%.3f after=%.3f thread=%u",
                    (unsigned long long)pending,result.status,result.before,result.after,result.update_thread_id);
                pending = 0;
            }
            continue;
        }
        if (stale) {
            AAModCommandResult result = {};
            if (api->command_result(api->command_owner,stale,&result,sizeof(result)) != AAMOD_OK) return 1;
            if (result.status == AAMOD_COMMAND_PENDING) continue;
            AAMOD_LOGI(api,"heal_once: stale result=%u",result.status); stale = 0;
        }
        AAModGameState state = {};
        if (api->game_state(&state,sizeof(state)) != AAMOD_OK) return 1;
        const auto& player = state.players[0];
        if (state.status != AAMOD_STATE_READY || state.scene_index != 268 || state.scene_epoch == healed_epoch ||
            !player.present || !(player.valid_fields & AAMOD_PLAYER_HEALTH) || player.health <= 0 || player.health >= player.max_health) continue;
        AAModCommand command = {sizeof(command),AAMOD_COMMAND_HEAL,state.scene_epoch,0,0,player.max_health};
        auto error = api->command_submit(api->command_owner,&command,sizeof(command),&pending);
        if (error != AAMOD_OK) { AAMOD_LOGE(api,"heal_once: submit error=%u",error); return 1; }
        healed_epoch = state.scene_epoch;
        command.scene_epoch = state.scene_epoch + 1;
        error = api->command_submit(api->command_owner,&command,sizeof(command),&stale);
        if (error != AAMOD_OK) { AAMOD_LOGE(api,"heal_once: stale submit error=%u",error); return 1; }
        AAMOD_LOGI(api,"heal_once: queued epoch=%llu health=%.3f max=%.3f",
            (unsigned long long)state.scene_epoch,player.health,player.max_health);
    }
    return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input,uint32_t size) {
    if (!input || input->api_version != AAMOD_ABI_VERSION || size < offsetof(AAModAPI,command_result)+sizeof(input->command_result) ||
        !input->game_state || !input->command_submit || !input->command_result) return AAMOD_ERR_ABI;
    api = input; stop_event = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!stop_event) return AAMOD_ERR_GENERIC;
    worker = CreateThread(nullptr,0,watch,nullptr,0,nullptr);
    if (!worker) { CloseHandle(stop_event); stop_event=nullptr; return AAMOD_ERR_GENERIC; }
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) {
    SetEvent(stop_event); WaitForSingleObject(worker,INFINITE);
    CloseHandle(worker); CloseHandle(stop_event); worker=stop_event=nullptr;
}
