#pragma once
#include "aamod/aamod.h"
#include <string>
#include <vector>
namespace aamod {
uint64_t resources_owner(const std::string&);
void resources_revoke(uint64_t);
size_t resources_live_images();
uint32_t resources_image_load(uint64_t,const char*,AAModImage*,uint32_t);
uint32_t resources_image_release(uint64_t,uint64_t);
uint32_t resources_image_copy(uint64_t,const char*,std::vector<uint8_t>&,uint32_t&,uint32_t&);
}
