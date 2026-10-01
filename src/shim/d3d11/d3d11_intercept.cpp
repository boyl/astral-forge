/* aamod d3d11 shim - interception of the device entry points (M1).
 *
 * The Astral Ascent executable statically imports exactly one function from
 * d3d11.dll: D3D11CreateDevice. That single import is our guaranteed, earliest
 * handle on the graphics stack:
 *
 *   game -> d3d11!D3D11CreateDevice  (our stub -> our detour)
 *     -> real device created by d3d11Hooked.dll
 *     -> core!AAMOD_OnD3D11Device(device)
 *        -> device -> IDXGIDevice -> GetAdapter -> GetParent(IDXGIFactory)
 *           -> CreateSwapChain* detour (see core/present.cpp)
 *
 * Anything loaded later (libGLESv2/ANGLE, for instance) that imports d3d11.dll
 * resolves to *this* module - the loader keys DLLs by base name - so the same
 * detour covers the ANGLE path without a second shim.
 *
 * No d3d11.h/dxgi.h here on purpose: the signatures below are ABI-identical to
 * the COM ones while keeping the shim free of SDK link dependencies.
 */
#include <windows.h>
#include <stdio.h>

/* From the generated table (tools/gen_shim_def.py): swap one stub target and
 * return its previous value so we can chain to the real DLL. */
extern "C" void* aamod_shim_override(const char* name, void* fn);
/* From shim_common.cpp: load + attach aamod_core.dll on demand. */
extern "C" HMODULE aamod_shim_ensure_core(void);

namespace {

typedef HRESULT (WINAPI *CreateDeviceFn)(void* adapter, UINT driver_type, HMODULE software,
                                         UINT flags, const UINT* feature_levels,
                                         UINT feature_level_count, UINT sdk_version,
                                         void** device, UINT* feature_level_out, void** context);

typedef HRESULT (WINAPI *CreateDeviceAndSwapChainFn)(void* adapter, UINT driver_type, HMODULE software,
                                                     UINT flags, const UINT* feature_levels,
                                                     UINT feature_level_count, UINT sdk_version,
                                                     const void* swap_chain_desc, void** swap_chain,
                                                     void** device, UINT* feature_level_out,
                                                     void** context);

CreateDeviceFn             g_real_create_device = nullptr;
CreateDeviceAndSwapChainFn g_real_create_device_and_swap_chain = nullptr;

void* g_on_device = nullptr;      /* AAMOD_OnD3D11Device */
void* g_on_swap_chain = nullptr;  /* AAMOD_OnSwapChain */
bool  g_core_probed = false;

void log_line(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}

/* Resolve the core exports once. Retried on every device creation until the
 * core is there, so a missing core degrades to plain passthrough. */
void resolve_core()
{
    if (g_core_probed)
        return;
    HMODULE core = aamod_shim_ensure_core();
    if (!core)
        return;
    g_on_device     = (void*)GetProcAddress(core, "AAMOD_OnD3D11Device");
    g_on_swap_chain = (void*)GetProcAddress(core, "AAMOD_OnSwapChain");
    g_core_probed   = g_on_device != nullptr;
    if (!g_core_probed)
        log_line(L"aamod[d3d11]: core has no AAMOD_OnD3D11Device; present stays un-hooked\n");
}

void report_device(void* device)
{
    resolve_core();
    if (!g_on_device || !device)
        return;
    typedef int (*OnDeviceFn)(void*);
    int hooked = ((OnDeviceFn)g_on_device)(device);
    log_line(L"aamod[d3d11]: device %p -> present hook %s\n", device, hooked ? L"installed" : L"failed");
}

void report_swap_chain(void* swap_chain)
{
    resolve_core();
    if (!g_on_swap_chain || !swap_chain)
        return;
    typedef int (*OnSwapChainFn)(void*);
    ((OnSwapChainFn)g_on_swap_chain)(swap_chain);
}

HRESULT WINAPI hook_create_device(void* adapter, UINT driver_type, HMODULE software, UINT flags,
                                  const UINT* feature_levels, UINT feature_level_count,
                                  UINT sdk_version, void** device, UINT* feature_level_out,
                                  void** context)
{
    if (!g_real_create_device)
        return E_FAIL;
    HRESULT hr = g_real_create_device(adapter, driver_type, software, flags, feature_levels,
                                      feature_level_count, sdk_version, device, feature_level_out,
                                      context);
    if (SUCCEEDED(hr) && device && *device)
        report_device(*device);
    return hr;
}

HRESULT WINAPI hook_create_device_and_swap_chain(void* adapter, UINT driver_type, HMODULE software,
                                                 UINT flags, const UINT* feature_levels,
                                                 UINT feature_level_count, UINT sdk_version,
                                                 const void* swap_chain_desc, void** swap_chain,
                                                 void** device, UINT* feature_level_out,
                                                 void** context)
{
    if (!g_real_create_device_and_swap_chain)
        return E_FAIL;
    HRESULT hr = g_real_create_device_and_swap_chain(adapter, driver_type, software, flags,
                                                     feature_levels, feature_level_count, sdk_version,
                                                     swap_chain_desc, swap_chain, device,
                                                     feature_level_out, context);
    if (SUCCEEDED(hr)) {
        if (device && *device)
            report_device(*device);
        /* The factory detour would catch it too, but hooking the chain we were
         * just handed is immediate and cannot be missed. */
        if (swap_chain && *swap_chain)
            report_swap_chain(*swap_chain);
    }
    return hr;
}

} /* namespace */

/* Called from the shim's DllMain after the jump table was filled. */
extern "C" void aamod_shim_intercept_init(void)
{
    g_real_create_device = (CreateDeviceFn)aamod_shim_override(
        "D3D11CreateDevice", (void*)&hook_create_device);
    g_real_create_device_and_swap_chain = (CreateDeviceAndSwapChainFn)aamod_shim_override(
        "D3D11CreateDeviceAndSwapChain", (void*)&hook_create_device_and_swap_chain);

    if (!g_real_create_device)
        log_line(L"aamod[d3d11]: D3D11CreateDevice is not in the stub table; no interception\n");
    else
        log_line(L"aamod[d3d11]: intercepting D3D11CreateDevice (real %p)\n",
                 (void*)g_real_create_device);
}
