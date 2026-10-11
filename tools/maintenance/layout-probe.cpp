// 只读映射目标 EXE，复用生产解析器；不解析导入、不调用游戏入口。
#include <windows.h>
#include <cstdio>
#include "../../src/core/game_layout.h"
#include "../../src/vendor/nlohmann/json.hpp"
using nlohmann::json;
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { fputs("usage: layout-probe.exe <Astral Ascent.exe>\n", stderr); return 2; }
    HMODULE image = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!image) { fprintf(stderr, "read-only image mapping failed: %lu\n", GetLastError()); return 2; }
    aamod::GameLayout l;
    bool resolved = aamod::game_layout_resolve(image, l);
    json result={{"format",1},{"kind","offline-native-contracts"},{"imageMapped",resolved}};
    result["capabilities"]={{"state",l.state},{"equipment",l.equipment},{"checkpoint",l.checkpoint},{"events",l.events},{"images",l.images},
            {"damage",bool(l.damage_entry&&l.damage_site)},{"crystalPickup",bool(l.pickup_entry&&l.pickup_site&&l.crystals)},
            {"abandon",bool(l.abandon_entry&&l.abandon_site)},{"defeat",bool(l.outcome_source&&l.defeat_entry&&l.defeat_site)},
            {"victory",bool(l.outcome_source&&l.victory_entry&&l.victory_site&&l.victory_flag)}};
    result["rvas"]=json::object();
    result["fields"]={{"health",{l.health[0],l.health[1]}},{"maximum",{l.maximum[0],l.maximum[1]}}};
#define RVA(name) result["rvas"][#name]=l.name
    RVA(frame_update);RVA(run_start);RVA(assign);RVA(refresh);RVA(gambit_refresh);
    RVA(checkpoint_save);RVA(json_get);RVA(progress_object);RVA(room_path);RVA(checkpoint_count);RVA(checkpoint_object);RVA(equipment_object);RVA(player_count);
    RVA(image_upload);RVA(image_load);RVA(image_release);RVA(image_allocate);RVA(image_premultiply);RVA(image_flip);RVA(image_cache);RVA(image_orientation);
    RVA(damage_entry);RVA(damage_site);RVA(pickup_entry);RVA(pickup_site);RVA(abandon_entry);RVA(abandon_site);RVA(outcome_source);RVA(defeat_entry);RVA(defeat_site);RVA(victory_entry);RVA(victory_site);
#undef RVA
#define FIELD(name) result["fields"][#name]=l.name
    FIELD(mana);FIELD(mana_maximum);FIELD(run);FIELD(save_pending);FIELD(crystals);FIELD(victory_flag);
#undef FIELD
    FreeLibrary(image);
    puts(result.dump().c_str());
    return resolved ? 0 : 2;
}
