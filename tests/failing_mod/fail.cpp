#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api_;
__declspec(noinline) static int target(int a, int b) { volatile int x = a + 3; return x * b + 7; }
static int replacement(int a, int b) { return a - b; }
static void on_frame(const AAModFrameInfo*, void*) { AAMOD_LOGE(api_, "FAILED_MOD_CALLBACK_MUST_NOT_RUN"); }
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t) {
    api_ = api;
    void* trampoline = nullptr;
    if (api->hook_install((void*)target, (void*)replacement, &trampoline) != AAMOD_OK) return AAMOD_ERR_ABI;
    if (!api->frame_subscribe(on_frame, nullptr)) return AAMOD_ERR_ABI;
    AAModImage image = {};
    if (api->image_load(api->resource_owner,"assets/checker.png",&image,sizeof(image)) != AAMOD_OK)
        return AAMOD_ERR_RESOURCE_IO;
    AAMOD_LOGI(api,"failing_mod: owned image loaded; core must release on failure");
    AAMOD_LOGI(api, "failing_mod: registered hook and frame; intentionally failing");
    wchar_t throwing[2] = {};
    if (GetEnvironmentVariableW(L"AAMOD_TEST_THROW_INIT", throwing, 2)) RaiseException(0xE0000001, 0, 0, nullptr);
    return AAMOD_ERR_GENERIC;
}
