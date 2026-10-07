/* aamod test host - an offline stand-in for the game executable.
 *
 * It statically imports winmm.dll exactly like Astral Ascent.exe does (well,
 * a subset of the same 20 entry points) and reports whether the calls actually
 * reach the real implementation through the shim's forwarders. It also waits
 * for the aamod core to finish bootstrapping so the test can assert that mods
 * were discovered and initialised.
 *
 * Built as part of build.ps1 and run from out/test/, where the loader finds
 * winmm.dll (our shim) before System32.
 */
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")

typedef void (*WaitForCoreFn)(unsigned int);
typedef unsigned int (*CoreReadyFn)(void);
typedef const char* (*VersionFn)(void);
typedef int (*ResolveAnchorFn)(void* module, const char* literal, void** code, size_t* size);
typedef unsigned int (*HookStateFn)(void);
typedef unsigned long long (*FrameCountFn)(void);
typedef void (*DetachFn)(void);

/* Returns a hidden window for the swap chain, or NULL. */
static HWND host_make_window()
{
    const wchar_t* cls = L"aamod_host_window";
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, cls, L"aamod host", WS_OVERLAPPEDWINDOW,
                           0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
}

/* Ground truth for the anchor resolver: this function prints a message nobody
 * else in the image carries, so resolving that literal must land back here.
 * Called through a volatile pointer so the optimiser cannot drop it. */
__declspec(noinline) static int host_anchor_probe(int x)
{
    if (x == 12345)
        printf("host: anchor probe message %d\n", x);
    return x + 1;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep the log when a later step crashes */
    printf("host: winmm import test\n");

    /* 1. forwarded calls must reach real winmm and return sane values */
    MMRESULT r = timeBeginPeriod(1);
    printf("host: timeBeginPeriod(1)   = %u\n", (unsigned)r);
    DWORD t0 = timeGetTime();
    Sleep(20);
    DWORD t1 = timeGetTime();
    printf("host: timeGetTime delta    = %lu ms\n", (unsigned long)(t1 - t0));
    UINT devs = waveOutGetNumDevs();
    printf("host: waveOutGetNumDevs    = %u\n", (unsigned)devs);
    UINT indevs = waveInGetNumDevs();
    printf("host: waveInGetNumDevs     = %u\n", (unsigned)indevs);
    WAVEOUTCAPSW caps;
    MMRESULT rc = waveOutGetDevCapsW(0, &caps, sizeof(caps));
    printf("host: waveOutGetDevCapsW   = %u\n", (unsigned)rc);
    timeEndPeriod(1);

    int fail = 0;
    if (t1 <= t0)
        { printf("host: FAIL timeGetTime did not advance\n"); fail = 1; }
    if (t1 - t0 < 10)
        { printf("host: FAIL timeGetTime delta too small: %lu\n", (unsigned long)(t1 - t0)); fail = 1; }

    /* 2. the core must be present and must have bootstrapped */
    HMODULE core = GetModuleHandleW(L"aamod_core.dll");
    if (!core) {
        printf("host: FAIL aamod_core.dll was not loaded by the shim\n");
        return 2;
    }
    VersionFn ver = (VersionFn)(void*)GetProcAddress(core, "AAMOD_Version");
    printf("host: aamod core version   = %s\n", ver ? ver() : "(no AAMOD_Version)");
    WaitForCoreFn wait = (WaitForCoreFn)(void*)GetProcAddress(core, "AAMOD_WaitForCore");
    if (wait)
        wait(15000);
    CoreReadyFn ready = (CoreReadyFn)(void*)GetProcAddress(core, "AAMOD_CoreReady");
    if (ready) {
        unsigned int ok = ready();
        printf("host: core bootstrap       = %s\n", ok ? "complete" : "NOT complete");
        if (!ok)
            fail = 1;
    }

    /* 3. report whether a mod directory was seen (info only; asserted by build.ps1) */
    const char* mod_dir = argc > 1 ? argv[1] : "aamod\\mods";
    DWORD attr = GetFileAttributesA(mod_dir);
    printf("host: mod dir (%s)          = %s\n", mod_dir,
           (attr != INVALID_FILE_ATTRIBUTES) ? "present" : "absent");

    /* 4. the runtime anchor resolver must find a function by its message
     *    string, using this process as the search space */
    int (*volatile probe_call)(int) = host_anchor_probe;
    probe_call(0);                                   /* keep the body alive */
    ResolveAnchorFn resolve = (ResolveAnchorFn)(void*)GetProcAddress(core, "AAMOD_ResolveAnchor");
    if (!resolve) {
        printf("host: FAIL aamod_core.dll has no AAMOD_ResolveAnchor\n");
        fail = 1;
    } else {
        void* code = NULL;
        size_t size = 0;
        if (resolve(NULL, "host: anchor probe message %d\n", &code, &size)) {
            uintptr_t begin = (uintptr_t)code;
            uintptr_t probe = (uintptr_t)&host_anchor_probe;
            int inside = probe >= begin && probe < begin + size;
            printf("host: anchor probe         = %p +%zu, probe at %p -> %s\n",
                   code, size, (void*)probe, inside ? "inside" : "OUTSIDE");
            if (!inside)
                fail = 1;
        } else {
            printf("host: FAIL anchor probe did not resolve\n");
            fail = 1;
        }
        /* This literal must be absent, so it has to be built at runtime:
         * anything written in the source would sit in our own .rdata. */
        char missing[64];
        sprintf_s(missing, sizeof(missing), "host: absent literal %c%c%c%c%d",
                  'z', 'q', 'q', 'x', 4711);
        void* bad = (void*)1;
        if (resolve(NULL, missing, &bad, NULL)) {
            printf("host: FAIL negative anchor resolved to %p\n", bad);
            fail = 1;
        } else {
            printf("host: anchor negative      = not found (correct)\n");
        }
    }

    /* 5. present hook, offline. host.exe imports d3d11.dll statically, exactly
     *    like the game does, so D3D11CreateDevice resolves to our proxy in this
     *    directory; the proxy hands the device to the core, the core hooks the
     *    IDXGIFactory, our CreateSwapChain detour hooks the chain, and every
     *    Present dispatches the frame callbacks. */
    HookStateFn hook_state = (HookStateFn)(void*)GetProcAddress(core, "AAMOD_PresentHookState");
    FrameCountFn frame_count = (FrameCountFn)(void*)GetProcAddress(core, "AAMOD_PresentFrameCount");
    typedef size_t (*ResourceCountFn)(void);
    ResourceCountFn active_hooks = (ResourceCountFn)GetProcAddress(core, "AAMOD_ActiveHookCount");
    ResourceCountFn subscribers = (ResourceCountFn)GetProcAddress(core, "AAMOD_SubscriberCount");
    ResourceCountFn images = (ResourceCountFn)GetProcAddress(core, "AAMOD_ImageCount");
    if (active_hooks && active_hooks() != 0) { printf("host: FAIL initialization left hooks\n"); fail = 1; }
    DetachFn detach = (DetachFn)(void*)GetProcAddress(core, "AAMOD_DetachPresent");
    if (!hook_state || !frame_count || !detach) {
        printf("host: FAIL core has no present hook entry points\n");
        fail = 1;
    } else {
        wchar_t d3d_path[MAX_PATH];
        HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
        d3d_path[0] = 0;
        if (d3d)
            GetModuleFileNameW(d3d, d3d_path, MAX_PATH);
        printf("host: d3d11 module         = %ls\n", d3d_path[0] ? d3d_path : L"(not loaded)");

        D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
        D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
        ID3D11Device* device = NULL;
        ID3D11DeviceContext* context = NULL;
        HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &want, 1,
                                       D3D11_SDK_VERSION, &device, &got, &context);
        if (FAILED(hr))
            hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &want, 1,
                                   D3D11_SDK_VERSION, &device, &got, &context);
        printf("host: D3D11CreateDevice    = 0x%08lx (feature level 0x%04x)\n",
               (unsigned long)hr, (unsigned)got);
        if (FAILED(hr)) {
            printf("host: FAIL no D3D11 device (hardware and WARP both failed)\n");
            fail = 1;
        } else {
            unsigned int state = hook_state();
            if (argc > 1 && strcmp(argv[1], "--loader-only") == 0) {
                if (state != 0) fail = 1;
                if (context) context->Release();
                device->Release();
                typedef void (*ShutdownFn)(void);
                ShutdownFn shutdown_core = (ShutdownFn)GetProcAddress(core, "AAMOD_ShutdownCore");
                if (shutdown_core) { shutdown_core(); shutdown_core(); }
                if ((active_hooks && active_hooks()) || (subscribers && subscribers()) || (images && images())) fail = 1;
                printf("host: loader-only state=%u, shutdown clean, RESULT=%s\n", state, fail ? "FAIL" : "OK");
                return fail;
            }
            printf("host: present hook state   = %u after device creation\n", state);
            if (!(state & 1u)) {
                printf("host: FAIL device creation did not hook an IDXGIFactory\n");
                fail = 1;
            }

            IDXGIDevice* dxgi_device = NULL;
            IDXGIAdapter* adapter = NULL;
            IDXGIFactory* factory = NULL;
            HRESULT qhr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
            printf("host: IDXGIDevice          = 0x%08lx, %p\n", (unsigned long)qhr, (void*)dxgi_device);
            if (dxgi_device) {
                qhr = dxgi_device->GetAdapter(&adapter);
                printf("host: GetAdapter           = 0x%08lx, %p\n", (unsigned long)qhr, (void*)adapter);
            }
            if (adapter) {
                qhr = adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);
                printf("host: GetParent(factory)   = 0x%08lx, %p\n", (unsigned long)qhr, (void*)factory);
            }

            HWND window = host_make_window();
            printf("host: window               = %p\n", (void*)window);
            IDXGISwapChain* chain = NULL;
            if (factory && window) {
                DXGI_SWAP_CHAIN_DESC scd;
                ZeroMemory(&scd, sizeof(scd));
                scd.BufferCount = 2;
                scd.BufferDesc.Width = 64;
                scd.BufferDesc.Height = 64;
                scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                scd.OutputWindow = window;
                scd.SampleDesc.Count = 1;
                scd.Windowed = TRUE;
                scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                printf("host: calling factory->CreateSwapChain\n");
                hr = factory->CreateSwapChain(device, &scd, &chain);
                printf("host: CreateSwapChain      = 0x%08lx, chain %p\n", (unsigned long)hr, (void*)chain);
            }

            if (!chain) {
                printf("host: FAIL no swap chain to present\n");
                fail = 1;
            } else {
                state = hook_state();
                printf("host: present hook state   = %u after chain creation\n", state);
                if (!(state & 2u)) {
                    printf("host: FAIL CreateSwapChain did not hook the chain\n");
                    fail = 1;
                }
                unsigned long long before = frame_count();
                for (int i = 0; i < 3; ++i) {
                    hr = chain->Present(0, 0);
                    if (FAILED(hr))
                        break;
                }
                unsigned long long after = frame_count();
                printf("host: present              0x%08lx, frames %llu -> %llu\n",
                       (unsigned long)hr, before, after);
                if (after - before != 3) {
                    printf("host: FAIL expected 3 presented frames, saw %llu\n", after - before);
                    fail = 1;
                }
                /* after detaching, Present must go straight to DXGI again */
                detach();
                unsigned long long frozen = frame_count();
                hr = chain->Present(0, 0);
                printf("host: after detach         frames %llu -> %llu, state %u\n",
                       frozen, frame_count(), hook_state());
                if (frame_count() != frozen || hook_state() != 0) {
                    printf("host: FAIL the present hook survived AAMOD_DetachPresent\n");
                    fail = 1;
                }
                chain->Release();
            }

            /* Regression: the factory2 detour must forward HWND as argument 3. */
            IDXGIFactory2* factory2 = NULL;
            if (factory && SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2))) {
                typedef int (*AttachDeviceFn)(void*);
                AttachDeviceFn attach_device = (AttachDeviceFn)GetProcAddress(core, "AAMOD_OnD3D11Device");
                if (attach_device) attach_device(device);
                DXGI_SWAP_CHAIN_DESC1 desc = {};
                desc.Width = 64; desc.Height = 64;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                IDXGISwapChain1* chain1 = NULL;
                hr = factory2->CreateSwapChainForHwnd(device, window, &desc, NULL, NULL, &chain1);
                printf("host: CreateSwapChainForHwnd = 0x%08lx\n", (unsigned long)hr);
                if (FAILED(hr) || !chain1) fail = 1;
                else {
                    unsigned long long before = frame_count();
                    DXGI_PRESENT_PARAMETERS parameters = {};
                    hr = chain1->Present1(0, 0, &parameters);
                    if (FAILED(hr) || frame_count() != before + 1) fail = 1;
                    detach();
                    chain1->Release();
                }
                factory2->Release();
            } else { printf("host: FAIL factory2 unavailable\n"); fail = 1; }

            if (factory)
                factory->Release();
            if (adapter)
                adapter->Release();
            if (dxgi_device)
                dxgi_device->Release();
            if (context)
                context->Release();
            if (device)
                device->Release();
            if (window)
                DestroyWindow(window);
        }
    }

    typedef void (*ShutdownFn)(void);
    ShutdownFn shutdown_core = (ShutdownFn)GetProcAddress(core, "AAMOD_ShutdownCore");
    if (shutdown_core) { shutdown_core(); shutdown_core(); }
    if ((active_hooks && active_hooks()) || (subscribers && subscribers()) || (images && images())) { printf("host: FAIL shutdown retained resources\n"); fail = 1; }
    if (ready && ready()) { printf("host: FAIL core remained ready after shutdown\n"); fail = 1; }
    printf("host: shutdown              complete, repeated call safe\n");
    printf("host: %s\n", fail ? "RESULT=FAIL" : "RESULT=OK");
    return fail;
}
