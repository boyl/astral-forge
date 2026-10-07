#pragma once
#include <string>
#include "aamod/aamod.h"
namespace aamod {
uint64_t plugin_data_owner(const std::wstring& data_directory,const std::string& plugin_id);
void plugin_data_revoke(uint64_t owner);
uint32_t plugin_data_read(uint64_t,const char*,void*,uint32_t,uint32_t*);
uint32_t plugin_data_write(uint64_t,const char*,const void*,uint32_t);
uint32_t plugin_data_delete(uint64_t,const char*);
}
