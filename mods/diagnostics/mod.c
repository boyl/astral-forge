#include "aamod/aamod.h"
#include <string.h>
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t size)
{
    if (!api || api->api_version != AAMOD_ABI_VERSION || size < 128) return AAMOD_ERR_ABI;
    if (size < offsetof(AAModAPI, game_info) + sizeof(api->game_info) ||
        !AAMOD_API_HAS(api, game_info) || !api->game_info) {
        AAMOD_LOGW(api, "diagnostics: game_info unavailable in this core");
        return AAMOD_OK;
    }
    AAModGameInfo info = {0};
    if (api->game_info(&info, sizeof(info)) != AAMOD_OK) return AAMOD_ERR_GENERIC;
    AAMOD_LOGI(api, "diagnostics: identity=%u profile=%s sha256=%s bytes=%llu",
               info.identity_status, info.profile_id, info.sha256, (unsigned long long)info.file_size);
    if (info.identity_status != AAMOD_GAME_IDENTITY_MATCH)
        AAMOD_LOGW(api, "diagnostics: unknown host; game-specific offsets must stay disabled");
    AAMOD_LOGI(api, "diagnostics: startup identity only; no player/room/build queries are exposed");
    return AAMOD_OK;
}
