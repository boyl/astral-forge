#ifndef AAMOD_MODS_H
#define AAMOD_MODS_H

#include <string>
#include <vector>
#include <windows.h>

#include "aamod/aamod.h"

namespace aamod {

struct ModInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string dir;          // UTF-8 directory that contains mod.json
    std::wstring entry_path;  // absolute path of the mod DLL
    bool        enabled;
    int         priority;
    HINSTANCE   module;
    AAModShutdownFn shutdown;
    bool        initialized;

    ModInfo() : enabled(true), priority(0), module(NULL), shutdown(NULL), initialized(false) {}
};

// Scans `dirs` (in order) for `<dir>/<mod>/mod.json`, parses the manifests and
// returns them sorted by descending priority, then by id. Directories that do
// not exist are skipped silently. Duplicate ids: the first one wins.
std::vector<ModInfo> discover_mods(const std::vector<std::wstring>& dirs);

// Loads a single mod and calls its AAMOD_Init with `api`.
// Returns true when the mod initialised successfully.
bool load_one(ModInfo& mod, const AAModAPI* api);

// Loads every enabled mod and calls its AAMOD_Init with `api`.
// Returns the number of mods that initialised successfully.
size_t load_mods(std::vector<ModInfo>& mods, const AAModAPI* api);

// Calls AAMOD_Shutdown (if exported) then releases every loaded mod.
void unload_mods(std::vector<ModInfo>& mods);

} // namespace aamod

#endif // AAMOD_MODS_H
