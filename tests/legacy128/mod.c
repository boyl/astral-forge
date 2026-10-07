/* Frozen ABI-1 layout: deliberately does not include any current SDK header. */
#include <stdint.h>
#include <stddef.h>
#ifndef LEGACY_SIZE
#define LEGACY_SIZE 128
#endif
typedef struct LegacyAPI {
    uint32_t version,size;
    void (*log)(int,const char*,...);
    const char* (*config_str)(const char*,const char*);
    int64_t (*config_int)(const char*,int64_t);
    void* (*alloc)(size_t);
    void (*free_)(void*);
    void* hook_install;
    void* hook_remove;
    void* game_module;
    const char* game_dir;
    const char* mod_dir;
    void* asset_register;
    void* event_subscribe;
    void* anchor_find;
    void* frame_subscribe;
    void* frame_unsubscribe;
#if LEGACY_SIZE == 208
    uint64_t (*capabilities)(void);
    void* game_info;
    void* game_state;
    void* state_events;
    uint64_t command_owner;
    void* command_submit;
    void* command_result;
    uint64_t resource_owner;
    void* image_load;
    void* image_release;
#endif
} LegacyAPI;
_Static_assert(sizeof(LegacyAPI)==LEGACY_SIZE,"frozen ABI size");
static const LegacyAPI* saved;
__declspec(dllexport) uint32_t AAMOD_Init(const LegacyAPI* api,uint32_t size) {
    if(!api||api->version!=1||size<sizeof(*api)||api->size!=size)return 2;
    if(api->config_int("missing_legacy_key",37)!=37)return 1;
    unsigned char* value=(unsigned char*)api->alloc(16);
    if(!value)return 1;value[0]=19;api->free_(value);
#if LEGACY_SIZE == 208
    if(!api->capabilities||(api->capabilities()&743)!=743)return 1;
#endif
    saved=api;api->log(2,"legacy%d: frozen SDK init OK, core size=%u",LEGACY_SIZE,size);return 0;
}
__declspec(dllexport) void AAMOD_Shutdown(void) { saved->log(2,"legacy%d: shutdown OK",LEGACY_SIZE);saved=0; }
