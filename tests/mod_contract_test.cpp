#include "../src/core/mod_contract.h"
#include <stdio.h>
#define CHECK(x) do { if(!(x)){printf("FAIL line %d\n",__LINE__);return 1;} } while(0)
static uint64_t caps() { return 743; }
int main() {
    using namespace aamod;
    std::string error; std::vector<json::Field> fields; ModContract contract;
    auto parse = [&](const char* text) { fields.clear();return json::parse_flat_object(text,fields) && mod_contract_parse(fields,contract,error); };
    AAModAPI api = {};api.api_version = 1;api.api_size = 248;api.capabilities = caps;
    CHECK(parse("{}") && contract.min_size == 128 && mod_contract_check(contract,&api,error));
    CHECK(parse("{\"api_version\":1,\"min_api_size\":248,\"required_capabilities\":743}") && mod_contract_check(contract,&api,error));
    CHECK(parse("{\"api_version\":2}") && !mod_contract_check(contract,&api,error));
    CHECK(parse("{\"min_api_size\":249}") && !mod_contract_check(contract,&api,error));
    CHECK(parse("{\"required_capabilities\":1024}") && !mod_contract_check(contract,&api,error));
    CHECK(parse("{\"required_capabilities\":0}") && mod_contract_check(contract,&api,error));
    api.api_size=128;CHECK(parse("{}") && mod_contract_check(contract,&api,error));
    CHECK(parse("{\"required_capabilities\":1}") && !mod_contract_check(contract,&api,error));
    for(const char* text : {"{\"api_version\":0}","{\"min_api_size\":127}","{\"api_version\":4294967296}",
        "{\"required_capabilities\":9223372036854775808}","{\"required_capabilities\":184467440737095516160}",
        "{\"min_api_size\":-1}","{\"api_version\":1.5}","{\"required_capabilities\":1e3}",
        "{\"api_version\":\"1\"}","{\"api_version\":true}","{\"api_version\":1,\"api_version\":2}"}) CHECK(!parse(text));
    puts("PASS: legacy defaults, ABI/size/capability gates, invalid declarations"); return 0;
}
