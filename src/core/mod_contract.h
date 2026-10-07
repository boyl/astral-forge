#ifndef AAMOD_MOD_CONTRACT_H
#define AAMOD_MOD_CONTRACT_H
#include "json_min.h"
#include "aamod/aamod.h"
namespace aamod {
struct ModContract {
    uint32_t abi = 1;
    uint32_t min_size = 128;
    uint64_t capabilities = 0;
};
bool mod_contract_parse(const std::vector<json::Field>&, ModContract&, std::string& error);
bool mod_contract_check(const ModContract&, const AAModAPI*, std::string& error);
}
#endif
