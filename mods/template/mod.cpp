#include "aamod/aamod.h"
static const AAModAPI* api_ = nullptr;
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t size) {
    if (!api || size < offsetof(AAModAPI, capabilities) || api->api_version != AAMOD_ABI_VERSION)
        return AAMOD_ERR_ABI;
    api_ = api;
    AAMOD_LOGI(api_, "template: loaded; no game state changed");
    if (AAMOD_API_HAS(api, capabilities) && api->capabilities)
        AAMOD_LOGI(api_, "template: available services=%llu", (unsigned long long)api->capabilities());
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) { api_ = nullptr; }
