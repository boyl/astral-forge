#include "native_images.h"
#include "resources.h"
#include "game_profile.h"
#include "hook.h"
#include "log.h"
#include <windows.h>
#include <vector>
#include <map>
#include <cstring>
#include <new>
#include <set>
namespace aamod {
namespace {
constexpr uint32_t image_count=71870;
constexpr size_t MiB=1024*1024;
SRWLOCK mutex=SRWLOCK_INIT;
struct Guard {Guard(){AcquireSRWLockExclusive(&mutex);}~Guard(){ReleaseSRWLockExclusive(&mutex);}};
struct Replacement {
    uint64_t owner,id;
    uint32_t image,state=AAMOD_NATIVE_PENDING,error=0,width,height,texture=0;
    uint64_t applies=0,restores=0;
    bool revoked=false,saved=false;
    unsigned char original[0x48]={};
    std::vector<uint8_t> pixels;
};
std::map<uint64_t,Replacement> replacements;
std::map<uint32_t,uint64_t> bindings;
uint64_t next_id;
std::set<uint64_t> revoked_owners;
std::set<uint64_t> active_owners;
unsigned char* base;
void (*upload_original)(void*);
void (*load_original)(void*);
void (*release_original)(void*);
void* (*allocate_native)(size_t);
void (*premultiply_native)(void*,int,int);
void (*flip_native)(void*,int,int,int);
bool available=false;
uint64_t draws[image_count]={};
AAModNativeImageInfo observed[image_count]={};
bool snapshot(void* object,AAModNativeImageInfo* info) {
    __try {
        auto p=(unsigned char*)object;
        info->size=sizeof(*info);info->image_id=*(uint32_t*)p;
        info->width=*(uint16_t*)(p+0x18);info->height=*(uint16_t*)(p+0x1a);
        info->buffer_width=*(uint16_t*)(p+0x1c);info->buffer_height=*(uint16_t*)(p+0x1e);
        info->format=*(uint32_t*)(p+0x40);info->has_texture=*(uint32_t*)(p+0x28)!=0;
        return info->image_id<image_count&&info->width&&info->height;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void* cached_object(uint32_t id) {
    __try {return ((void**)(base+0xa314390))[id];}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return nullptr;}
}
void observe_current(void* object) {
    AAModNativeImageInfo info={};if(!snapshot(object,&info))return;
    info.upload_visits=draws[info.image_id];observed[info.image_id]=info;
}
void original_pixels(void* object,Replacement& r) {
    release_original(object);
    auto p=(unsigned char*)object;
    memcpy(p,r.original,sizeof(r.original));
    *(uint32_t*)(p+0x28)=0;*(void**)(p+0x30)=nullptr;
    load_original(object);
}
bool apply_pixels(void* object,Replacement& r) {
    void* pixels=allocate_native(r.pixels.size());
    if(!pixels){r.error=AAMOD_ERR_RESOURCE_MEMORY;r.state=AAMOD_NATIVE_FAILED;return false;}
    memcpy(pixels,r.pixels.data(),r.pixels.size());
    premultiply_native(pixels,r.width,r.height);
    // Native external-image decoder uses the same backend orientation flag.
    if(*(uint32_t*)(base+0xa314378))flip_native(pixels,r.width,r.height,4);
    auto p=(unsigned char*)object;
    if(!r.saved){memcpy(r.original,p,sizeof(r.original));r.saved=true;}
    release_original(object);
    *(void**)(p+0x30)=pixels;*(uint32_t*)(p+0x40)=0;
    *(uint16_t*)(p+0x1c)=(uint16_t)r.width;*(uint16_t*)(p+0x1e)=(uint16_t)r.height;
    *(uint16_t*)(p+0x20)=0;*(uint16_t*)(p+0x22)=0;
    *(uint16_t*)(p+0x24)=(uint16_t)r.width;*(uint16_t*)(p+0x26)=(uint16_t)r.height;
    // Original solid-color images can use stretch mode; the replacement is full size.
    *(uint16_t*)(p+4)&=~uint16_t(0x80);
    upload_original(object);
    r.texture=*(uint32_t*)(p+0x28);
    if(!r.texture){original_pixels(object,r);upload_original(object);observe_current(object);r.error=AAMOD_ERR_GENERIC;r.state=AAMOD_NATIVE_FAILED;return false;}
    r.state=AAMOD_NATIVE_APPLIED;++r.applies;
    observe_current(object);
    AAMOD_INFO("native image: applied id=%u handle=%llu count=%llu thread=%lu",r.image,r.id,r.applies,GetCurrentThreadId());
    return true;
}
void upload(void* object) {
    Guard guard;
    // Most sprites skip this function once cached. Drain queued work at any
    // native upload boundary, where the game's graphics context is current.
    for(auto i=bindings.begin();i!=bindings.end();){
        uint32_t id=i->first;uint64_t handle=i->second;++i;
        auto entry=replacements.find(handle);auto& r=entry->second;
        if(r.state!=AAMOD_NATIVE_PENDING&&r.state!=AAMOD_NATIVE_RESTORE_PENDING)continue;
        if(r.state==AAMOD_NATIVE_PENDING&&!active_owners.count(r.owner))continue;
        void* target=cached_object(id);if(!target)continue;
        if(r.state==AAMOD_NATIVE_PENDING){apply_pixels(target,r);continue;}
        if(r.saved){
            original_pixels(target,r);upload_original(target);observe_current(target);
            if(!*(uint32_t*)((unsigned char*)target+0x28)){
                r.state=AAMOD_NATIVE_FAILED;r.error=AAMOD_ERR_GENERIC;
                AAMOD_ERROR("native image: original texture restoration failed id=%u handle=%llu; binding retained",r.image,r.id);
                continue;
            }
            ++r.restores;
        }
        r.error=0;
        r.state=AAMOD_NATIVE_RESTORED;r.texture=0;std::vector<uint8_t>().swap(r.pixels);bindings.erase(id);
        observe_current(target);
        AAMOD_INFO("native image: restored id=%u handle=%llu thread=%lu",r.image,r.id,GetCurrentThreadId());
        if(r.revoked)replacements.erase(entry);
    }
    AAModNativeImageInfo info={};
    if(!snapshot(object,&info)){upload_original(object);return;}
    if(cached_object(info.image_id)!=object){upload_original(object);return;}
    ++draws[info.image_id];
    info.upload_visits=draws[info.image_id];observed[info.image_id]=info;
    auto binding=bindings.find(info.image_id);
    if(binding==bindings.end()){upload_original(object);return;}
    auto entry=replacements.find(binding->second);auto& r=entry->second;
    if(r.state==AAMOD_NATIVE_APPLIED && *(uint32_t*)((unsigned char*)object+0x28)!=r.texture) {
        apply_pixels(object,r);return;
    }
    upload_original(object);
}
}
bool native_images_available(){Guard guard;return available;}
void native_images_initialize(){
    AAModGameInfo identity={};game_profile_query(&identity,sizeof(identity));
    if(identity.identity_status!=AAMOD_GAME_IDENTITY_MATCH)return;
    base=(unsigned char*)GetModuleHandleW(nullptr);
    const uint8_t head[]={0x40,0x53,0x48,0x83,0xec,0x20,0x83,0x79,0x28,0x00};
    if(memcmp(base+0x623b30,head,sizeof(head))){AAMOD_ERROR("native image: upload signature mismatch");return;}
    load_original=(void(*)(void*))(base+0x616ed0);release_original=(void(*)(void*))(base+0x616570);
    allocate_native=(void*(*)(size_t))(base+0x583ce24);premultiply_native=(void(*)(void*,int,int))(base+0x618130);
    flip_native=(void(*)(void*,int,int,int))(base+0x623630);
    available=hook::install_near(base+0x623b30,(void*)upload,(void**)&upload_original);
    if(available)AAMOD_INFO("native image: exact-profile upload adapter ready");
}
bool native_images_shutdown(){
    Guard guard;
    if(available&&!hook::remove(base+0x623b30))return false;
    available=false;
    // ShutdownCore contract requires the renderer to have stopped. Native
    // cached textures remain game-owned until game teardown; no thread-unsafe GL calls.
    bindings.clear();replacements.clear();active_owners.clear();return true;
}
uint32_t native_image_info(uint32_t id,AAModNativeImageInfo* output,uint32_t size){
    if(!output||size<sizeof(*output)||id>=image_count)return AAMOD_ERR_ARGUMENT;
    Guard guard;if(!available)return AAMOD_ERR_NATIVE_UNSUPPORTED;
    if(!draws[id])return AAMOD_ERR_NATIVE_NOT_READY;
    *output=observed[id];return AAMOD_OK;
}
uint32_t native_image_replace(uint64_t owner,uint32_t id,const char* png,uint64_t* output){
    if(!output||!png||id>=image_count)return AAMOD_ERR_ARGUMENT;
    AAModNativeImageInfo info={};auto error=native_image_info(id,&info,sizeof(info));if(error)return error;
    Replacement request={};request.owner=owner;request.image=id;
    error=resources_image_copy(owner,png,request.pixels,request.width,request.height);if(error)return error;
    if(info.width!=request.width||info.height!=request.height)return AAMOD_ERR_NATIVE_DIMENSIONS;
    Guard guard;if(!available)return AAMOD_ERR_NATIVE_UNSUPPORTED;
    if(revoked_owners.count(owner))return AAMOD_ERR_RESOURCE_HANDLE;
    if(bindings.count(id))return AAMOD_ERR_NATIVE_CONFLICT;
    size_t owned=0,bytes=0,total=0;
    for(const auto& entry:replacements){total+=entry.second.pixels.size();if(entry.second.owner==owner){++owned;bytes+=entry.second.pixels.size();}}
    if(owned>=16||replacements.size()>=128||bytes+request.pixels.size()>64*MiB||total+request.pixels.size()>256*MiB)return AAMOD_ERR_RESOURCE_LIMIT;
    request.id=++next_id;uint64_t handle=request.id;
    try {
        replacements.emplace(handle,std::move(request));
        bindings.emplace(id,handle);
    } catch(const std::bad_alloc&) {replacements.erase(handle);return AAMOD_ERR_RESOURCE_MEMORY;}
    *output=handle;return AAMOD_OK;
}
uint32_t native_image_restore(uint64_t owner,uint64_t handle){
    Guard guard;auto entry=replacements.find(handle);
    if(entry==replacements.end()||entry->second.owner!=owner||entry->second.revoked)return AAMOD_ERR_RESOURCE_HANDLE;
    auto& r=entry->second;
    if(r.state==AAMOD_NATIVE_RESTORED||r.state==AAMOD_NATIVE_RESTORE_PENDING)return AAMOD_OK;
    r.error=0;
    if(!r.saved){bindings.erase(r.image);r.state=AAMOD_NATIVE_RESTORED;std::vector<uint8_t>().swap(r.pixels);}
    else r.state=AAMOD_NATIVE_RESTORE_PENDING;
    return AAMOD_OK;
}
uint32_t native_image_status(uint64_t owner,uint64_t handle,AAModImageReplacementStatus* output,uint32_t size){
    if(!output||size<sizeof(*output))return AAMOD_ERR_ARGUMENT;
    Guard guard;auto entry=replacements.find(handle);
    if(entry==replacements.end()||entry->second.owner!=owner||entry->second.revoked)return AAMOD_ERR_RESOURCE_HANDLE;
    const auto& r=entry->second;*output={sizeof(*output),r.state,r.error,r.image,r.id,r.applies,r.restores};return AAMOD_OK;
}
void native_images_revoke(uint64_t owner){
    Guard guard;
    revoked_owners.insert(owner);
    active_owners.erase(owner);
    for(auto i=replacements.begin();i!=replacements.end();){
        auto& r=i->second;if(r.owner!=owner){++i;continue;}
        if(r.saved&&r.state!=AAMOD_NATIVE_RESTORED){r.revoked=true;r.state=AAMOD_NATIVE_RESTORE_PENDING;++i;}
        else {
            auto binding=bindings.find(r.image);
            if(binding!=bindings.end()&&binding->second==r.id)bindings.erase(binding);
            i=replacements.erase(i);
        }
    }
}
void native_images_activate(uint64_t owner){Guard guard;if(!revoked_owners.count(owner))active_owners.insert(owner);}
uint32_t native_image_forget(uint64_t owner,uint64_t handle){
    Guard guard;auto entry=replacements.find(handle);
    if(entry==replacements.end()||entry->second.owner!=owner||entry->second.revoked)return AAMOD_ERR_RESOURCE_HANDLE;
    if(entry->second.state!=AAMOD_NATIVE_RESTORED)return AAMOD_ERR_NATIVE_PENDING;
    replacements.erase(entry);return AAMOD_OK;
}
}

