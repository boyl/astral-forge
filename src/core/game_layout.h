#pragma once
#include <windows.h>
#include <cstdint>
namespace aamod {
// 私有原生适配数据；公共 C ABI 不变。地址来自代码及字段绑定，指纹仅用于诊断。
struct GameLayout {
    bool state=false, equipment=false, checkpoint=false, events=false, images=false;
    uintptr_t frame_update=0, run_start=0, assign=0, refresh=0, gambit_refresh=0;
    uintptr_t checkpoint_save=0, json_get=0, progress_object=0, room_path=0;
    uintptr_t checkpoint_count=0, checkpoint_object=0, equipment_object=0, player_count=0;
    uint32_t health[2]={}, maximum[2]={}, mana=0, mana_maximum=0, run=0, save_pending=0;
    uintptr_t image_upload=0,image_load=0,image_release=0,image_allocate=0,image_premultiply=0,image_flip=0,image_cache=0,image_orientation=0;
    uintptr_t damage_entry=0,damage_site=0,pickup_entry=0,pickup_site=0,abandon_entry=0,abandon_site=0;
    uint32_t crystals=0;
    uintptr_t outcome_source=0,defeat_entry=0,defeat_site=0,victory_entry=0,victory_site=0;
    uint32_t victory_flag=0;
};
extern GameLayout game_layout;
void game_layout_initialize();
bool game_layout_resolve(HMODULE module, GameLayout& result);
int native_player_index(const void* metadata);
bool native_object_named(const void* metadata,const char* expected);
}
