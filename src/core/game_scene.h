#pragma once
#include "aamod/aamod.h"
#include <cstring>
namespace aamod {
inline bool gameplay_scene(const AAModGameState& state){return !std::strcmp(state.scene_name,"layout_game");}
inline bool hub_scene(const AAModGameState& state){return !std::strcmp(state.scene_name,"layout_hub");}
}
