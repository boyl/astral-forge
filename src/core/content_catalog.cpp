#include "content_catalog.h"
#include "game_profile.h"
#include "log.h"
#include "../vendor/nlohmann/json.hpp"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <fstream>
namespace aamod {
namespace {
using Json=nlohmann::json;
SRWLOCK lock=SRWLOCK_INIT;
struct Row {uint32_t kind,id,quality;std::string zh,en;};
std::vector<Row> rows;
uint32_t availability=AAMOD_ERR_CONTENT_UNSUPPORTED;
Json read(const std::wstring& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("content file unavailable");
    stream.seekg(0,std::ios::end);auto n=stream.tellg();
    if(n<0 || n>32*1024*1024)throw std::runtime_error("content file length invalid");
    stream.seekg(0);std::string text((size_t)n,'\0');
    if(n && !stream.read(text.data(),n))throw std::runtime_error("content file read failed");
    return Json::parse(text);
}
std::string label(const Json& dictionary,const std::string& key) {
    auto it=dictionary.find(key);if(it==dictionary.end() || !it->is_string())return {};
    auto value=it->get<std::string>();std::string result;bool markup=false;
    for(char c:value) {if(c=='[')markup=true;else if(c==']')markup=false;else if(!markup)result+=c;}
    return result;
}
bool fingerprint(const std::wstring& path,const char* expected) {
    char hash[65];uint64_t size;return sha256_file(path.c_str(),hash,&size)&&!strcmp(hash,expected);
}
void name_copy(char (&output)[192],const std::string& text) {
    size_t n=(std::min)(text.size(),sizeof(output)-1);
    while(n<text.size() && n && ((unsigned char)text[n]&0xc0)==0x80)--n;
    memcpy(output,text.data(),n);output[n]=0;
}
}
void content_catalog_initialize(const std::wstring& directory) {
    AAModGameInfo game={};game_profile_query(&game,sizeof(game));
    if(game.identity_status!=AAMOD_GAME_IDENTITY_MATCH)return;
    std::vector<Row> candidate;uint32_t error=AAMOD_ERR_CONTENT_FORMAT;
    try {
        auto relic=directory+L"\\array_relicsbook.json",spells=directory+L"\\dictionary_spellbook.json";
        if(!fingerprint(relic,"445bef5e745844fa0c508de51c97f4a4ae218188f26e3582c8e0452e30ec1c04")||
           !fingerprint(spells,"87397723e709e0af689bc8ccb306a77a0e8fe6ae32ec3f0c47f0729221347552"))throw std::runtime_error("native content fingerprint mismatch");
        auto book=read(relic).at("data"),spellbook=read(spells).at("data");
        auto zh=read(directory+L"\\dictionary_localisation_chinese_simplified.json").at("data");
        auto en=read(directory+L"\\dictionary_localisation_english.json").at("data");
        const auto& qualities=book.at(9);
        for(size_t id=1;id<qualities.size();++id) {
            const auto& raw_quality=qualities.at(id).at(0);
            // The native C2 array also contains textual section-marker rows;
            // those are not selectable relics (same domain rule as BD).
            if(!raw_quality.is_number_integer())continue;
            auto quality=raw_quality.get<int>();if(quality<1||quality>4)continue;
            auto key="relic_name_"+std::to_string(id),z=label(zh,key),e=label(en,key);if(z.empty()||e.empty())continue;
            candidate.push_back({AAMOD_CONTENT_AURA,(uint32_t)id,(uint32_t)quality,std::move(z),std::move(e)});
        }
        for(uint32_t id=30;id<=86;++id) {
            if(!spellbook.contains("spell_"+std::to_string(id)))continue;
            auto key="skill_name_"+std::to_string(id),z=label(zh,key),e=label(en,key);if(z.empty()||e.empty())continue;
            candidate.push_back({AAMOD_CONTENT_SPELL,id,0,std::move(z),std::move(e)});
        }
        error=AAMOD_OK;
    } catch(const Json::exception& e){AAMOD_ERROR("content catalog: JSON error: %s",e.what());}
      catch(const std::runtime_error& e){AAMOD_ERROR("content catalog: %s",e.what());}
    if(error!=AAMOD_OK)candidate.clear();
    AcquireSRWLockExclusive(&lock);rows=std::move(candidate);availability=error;ReleaseSRWLockExclusive(&lock);
    if(error==AAMOD_OK)AAMOD_INFO("content catalog: native rows=%zu (aura + common spell)",rows.size());
}
bool content_catalog_available(){AcquireSRWLockShared(&lock);bool value=availability==AAMOD_OK;ReleaseSRWLockShared(&lock);return value;}
bool content_catalog_contains(uint32_t kind,uint32_t id) {
    if(kind==AAMOD_CONTENT_AURA && !id)return true;
    AcquireSRWLockShared(&lock);bool found=false;if(availability==AAMOD_OK)for(const auto& row:rows)if(row.kind==kind&&row.id==id){found=true;break;}ReleaseSRWLockShared(&lock);return found;
}
uint32_t content_catalog_query(uint32_t kind,const char* language,uint32_t offset,AAModCatalogEntry* out,uint32_t capacity,uint32_t size,uint32_t* count,uint32_t* total) {
    if((kind!=AAMOD_CONTENT_AURA&&kind!=AAMOD_CONTENT_SPELL)||!language||(!out&&capacity)||size!=sizeof(*out)||!count||!total)return AAMOD_ERR_ARGUMENT;
    bool chinese=!strcmp(language,"zh-CN");if(!chinese&&strcmp(language,"en"))return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock);
    if(availability){uint32_t error=availability;ReleaseSRWLockShared(&lock);return error;}
    uint32_t length=0;for(const auto& row:rows)length+=row.kind==kind;
    if(offset>length){ReleaseSRWLockShared(&lock);return AAMOD_ERR_ARGUMENT;}
    uint32_t copied=0,index=0;
    for(const auto& row:rows)if(row.kind==kind) {
        if(index++<offset)continue;if(copied==capacity)break;
        AAModCatalogEntry entry={};entry.size=sizeof(entry);entry.kind=kind;entry.id=row.id;entry.quality=row.quality;name_copy(entry.name,chinese?row.zh:row.en);out[copied++]=entry;
    }
    *count=copied;*total=length;ReleaseSRWLockShared(&lock);return AAMOD_OK;
}
}
