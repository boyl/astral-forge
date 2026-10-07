#pragma once
#include <windows.h>
#include "aamod/game.h"
namespace aamod {
bool sha256_file(const wchar_t* path, char output[65], uint64_t* file_size);
void game_profile_apply(AAModGameInfo& info);
void game_profile_initialize();
uint32_t game_profile_query(AAModGameInfo* output, uint32_t output_size);
}
