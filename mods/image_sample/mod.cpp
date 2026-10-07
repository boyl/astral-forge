#include "aamod/aamod.h"
static const AAModAPI* api;
static AAModImage image;
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input,uint32_t size) {
    if (!input || input->api_version != AAMOD_ABI_VERSION || size < offsetof(AAModAPI,image_release)+sizeof(input->image_release) ||
        !input->image_load || !input->image_release) return AAMOD_ERR_ABI;
    api=input;
    auto error=api->image_load(api->resource_owner,"assets/checker.png",&image,sizeof(image));
    if(error){AAMOD_LOGE(api,"image_sample: load failed=%u",error);return error;}
    AAMOD_LOGI(api,"image_sample: RGBA8 width=%u height=%u stride=%u bytes=%u first=%u,%u,%u,%u",
        image.width,image.height,image.stride,image.byte_count,image.pixels[0],image.pixels[1],image.pixels[2],image.pixels[3]);
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) {
    auto error=api->image_release(api->resource_owner,image.handle);
    AAMOD_LOGI(api,"image_sample: release=%u",error);image={};
}
