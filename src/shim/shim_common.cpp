/* aamod shim - generic proxy DLL body (built once per target DLL).
 *
 * A shim is a copy of a system DLL that the game statically imports. It is
 * placed next to the game executable, where the Windows loader finds it before
 * System32 (SafeDllSearchMode leaves the application directory first for
 * statically imported DLLs). Two jobs:
 *
 *   1. fill the generated jump table from the *real* DLL, which install.ps1
 *      copies as <name>Hooked.dll. Forwarding to a renamed copy is what breaks
 *      the load loop: forwarding to "winmm.x" would resolve back to this file.
 *      This must complete inside DllMain, before our own exports are callable.
 *   2. load aamod_core.dll from this DLL's own directory and hand control to
 *      AAMOD_AttachCore(). Done on a worker thread, never under the loader lock.
 *
 * Compiled with:
 *   /D AAMOD_SHIM_CONFIG_H=\"winmm_config.h\" /I src\shim\winmm
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#include AAMOD_SHIM_CONFIG_H

extern "C" void     aamod_shim_init_table(HMODULE real);
extern "C" unsigned aamod_shim_stub_count(void);
extern "C" unsigned aamod_shim_unresolved_count(void);

/* A shim may carry target-specific interception (see d3d11_intercept.cpp).
 * Such a shim defines AAMOD_SHIM_INTERCEPTS in its config header and links the
 * extra source file; the generic body only knows these two entry points. */
#ifdef AAMOD_SHIM_INTERCEPTS
extern "C" void aamod_shim_intercept_init(void);
#endif

static HMODULE g_self = NULL;
static HMODULE g_real = NULL;
static HMODULE g_core = NULL;
static SRWLOCK g_core_lock = SRWLOCK_INIT;

static void shim_log(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}

static void self_dir(wchar_t* out, size_t count)
{
    DWORD n = GetModuleFileNameW(g_self, out, (DWORD)count);
    if (n == 0 || n >= count) {
        out[0] = 0;
        return;
    }
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash)
        *(slash + 1) = 0;
}

/* Resolve the real implementation DLL.
 * Order: <exe dir>\<name>Hooked.dll, then an absolute override from the
 * environment (AAMOD_REAL_WINMM etc.), then %SystemRoot%\System32\<name>.dll.
 * The last one is a real path (not a bare name), so it cannot come back to us.
 */
static HMODULE load_real_dll(void)
{
    wchar_t dir[MAX_PATH * 2];
    self_dir(dir, MAX_PATH * 2);
    if (!dir[0])
        return NULL;

    wchar_t path[MAX_PATH * 2];
    _snwprintf_s(path, _TRUNCATE, L"%s%s", dir, AAMOD_SHIM_HOOKED);
    HMODULE h = LoadLibraryW(path);
    if (h)
        return h;
    shim_log(L"aamod[%hs]: %s not found (err %lu)\n", AAMOD_SHIM_NAME, path, GetLastError());

    wchar_t sys[MAX_PATH * 2];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n && n < MAX_PATH) {
        _snwprintf_s(path, _TRUNCATE, L"%s\\%hs.dll", sys, AAMOD_SHIM_NAME);
        h = LoadLibraryW(path);
        if (h)
            return h;
        shim_log(L"aamod[%hs]: %s not found (err %lu)\n", AAMOD_SHIM_NAME, path, GetLastError());
    }
    return NULL;
}

static void load_core_async();

static DWORD WINAPI boot_thread(LPVOID)
{
    load_core_async();
    return 0;
}

/* Load + attach aamod_core.dll. Idempotent and safe to call from any thread:
 * the background thread started by DllMain uses it, and so does a
 * target-specific intercept that needs the core *now* (e.g. the d3d11 shim
 * handing a freshly created device to the present hook). */
static HMODULE load_core()
{
    AcquireSRWLockExclusive(&g_core_lock);
    if (!g_core) {
        wchar_t dir[MAX_PATH * 2];
        self_dir(dir, MAX_PATH * 2);
        if (!dir[0]) {
            ReleaseSRWLockExclusive(&g_core_lock);
            return NULL;
        }

        wchar_t core_path[MAX_PATH * 2];
        _snwprintf_s(core_path, _TRUNCATE, L"%saamod_core.dll", dir);

        HMODULE core = LoadLibraryW(core_path);
        if (!core) {
            shim_log(L"aamod[%hs]: could not load %s (error %lu); forwarding only\n",
                     AAMOD_SHIM_NAME, core_path, GetLastError());
        } else {
            g_core = core;
            typedef void (*AttachFn)(HMODULE);
            AttachFn attach = (AttachFn)(void*)GetProcAddress(core, "AAMOD_AttachCore");
            if (attach)
                attach(core);
            else
                shim_log(L"aamod[%hs]: %s has no AAMOD_AttachCore\n", AAMOD_SHIM_NAME, core_path);
        }
    }
    HMODULE result = g_core;
    ReleaseSRWLockExclusive(&g_core_lock);
    return result;
}

static void load_core_async()
{
    load_core();
}

/* Used by target-specific intercepts that must reach the core right away. */
extern "C" HMODULE aamod_shim_ensure_core(void)
{
    return load_core();
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = (HMODULE)hinst;
        DisableThreadLibraryCalls(hinst);

        g_real = load_real_dll();
        aamod_shim_init_table(g_real);
#ifdef AAMOD_SHIM_INTERCEPTS
        /* Needs the table filled: an intercept replaces a stub target and keeps
         * the previous value as the "real function" to chain to. */
        aamod_shim_intercept_init();
#endif

        unsigned unresolved = aamod_shim_unresolved_count();
        if (unresolved)
            shim_log(L"aamod[%hs]: %u of %u exports unresolved\n",
                     AAMOD_SHIM_NAME, unresolved, aamod_shim_stub_count());

        HANDLE t = CreateThread(NULL, 0, boot_thread, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
        else
            shim_log(L"aamod[%hs]: bootstrap thread creation failed (err %lu); forwarding only\n", AAMOD_SHIM_NAME, GetLastError());
    }
    return TRUE;
}
