#pragma once
#include <string>
#include "aamod/aamod.h"
namespace aamod {
void content_catalog_initialize(const std::wstring& game_directory);
bool content_catalog_available();
bool content_catalog_contains(uint32_t kind,uint32_t id);
uint32_t content_catalog_query(uint32_t,const char*,uint32_t,AAModCatalogEntry*,uint32_t,uint32_t,uint32_t*,uint32_t*);
}
