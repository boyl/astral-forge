/* aamod_core.dll - the loader core.
 *
 * Loaded by a shim DLL (winmm.dll / version.dll / d3d11.dll) placed next to the
 * game exe, or by any ASI loader. The core never assumes who loaded it: it
 * reads the host exe path with GetModuleFileNameW(NULL) and its own path with
 * GetModuleFileNameW(hSelf).
 *
 * Sequence (on a worker thread, never inside DllMain):
 *   1. resolve game dir / core dir / writable data dir
 *   2. open the log
 *   3. read aamod\config.ini (creating a default one when possible)
 *   4. apply engine environment gates (CHOWDREN_*) before the game reads them
 *   5. discover + load mods, calling AAMOD_Init on each
 *   6. (M1+) install hooks / (M3+) create the overlay
 */
#include <windows.h>
#include <shlobj.h>
#include <winver.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

#include "aamod/aamod.h"
#include "log.h"
#include "ini.h"
#include "mods.h"
#include "hook.h"
#include "anchor.h"

#define AAMOD_VERSION_STR "0.1.0-m1"

using namespace aamod;

namespace {

HMODULE        g_core_module = NULL;
std::wstring   g_game_dir;
std::wstring   g_core_dir;
std::wstring   g_data_dir;
std::string    g_game_dir_utf8;
Ini            g_config;
std::vector<ModInfo> g_mods;
volatile LONG  g_bootstrap_done = 0;
HANDLE         g_thread = NULL;
bool           g_finalized = false;

// ---------- path helpers ----------
std::wstring dir_of_module(HMODULE m)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(m, buf, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2)
        return std::wstring();
    std::wstring s(buf, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? s : s.substr(0, slash);
}

std::string to_utf8(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

std::wstring from_utf8(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool dir_writable(const std::wstring& dir)
{
    if (!CreateDirectoryW(dir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    std::wstring probe = dir + L"\\.aamod_write_test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

std::wstring local_appdata()
{
    PWSTR p = NULL;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &p)) && p) {
        out = p;
        CoTaskMemFree(p);
    }
    return out;
}

std::wstring env_str(const wchar_t* name)
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 4);
    if (n == 0 || n >= MAX_PATH * 4)
        return std::wstring();
    return std::wstring(buf, n);
}

// ---------- API surface exposed to mods ----------
void api_log(int level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_writev(level, fmt, args);
    va_end(args);
}

const char* api_config_str(const char* key, const char* def)
{
    static std::string scratch;
    if (!key || !g_config.has(key))
        return def;
    scratch = g_config.get_str(key);
    return scratch.c_str();
}

int64_t api_config_int(const char* key, int64_t def)
{
    if (!key || !g_config.has(key))
        return def;
    return g_config.get_int(key, def);
}

void* api_alloc(size_t size) { return malloc(size); }
void  api_free(void* p) { free(p); }

uint32_t api_hook_install(void* target, void* detour, void** trampoline)
{
    return hook::install(target, detour, trampoline) ? AAMOD_OK : AAMOD_ERR_GENERIC;
}

uint32_t api_hook_remove(void* target)
{
    return hook::remove(target) ? AAMOD_OK : AAMOD_ERR_GENERIC;
}

void api_asset_register(uint32_t index, AAModAssetOverrideCb cb, void* user)
{
    (void)index; (void)cb; (void)user;
    AAMOD_WARN("asset_register: not available in this build (planned for M4)");
}

uint32_t api_event_subscribe(const char* name, void* handler, void* user)
{
    (void)name; (void)handler; (void)user;
    AAMOD_WARN("event_subscribe: not available in this build (planned for M2)");
    return AAMOD_ERR_GENERIC;
}

int api_anchor_find(void* module, const char* literal, void** code, size_t* size)
{
    AnchorHit hit;
    if (!anchor_resolve((HMODULE)module, literal, &hit))
        return 0;
    if (code)
        *code = hit.code;
    if (size)
        *size = hit.size;
    return 1;
}

// One API struct per mod (mod_dir differs); everything else is shared.
AAModAPI* make_api(const std::string& mod_dir)
{
    // Keep the mod_dir strings alive for the lifetime of the process.
    static std::vector<std::string*> s_keep;
    s_keep.push_back(new std::string(mod_dir));

    AAModAPI* api = (AAModAPI*)calloc(1, sizeof(AAModAPI));
    api->api_version     = AAMOD_ABI_VERSION;
    api->api_size        = (uint32_t)sizeof(AAModAPI);
    api->log             = api_log;
    api->config_str      = api_config_str;
    api->config_int      = api_config_int;
    api->alloc           = api_alloc;
    api->free_           = api_free;
    api->hook_install    = api_hook_install;
    api->hook_remove     = api_hook_remove;
    api->game_module     = (void*)GetModuleHandleW(NULL);
    api->game_dir        = g_game_dir_utf8.c_str();
    api->mod_dir         = s_keep.back()->c_str();
    api->asset_register  = api_asset_register;
    api->event_subscribe = api_event_subscribe;
    api->anchor_find     = api_anchor_find;
    return api;
}

// ---------- config ----------
void write_default_config(const std::wstring& path)
{
    FILE* f = NULL;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        return;
    static const char kDefault[] =
        "; aamod configuration - generated on first run\r\n"
        "; All keys are optional; delete this file to regenerate it.\r\n"
        "\r\n"
        "[general]\r\n"
        "; extra directories to scan for mods, separated by ';'\r\n"
        "mod_dirs=\r\n"
        "; vertical sync override, -1 = leave the game alone\r\n"
        "vsync=-1\r\n"
        "\r\n"
        "[engine]\r\n"
        "; engine feature gates, applied before the game reads them\r\n"
        "show_debugger=0\r\n"
        "sdl_debug=0\r\n"
        "sdl_log=0\r\n"
        "debug_achievements=0\r\n"
        "backend_picker=\r\n";
    fwrite(kDefault, 1, sizeof(kDefault) - 1, f);
    fclose(f);
}

void apply_engine_env()
{
    struct Gate { const wchar_t* var; const char* key; };
    static const Gate gates[] = {
        { L"CHOWDREN_SHOW_DEBUGGER",      "engine.show_debugger" },
        { L"CHOWDREN_SDL_DEBUG",          "engine.sdl_debug" },
        { L"CHOWDREN_SDL_LOG",            "engine.sdl_log" },
        { L"CHOWDREN_DEBUG_ACHIEVEMENTS", "engine.debug_achievements" },
    };
    for (size_t i = 0; i < sizeof(gates) / sizeof(gates[0]); ++i) {
        if (g_config.get_bool(gates[i].key, false)) {
            SetEnvironmentVariableW(gates[i].var, L"1");
            AAMOD_INFO("env: %s=1", to_utf8(gates[i].var).c_str());
        }
    }
    std::string bp = g_config.get_str("engine.backend_picker");
    if (!bp.empty()) {
        SetEnvironmentVariableW(L"CHOWDREN_BACKEND_PICKER", from_utf8(bp).c_str());
        AAMOD_INFO("env: CHOWDREN_BACKEND_PICKER=%s", bp.c_str());
    }
}

void add_mod_dir(std::vector<std::wstring>& dirs, const std::wstring& d)
{
    if (d.empty())
        return;
    for (size_t i = 0; i < dirs.size(); ++i)
        if (_wcsicmp(dirs[i].c_str(), d.c_str()) == 0)
            return;
    dirs.push_back(d);
}

void log_game_version()
{
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
    if (!n)
        return;
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe, &dummy);
    if (!size)
        return;
    std::vector<unsigned char> buf(size);
    if (!GetFileVersionInfoW(exe, 0, size, &buf[0]))
        return;
    VS_FIXEDFILEINFO* info = NULL;
    UINT len = 0;
    if (VerQueryValueW(&buf[0], L"\\", (LPVOID*)&info, &len) && info) {
        AAMOD_INFO("game ver : %u.%u.%u.%u",
                   HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                   HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    }
}

DWORD WINAPI bootstrap_thread(LPVOID)
{
    g_game_dir = dir_of_module(NULL);
    g_core_dir = dir_of_module(g_core_module);
    g_game_dir_utf8 = to_utf8(g_game_dir);

    // data dir: prefer <gamedir>\aamod, fall back to %LOCALAPPDATA%\aamod
    std::wstring game_aamod = g_game_dir + L"\\aamod";
    if (dir_writable(game_aamod))
        g_data_dir = game_aamod;

    std::wstring local = local_appdata();
    std::wstring local_aamod = local.empty() ? std::wstring() : local + L"\\aamod";

    if (g_data_dir.empty()) {
        if (local_aamod.empty()) {
            OutputDebugStringA("aamod: no writable data directory, aborting\n");
            InterlockedExchange(&g_bootstrap_done, 1);
            return 0;
        }
        CreateDirectoryW(local_aamod.c_str(), NULL);
        g_data_dir = local_aamod;
    }

    log_open((g_data_dir + L"\\logs").c_str());

    AAMOD_INFO("======================================================");
    AAMOD_INFO("aamod core %s (abi %u) starting", AAMOD_VERSION_STR, AAMOD_ABI_VERSION);
    AAMOD_INFO("game dir : %s", g_game_dir_utf8.c_str());
    AAMOD_INFO("core dir : %s", to_utf8(g_core_dir).c_str());
    AAMOD_INFO("data dir : %s", to_utf8(g_data_dir).c_str());
    AAMOD_INFO("log file : %s", log_path());
    log_game_version();

    std::wstring cfg = g_data_dir + L"\\config.ini";
    if (!g_config.load_file(cfg.c_str())) {
        write_default_config(cfg);
        g_config.load_file(cfg.c_str());
        AAMOD_INFO("config   : created default %s", to_utf8(cfg).c_str());
    } else {
        AAMOD_INFO("config   : %s (%zu keys)", to_utf8(cfg).c_str(), g_config.entries().size());
    }

    apply_engine_env();

    std::vector<std::wstring> dirs;
    add_mod_dir(dirs, env_str(L"AAMOD_MOD_DIR"));
    {
        std::string extra = g_config.get_str("general.mod_dirs");
        size_t start = 0;
        while (start <= extra.size()) {
            size_t sep = extra.find(';', start);
            std::string part = extra.substr(start, sep == std::string::npos
                                                   ? std::string::npos : sep - start);
            add_mod_dir(dirs, from_utf8(part));
            if (sep == std::string::npos)
                break;
            start = sep + 1;
        }
    }
    add_mod_dir(dirs, g_game_dir + L"\\aamod\\mods");
    add_mod_dir(dirs, g_data_dir + L"\\mods");
    if (!local_aamod.empty())
        add_mod_dir(dirs, local_aamod + L"\\mods");

    CreateDirectoryW((g_data_dir + L"\\mods").c_str(), NULL);

    g_mods = discover_mods(dirs);
    AAMOD_INFO("mods     : %zu discovered", g_mods.size());

    size_t loaded = 0;
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (!g_mods[i].enabled)
            continue;
        if (load_one(g_mods[i], make_api(g_mods[i].dir)))
            ++loaded;
    }
    AAMOD_INFO("mods     : %zu loaded", loaded);
    AAMOD_INFO("aamod core ready");
    AAMOD_INFO("======================================================");

    InterlockedExchange(&g_bootstrap_done, 1);
    return 0;
}

void finalize()
{
    if (g_finalized)
        return;
    g_finalized = true;
    AAMOD_INFO("aamod core shutting down (%zu mods)", g_mods.size());
    unload_mods(g_mods);
    log_close();
}

} // anonymous namespace

extern "C" __declspec(dllexport) void AAMOD_AttachCore(HMODULE hself)
{
    g_core_module = hself;
    if (g_thread)
        return;
    g_thread = CreateThread(NULL, 0, bootstrap_thread, NULL, 0, NULL);
    if (!g_thread)
        bootstrap_thread(NULL);   // last resort: run inline
}

extern "C" __declspec(dllexport) void AAMOD_WaitForCore(uint32_t timeout_ms)
{
    if (g_thread)
        WaitForSingleObject(g_thread, timeout_ms);
}

extern "C" __declspec(dllexport) uint32_t AAMOD_CoreReady(void)
{
    return (uint32_t)InterlockedCompareExchange(&g_bootstrap_done, 1, 1);
}

extern "C" __declspec(dllexport) const char* AAMOD_Version(void)
{
    return AAMOD_VERSION_STR;
}

/* Same as AAModAPI::anchor_find, reachable without a mod (used by host tests
 * and by the ASI-style entry points). */
extern "C" __declspec(dllexport) int AAMOD_ResolveAnchor(void* module,
                                                         const char* literal,
                                                         void** code,
                                                         size_t* size)
{
    return api_anchor_find(module, literal, code, size);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_core_module = hModule;
        DisableThreadLibraryCalls(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        finalize();
    }
    return TRUE;
}
