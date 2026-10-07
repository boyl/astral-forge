#pragma once
#include "aamod/aamod.h"
namespace aamod {
void native_images_initialize();
bool native_images_shutdown();
bool native_images_available();
void native_images_revoke(uint64_t);
void native_images_activate(uint64_t);
uint32_t native_image_info(uint32_t,AAModNativeImageInfo*,uint32_t);
uint32_t native_image_replace(uint64_t,uint32_t,const char*,uint64_t*);
uint32_t native_image_restore(uint64_t,uint64_t);
uint32_t native_image_status(uint64_t,uint64_t,AAModImageReplacementStatus*,uint32_t);
uint32_t native_image_forget(uint64_t,uint64_t);
}
