#include "mod_contract.h"
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
namespace aamod {
namespace {
bool read_integer(const std::vector<json::Field>& fields, const char* key,
                  uint64_t maximum, uint64_t& value, std::string& error) {
    const json::Field* found = nullptr;
    for (const auto& field : fields) if (field.key == key) {
        if (found) { error = std::string("duplicate contract field: ") + key; return false; }
        found = &field;
    }
    if (!found) return true;
    if (!found->is_number || found->value.empty() ||
        found->value.find_first_not_of("0123456789") != std::string::npos) {
        error = std::string("contract field must be a non-negative integer: ") + key; return false;
    }
    errno = 0;
    auto parsed = strtoull(found->value.c_str(), nullptr, 10);
    if (errno == ERANGE || parsed > maximum) {
        error = std::string("contract field out of range: ") + key; return false;
    }
    value = parsed; return true;
}
}
bool mod_contract_parse(const std::vector<json::Field>& fields, ModContract& output, std::string& error) {
    uint64_t abi = 1, size = 128, capabilities = 0;
    if (!read_integer(fields,"api_version",UINT32_MAX,abi,error) ||
        !read_integer(fields,"min_api_size",UINT32_MAX,size,error) ||
        !read_integer(fields,"required_capabilities",INT64_MAX,capabilities,error)) return false;
    if (!abi || size < 128) { error = "api_version must be positive and min_api_size at least 128"; return false; }
    output = {(uint32_t)abi,(uint32_t)size,capabilities}; return true;
}
bool mod_contract_check(const ModContract& contract, const AAModAPI* api, std::string& error) {
    if (!api || api->api_version != contract.abi) { error = "ABI version mismatch"; return false; }
    if (api->api_size < contract.min_size) { error = "API structure too short"; return false; }
    if (contract.capabilities) {
        uint64_t available = AAMOD_API_HAS(api,capabilities) && api->capabilities ? api->capabilities() : 0;
        if ((available & contract.capabilities) != contract.capabilities) {
            error = "required capabilities unavailable"; return false;
        }
    }
    return true;
}
}
