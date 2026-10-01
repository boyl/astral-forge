/* aamod - open base mod / modding framework for Astral Ascent (Chowdren v1388)
 *
 * Public C ABI. This header is the ONLY thing a mod author must include.
 * It is deliberately C99-compatible so mods can be written in C, C++, Zig,
 * Rust (cdylib), etc. The ABI is versioned and append-only:
 *   - never reorder or remove fields of AAModAPI
 *   - only append new function pointers at the end, bumping api_size
 *   - a mod must check `api->api_version` and only use fields it knows
 *
 * Mod entry points (exported by the mod DLL):
 *   uint32_t AAMOD_Init (const AAModAPI* api, uint32_t api_size);
 *      return AAMOD_OK (0) on success, non-zero to abort (mod is unloaded).
 *   void     AAMOD_Shutdown (void);            (optional)
 *   const char* AAMOD_ModInfo (void);          (optional, free-form string)
 */
#ifndef AAMOD_AAMOD_H
#define AAMOD_AAMOD_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#  define AAMOD_EXPORT extern "C" __declspec(dllexport)
#else
#  define AAMOD_EXPORT extern "C"
#endif

#define AAMOD_ABI_VERSION      1u
#define AAMOD_OK               0u
#define AAMOD_ERR_GENERIC      1u
#define AAMOD_ERR_ABI          2u

/* Log levels */
#define AAMOD_LOG_TRACE   0
#define AAMOD_LOG_DEBUG   1
#define AAMOD_LOG_INFO    2
#define AAMOD_LOG_WARN    3
#define AAMOD_LOG_ERROR   4

/* ------- subsystem: logging ------- */
typedef void (*AAModLogFn)(int level, const char* fmt, ...);

/* ------- subsystem: config ------- */
/* Read a key from aamod/config.ini. Returns default_value when absent. */
typedef const char* (*AAModConfigStrFn)(const char* key, const char* default_value);
typedef int64_t     (*AAModConfigIntFn)(const char* key, int64_t default_value);

/* ------- subsystem: hooks (x64 inline patching) -------
 * Install patches `target` (must be at least 12 bytes of patchable memory)
 * with a jump to `detour`. On success *trampoline receives a callable
 * pointer that executes the original prologue + jumps back.
 * Returns AAMOD_OK on success. Thread-safety: call from the main thread.
 */
typedef uint32_t (*AAModHookInstallFn)(void* target, void* detour, void** trampoline);
typedef uint32_t (*AAModHookRemoveFn)(void* target);

/* ------- subsystem: assets (defined in M4, reserved here) -------
 * Register an override for image asset `index`. `loader` is called the first
 * time the game asks for that image; return 1 and fill `*data`/`*size` with a
 * heap buffer (AAModFree) to replace it. Return 0 to keep the vanilla asset.
 */
typedef int  (*AAModAssetOverrideCb)(uint32_t asset_index, void* user);
typedef void (*AAModAssetRegisterFn)(uint32_t asset_index,
                                     AAModAssetOverrideCb cb, void* user);

/* ------- subsystem: events (defined in M2, reserved here) ------- */
typedef uint32_t (*AAModEventSubscribeFn)(const char* event_name, void* handler, void* user);

/* ------- subsystem: memory helpers ------- */
typedef void*  (*AAModAllocFn)(size_t size);
typedef void   (*AAModFreeFn)(void* ptr);

typedef struct AAModAPI {
    uint32_t api_version;      /* == AAMOD_ABI_VERSION */
    uint32_t api_size;         /* sizeof(AAModAPI) as seen by the loader */

    /* 1. logging */
    AAModLogFn        log;              /* printf-style, always non-NULL */
    /* 2. config */
    AAModConfigStrFn  config_str;
    AAModConfigIntFn  config_int;
    /* 3. memory */
    AAModAllocFn      alloc;
    AAModFreeFn       free_;
    /* 4. hooks */
    AAModHookInstallFn hook_install;
    AAModHookRemoveFn  hook_remove;
    /* 5. game module info */
    void*             game_module;      /* HMODULE of the host exe */
    const char*       game_dir;         /* UTF-8, no trailing slash */
    const char*       mod_dir;          /* UTF-8 dir this mod was loaded from */
    /* 6. reserved / later milestones */
    AAModAssetRegisterFn  asset_register;   /* M4 */
    AAModEventSubscribeFn event_subscribe;  /* M2 */
} AAModAPI;

/* Logging helper baked into the header so mods need no extra lib */
#define AAMOD_LOGT(api, ...) ((api)->log(AAMOD_LOG_TRACE, __VA_ARGS__))
#define AAMOD_LOGD(api, ...) ((api)->log(AAMOD_LOG_DEBUG, __VA_ARGS__))
#define AAMOD_LOGI(api, ...) ((api)->log(AAMOD_LOG_INFO,  __VA_ARGS__))
#define AAMOD_LOGW(api, ...) ((api)->log(AAMOD_LOG_WARN,  __VA_ARGS__))
#define AAMOD_LOGE(api, ...) ((api)->log(AAMOD_LOG_ERROR, __VA_ARGS__))

/* Entry point every mod DLL must export.
 * Declare it in your mod like this:
 *   AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t api_size) { ... }
 */
typedef uint32_t (*AAModInitFn)(const AAModAPI* api, uint32_t api_size);
typedef void     (*AAModShutdownFn)(void);

#endif /* AAMOD_AAMOD_H */
