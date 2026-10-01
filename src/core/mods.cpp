#include "mods.h"
#include "log.h"
#include "json_min.h"

#include <algorithm>
#include <stdio.h>

namespace aamod {

namespace {

std::string wide_to_utf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

bool read_file_utf8(const std::wstring& path, std::string& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > 1024 * 1024) {
        CloseHandle(f);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD got = 0;
    bool ok = size.QuadPart == 0 ||
              (ReadFile(f, &out[0], (DWORD)out.size(), &got, NULL) && got == out.size());
    CloseHandle(f);
    // strip UTF-8 BOM
    if (ok && out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return ok;
}

bool dir_exists(const std::wstring& dir)
{
    DWORD a = GetFileAttributesW(dir.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool file_exists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool parse_mod_dir(const std::wstring& mod_dir, ModInfo& out)
{
    std::wstring manifest = mod_dir + L"\\mod.json";
    std::string text;
    if (!read_file_utf8(manifest, text)) {
        AAMOD_DEBUG("mods: no readable mod.json in %s", wide_to_utf8(mod_dir).c_str());
        return false;
    }
    std::vector<json::Field> fields;
    if (!json::parse_flat_object(text, fields)) {
        AAMOD_WARN("mods: malformed mod.json in %s", wide_to_utf8(mod_dir).c_str());
        return false;
    }

    out.id       = json::get_string(fields, "id");
    out.name     = json::get_string(fields, "name");
    out.version  = json::get_string(fields, "version");
    out.author   = json::get_string(fields, "author");
    out.enabled  = json::get_bool(fields, "enabled", true);
    out.priority = (int)json::get_int(fields, "priority", 0);
    out.dir      = wide_to_utf8(mod_dir);

    if (out.id.empty()) {
        // fall back to the folder name
        std::wstring d = mod_dir;
        size_t slash = d.find_last_of(L"\\/");
        out.id = wide_to_utf8(slash == std::wstring::npos ? d : d.substr(slash + 1));
    }
    if (out.name.empty())
        out.name = out.id;

    std::string entry = json::get_string(fields, "entry", "mod.dll");
    out.entry_path = mod_dir + L"\\" + std::wstring(entry.begin(), entry.end());

    if (!file_exists(out.entry_path)) {
        AAMOD_WARN("mods: '%s' -> entry '%s' not found in %s", out.id.c_str(),
                   entry.c_str(), out.dir.c_str());
        return false;
    }
    return true;
}

} // anonymous namespace

std::vector<ModInfo> discover_mods(const std::vector<std::wstring>& dirs)
{
    std::vector<ModInfo> out;
    std::vector<std::string> seen;

    for (size_t d = 0; d < dirs.size(); ++d) {
        if (!dir_exists(dirs[d]))
            continue;
        AAMOD_INFO("mods: scanning %s", wide_to_utf8(dirs[d]).c_str());

        WIN32_FIND_DATAW fd;
        std::wstring pattern = dirs[d] + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            AAMOD_WARN("mods: cannot enumerate %s (err %lu)", wide_to_utf8(dirs[d]).c_str(),
                       GetLastError());
            continue;
        }
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            ModInfo m;
            if (!parse_mod_dir(dirs[d] + L"\\" + fd.cFileName, m))
                continue;

            bool dup = false;
            for (size_t i = 0; i < seen.size(); ++i) {
                if (seen[i] == m.id) {
                    AAMOD_WARN("mods: duplicate id '%s' at %s, ignoring", m.id.c_str(),
                               m.dir.c_str());
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            seen.push_back(m.id);
            AAMOD_INFO("mods: found '%s' v%s (%s)%s", m.id.c_str(), m.version.c_str(),
                       m.dir.c_str(), m.enabled ? "" : " [disabled]");
            out.push_back(m);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const ModInfo& a, const ModInfo& b) {
                         if (a.priority != b.priority)
                             return a.priority > b.priority;
                         return a.id < b.id;
                     });
    return out;
}

bool load_one(ModInfo& m, const AAModAPI* api)
{
    if (!m.enabled)
        return false;
    if (m.module)
        return m.initialized;

    HMODULE h = LoadLibraryExW(m.entry_path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        AAMOD_ERROR("mods: LoadLibrary('%s') failed (err %lu)",
                    wide_to_utf8(m.entry_path).c_str(), GetLastError());
        return false;
    }
    AAModInitFn init = (AAModInitFn)(void*)GetProcAddress(h, "AAMOD_Init");
    if (!init) {
        AAMOD_ERROR("mods: '%s' does not export AAMOD_Init, skipping", m.id.c_str());
        FreeLibrary(h);
        return false;
    }
    m.module = (HINSTANCE)h;
    m.shutdown = (AAModShutdownFn)(void*)GetProcAddress(h, "AAMOD_Shutdown");

    uint32_t rc = init(api, (uint32_t)sizeof(AAModAPI));
    if (rc != AAMOD_OK) {
        AAMOD_ERROR("mods: '%s' AAMOD_Init returned %u, unloading", m.id.c_str(), rc);
        FreeLibrary(h);
        m.module = NULL;
        m.shutdown = NULL;
        return false;
    }
    m.initialized = true;
    AAMOD_INFO("mods: '%s' initialised", m.id.c_str());
    return true;
}

size_t load_mods(std::vector<ModInfo>& mods, const AAModAPI* api)
{
    size_t ok_count = 0;
    for (size_t i = 0; i < mods.size(); ++i)
        if (load_one(mods[i], api))
            ++ok_count;
    return ok_count;
}

void unload_mods(std::vector<ModInfo>& mods)
{
    // reverse order of initialisation
    for (size_t i = mods.size(); i-- > 0;) {
        ModInfo& m = mods[i];
        if (!m.module)
            continue;
        if (m.shutdown) {
            __try {
                m.shutdown();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                AAMOD_ERROR("mods: '%s' AAMOD_Shutdown raised an exception", m.id.c_str());
            }
        }
        FreeLibrary((HMODULE)m.module);
        m.module = NULL;
    }
}

} // namespace aamod
