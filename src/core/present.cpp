/* aamod core - D3D11/DXGI present hook (M1). See present.h for the design. */
#include "present.h"

#include "log.h"
#include <dxgi1_2.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace aamod {
namespace {

/* ------------------------------------------------------------------ COM ---
 * Header-free COM plumbing. Everything the hook needs is reachable through a
 * vtable slot, so the core links against neither d3d11.lib nor dxgi.lib.
 */
struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
};

const Guid kIID_ID3D11Device     = { 0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };
const Guid kIID_IDXGIDevice      = { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };
const Guid kIID_IDXGIFactory     = { 0x7b7166ec, 0x21c7, 0x44ae, { 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69 } };
const Guid kIID_IDXGIFactory2    = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
const Guid kIID_IDXGISwapChain1  = { 0x790a45f7, 0x0d42, 0x4876, { 0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa } };

/* Vtable layout. IUnknown: 0..2, IDXGIObject: 3..6, then per interface. */
enum {
    kSlotQueryInterface = 0,
    kSlotAddRef         = 1,
    kSlotRelease        = 2,
    kSlotGetParent      = 6,

    kSlotDeviceGetAdapter   = 7,   /* IDXGIDevice */
    kSlotDeviceGetContext   = 40,  /* ID3D11Device::GetImmediateContext */

    kSlotFactoryCreate      = 10,  /* IDXGIFactory::CreateSwapChain */
    kSlotFactoryCreateHwnd  = 15,  /* IDXGIFactory2::CreateSwapChainForHwnd */
    kSlotFactoryCreateComp  = 24,  /* IDXGIFactory2::CreateSwapChainForComposition */
    kFactorySlots           = 12,  /* IDXGIFactory */
    kFactory2Slots          = 25,  /* IDXGIFactory2 */

    kSlotChainGetDevice     = 7,   /* IDXGIDeviceSubObject::GetDevice */
    kSlotChainPresent       = 8,
    kSlotChainGetDesc       = 12,
    kSlotChainResizeBuffers = 13,
    kSlotChainPresent1      = 22,
    kChainSlots             = 18,  /* IDXGISwapChain */
    kChain1Slots            = 23,  /* IDXGISwapChain1 */
};

template <typename Fn>
Fn slot_fn(void* object, size_t index)
{
    void** vtbl = *reinterpret_cast<void***>(object);
    return reinterpret_cast<Fn>(vtbl[index]);
}

template <typename Fn>
Fn* slot_ptr(void** vtbl, size_t index)
{
    return reinterpret_cast<Fn*>(&vtbl[index]);
}

HRESULT com_qi(void* object, const Guid& iid, void** out)
{
    if (!object)
        return E_POINTER;
    typedef HRESULT (STDMETHODCALLTYPE *Fn)(void*, const Guid*, void**);
    return slot_fn<Fn>(object, kSlotQueryInterface)(object, &iid, out);
}

ULONG com_add_ref(void* object)
{
    if (!object)
        return 0;
    typedef ULONG (STDMETHODCALLTYPE *Fn)(void*);
    return slot_fn<Fn>(object, kSlotAddRef)(object);
}

ULONG com_release(void* object)
{
    if (!object)
        return 0;
    typedef ULONG (STDMETHODCALLTYPE *Fn)(void*);
    return slot_fn<Fn>(object, kSlotRelease)(object);
}

/* -------------------------------------------------------------- records --- */

struct HookedFactory {
    void*  object;
    void** patched;                 /* our clone of the vtable */
    void** original;                /* the object's vtable before we touched it */
    size_t slots;
    void*  real_create;             /* slot 10 */
    void*  real_create_hwnd;        /* slot 15, may be null */
    void*  real_create_comp;        /* slot 24, may be null */
};

struct HookedChain {
    void*  object;
    void** patched;
    void** original;
    size_t slots;
    void*  real_present;            /* slot 8 */
    void*  real_present1;           /* slot 22, may be null */
    void*  real_resize;             /* slot 13 */
    void*  device;                  /* ID3D11Device*, reference held */
    void*  context;                 /* ID3D11DeviceContext*, reference held */
    uint32_t width;
    uint32_t height;
};

typedef HRESULT (STDMETHODCALLTYPE *PresentFn)(void*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Present1Fn)(void*, UINT, UINT, const void*);
typedef HRESULT (STDMETHODCALLTYPE *ResizeFn)(void*, UINT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *CreateChainFn)(void*, void*, void*, void**);
typedef HRESULT (STDMETHODCALLTYPE *CreateChainForHwndFn)(void*, void*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, void*, void**);
typedef HRESULT (STDMETHODCALLTYPE *CreateChainForCompFn)(void*, void*, const void*, void*, void**);

std::mutex g_lock;
std::vector<HookedFactory*> g_factories;
std::vector<HookedChain*>   g_chains;
std::vector<std::pair<FrameCallback, void*>> g_subscribers;
std::recursive_mutex g_dispatch_lock;
std::atomic<uint64_t> g_frames(0);
std::atomic<uint32_t> g_state(0);
void* g_fallback_present = nullptr;   /* used if a detour cannot identify its chain */

/* How many entries may we copy?
 *
 * The object's real vtable is usually longer than the interface we know about:
 * a factory handed out by IDXGIAdapter::GetParent is an IDXGIFactory7 (~34
 * slots) and a swap chain an IDXGISwapChain4 (~40), and DXGI's own
 * implementation makes virtual calls through that table. If we copied only the
 * prefix we patch, those calls would land on whatever follows our buffer, so we
 * copy the whole table - bounded by the region that holds it, which keeps the
 * read inside committed memory. */
size_t vtable_copy_slots(void** original, size_t minimum)
{
    const size_t kMaxVtableSlots = 128;
    size_t limit = kMaxVtableSlots;

    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(original, &info, sizeof(info)) == sizeof(info)) {
        uintptr_t region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        size_t available = (size_t)((region_end - (uintptr_t)original) / sizeof(void*));
        if (available < limit)
            limit = available;
    }
    if (limit < minimum) {
        AAMOD_ERROR("vtable at %p only has %zu slot(s) mapped, need %zu", original, limit, minimum);
        return 0;
    }
    return limit;
}

void** clone_vtable_wide(void* object, size_t minimum, size_t* copied_slots)
{
    void** original = *reinterpret_cast<void***>(object);
    size_t slots = vtable_copy_slots(original, minimum);
    if (!slots)
        return nullptr;
    void** copy = static_cast<void**>(HeapAlloc(GetProcessHeap(), 0, slots * sizeof(void*)));
    if (!copy)
        return nullptr;
    std::memcpy(copy, original, slots * sizeof(void*));
    if (copied_slots)
        *copied_slots = slots;
    return copy;
}

void install_vtable(void* object, void** patched)
{
    *reinterpret_cast<void***>(object) = patched;
}

bool get_chain(void* object, HookedChain* out)
{
    std::lock_guard<std::mutex> guard(g_lock);
    for (HookedChain* c : g_chains) {
        if (c->object == object) {
            *out = *c;
            return true;
        }
    }
    return false;
}

bool get_factory(void* object, HookedFactory* out)
{
    std::lock_guard<std::mutex> guard(g_lock);
    for (HookedFactory* f : g_factories) {
        if (f->object == object) {
            *out = *f;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- frames --- */

struct Subscriber {
    FrameCallback cb;
    void*         user;
};

std::vector<Subscriber> subscriber_snapshot()
{
    std::lock_guard<std::mutex> guard(g_lock);
    std::vector<Subscriber> out;
    out.reserve(g_subscribers.size());
    for (const auto& entry : g_subscribers)
        out.push_back(Subscriber{ entry.first, entry.second });
    return out;
}

/* A mod callback must never take the game down: swallow whatever it throws. */
void call_frame_callback(FrameCallback cb, const FrameInfo* info, void* user)
{
    __try {
        cb(info, user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        AAMOD_ERROR("frame callback %p raised an exception", (void*)cb);
    }
}

void dispatch_frame(const HookedChain* chain, uint32_t sync_interval, uint32_t flags)
{
    std::lock_guard<std::recursive_mutex> delivery(g_dispatch_lock);
    uint64_t index = g_frames.fetch_add(1) + 1;
    std::vector<Subscriber> subs = subscriber_snapshot();
    if (subs.empty())
        return;

    FrameInfo info;
    info.size          = (uint32_t)sizeof(AAModFrameInfo);
    info.reserved0     = 0;
    info.frame_index   = index;
    info.swap_chain    = chain->object;
    info.device        = chain->device;
    info.context       = chain->context;
    info.width         = chain->width;
    info.height        = chain->height;
    info.sync_interval = sync_interval;
    info.flags         = flags;

    if (index <= 3)
        AAMOD_TRACE("frame %llu (%ux%u) -> %zu subscriber(s)",
                    (unsigned long long)index, info.width, info.height, subs.size());
    for (const Subscriber& sub : subs)
        call_frame_callback(sub.cb, &info, sub.user);
}

/* Only two members of the swap chain description are ever needed:
 * BufferDesc.Width and BufferDesc.Height, the first two uint32 of
 * DXGI_SWAP_CHAIN_DESC. Rather than mirror a 72-byte SDK struct - get its
 * layout wrong by one field and DXGI writes past the stack buffer (this cost us
 * a 0xC0000409) - read into an oversized, 8-byte aligned scratch buffer and pick
 * the two values out of it. */
bool read_backbuffer_size(void* swap_chain, uint32_t* width, uint32_t* height)
{
    alignas(8) unsigned char desc[256];
    std::memset(desc, 0, sizeof(desc));

    typedef HRESULT (STDMETHODCALLTYPE *GetDescFn)(void*, void*);
    HRESULT hr = slot_fn<GetDescFn>(swap_chain, kSlotChainGetDesc)(swap_chain, desc);
    if (FAILED(hr))
        return false;

    std::memcpy(width, desc + 0, sizeof(uint32_t));
    std::memcpy(height, desc + 4, sizeof(uint32_t));
    return true;
}

/* --------------------------------------------------------------- detours --- */

HRESULT STDMETHODCALLTYPE detour_present(void* self, UINT sync_interval, UINT flags)
{
    HookedChain chain;
    if (!get_chain(self, &chain)) {
        AAMOD_ERROR("present: unrecognised swap chain %p", self);
        if (!g_fallback_present)
            return E_FAIL;
        return reinterpret_cast<PresentFn>(g_fallback_present)(self, sync_interval, flags);
    }
    dispatch_frame(&chain, sync_interval, flags);
    return reinterpret_cast<PresentFn>(chain.real_present)(self, sync_interval, flags);
}

HRESULT STDMETHODCALLTYPE detour_present1(void* self, UINT sync_interval, UINT flags, const void* params)
{
    HookedChain chain;
    if (!get_chain(self, &chain) || !chain.real_present1)
        return E_FAIL;
    dispatch_frame(&chain, sync_interval, flags);
    return reinterpret_cast<Present1Fn>(chain.real_present1)(self, sync_interval, flags, params);
}

HRESULT STDMETHODCALLTYPE detour_resize_buffers(void* self, UINT count, UINT width, UINT height,
                                                UINT format, UINT flags)
{
    HookedChain chain;
    if (!get_chain(self, &chain))
        return E_FAIL;
    HRESULT hr = reinterpret_cast<ResizeFn>(chain.real_resize)(self, count, width, height, format, flags);
    if (SUCCEEDED(hr)) {
        uint32_t w = 0, h = 0;
        if (read_backbuffer_size(self, &w, &h)) {
            std::lock_guard<std::mutex> guard(g_lock);
            for (HookedChain* c : g_chains) {
                if (c->object == self) {
                    c->width = w;
                    c->height = h;
                }
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE detour_create_swap_chain(void* self, void* device, void* desc, void** out)
{
    HookedFactory factory;
    if (!get_factory(self, &factory) || !factory.real_create)
        return E_FAIL;
    AAMOD_INFO("CreateSwapChain: factory %p -> real %p", self, factory.real_create);
    HRESULT hr = reinterpret_cast<CreateChainFn>(factory.real_create)(self, device, desc, out);
    void* created = (SUCCEEDED(hr) && out) ? *out : nullptr;
    AAMOD_INFO("CreateSwapChain: hr 0x%08lx, chain %p", (unsigned long)hr, created);
    if (created)
        present_attach_swapchain(created);
    return hr;
}

HRESULT STDMETHODCALLTYPE detour_create_swap_chain_for_hwnd(void* self, void* device, HWND window,
                                                            const DXGI_SWAP_CHAIN_DESC1* desc,
                                                            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, void* restrict_to,
                                                            void** out)
{
    HookedFactory factory;
    if (!get_factory(self, &factory) || !factory.real_create_hwnd)
        return E_FAIL;
    HRESULT hr = reinterpret_cast<CreateChainForHwndFn>(factory.real_create_hwnd)(
        self, device, window, desc, fullscreen, restrict_to, out);
    if (SUCCEEDED(hr) && out && *out)
        present_attach_swapchain(*out);
    return hr;
}

HRESULT STDMETHODCALLTYPE detour_create_swap_chain_for_composition(void* self, void* device, void* desc,
                                                                  void* restrict_to, void** out)
{
    HookedFactory factory;
    if (!get_factory(self, &factory) || !factory.real_create_comp)
        return E_FAIL;
    HRESULT hr = reinterpret_cast<CreateChainForCompFn>(factory.real_create_comp)(
        self, device, desc, restrict_to, out);
    if (SUCCEEDED(hr) && out && *out)
        present_attach_swapchain(*out);
    return hr;
}

/* ---------------------------------------------------------------- attach --- */

bool attach_factory(void* factory)
{
    {
        std::lock_guard<std::mutex> guard(g_lock);
        for (HookedFactory* f : g_factories) {
            if (f->object == factory)
                return true;
        }
    }

    size_t slots = kFactorySlots;
    void* factory2 = nullptr;
    if (SUCCEEDED(com_qi(factory, kIID_IDXGIFactory2, &factory2)) && factory2) {
        slots = kFactory2Slots;
        com_release(factory2);
    }

    size_t copied_slots = 0;
    void** patched = clone_vtable_wide(factory, slots, &copied_slots);
    if (!patched) {
        AAMOD_ERROR("could not clone the IDXGIFactory vtable");
        return false;
    }

    HookedFactory* record = new HookedFactory();
    record->object            = factory;
    record->patched           = patched;
    record->original          = *reinterpret_cast<void***>(factory);
    record->slots             = slots;
    record->real_create       = patched[kSlotFactoryCreate];
    record->real_create_hwnd  = slots > kSlotFactoryCreateHwnd ? patched[kSlotFactoryCreateHwnd] : nullptr;
    record->real_create_comp  = slots > kSlotFactoryCreateComp ? patched[kSlotFactoryCreateComp] : nullptr;

    patched[kSlotFactoryCreate] = reinterpret_cast<void*>(&detour_create_swap_chain);
    if (slots > kSlotFactoryCreateHwnd)
        patched[kSlotFactoryCreateHwnd] = reinterpret_cast<void*>(&detour_create_swap_chain_for_hwnd);
    if (slots > kSlotFactoryCreateComp)
        patched[kSlotFactoryCreateComp] = reinterpret_cast<void*>(&detour_create_swap_chain_for_composition);

    install_vtable(factory, patched);

    std::lock_guard<std::mutex> guard(g_lock);
    g_factories.push_back(record);
    g_state.fetch_or(1);
    AAMOD_INFO("hooked IDXGIFactory %p (%zu vtable slots)", factory, slots);
    return true;
}

} /* namespace */

bool present_attach_swapchain(void* swap_chain)
{
    if (!swap_chain)
        return false;
    AAMOD_INFO("present_attach_swapchain(%p)", swap_chain);
    {
        std::lock_guard<std::mutex> guard(g_lock);
        for (HookedChain* c : g_chains) {
            if (c->object == swap_chain)
                return true;
        }
    }

    size_t slots = kChainSlots;
    void* chain1 = nullptr;
    if (SUCCEEDED(com_qi(swap_chain, kIID_IDXGISwapChain1, &chain1)) && chain1) {
        slots = kChain1Slots;
        com_release(chain1);
    }

    size_t copied_slots = 0;
    void** patched = clone_vtable_wide(swap_chain, slots, &copied_slots);
    if (!patched) {
        AAMOD_ERROR("could not clone the IDXGISwapChain vtable");
        return false;
    }

    HookedChain* record = new HookedChain();
    record->object       = swap_chain;
    record->patched      = patched;
    record->original     = *reinterpret_cast<void***>(swap_chain);
    record->slots        = slots;
    record->real_present = patched[kSlotChainPresent];
    record->real_present1 = slots > kSlotChainPresent1 ? patched[kSlotChainPresent1] : nullptr;
    record->real_resize  = patched[kSlotChainResizeBuffers];
    record->width        = 0;
    record->height       = 0;
    record->device       = nullptr;
    record->context      = nullptr;

    /* The device and its immediate context are what an overlay draws with. Both
     * calls hand us a reference, which we drop in present_detach_all(). */
    typedef HRESULT (STDMETHODCALLTYPE *GetDeviceFn)(void*, const Guid*, void**);
    slot_fn<GetDeviceFn>(swap_chain, kSlotChainGetDevice)(swap_chain, &kIID_ID3D11Device, &record->device);
    if (record->device) {
        typedef void (STDMETHODCALLTYPE *GetContextFn)(void*, void**);
        slot_fn<GetContextFn>(record->device, kSlotDeviceGetContext)(record->device, &record->context);
    }
    read_backbuffer_size(swap_chain, &record->width, &record->height);

    patched[kSlotChainPresent]       = reinterpret_cast<void*>(&detour_present);
    patched[kSlotChainResizeBuffers] = reinterpret_cast<void*>(&detour_resize_buffers);
    if (slots > kSlotChainPresent1)
        patched[kSlotChainPresent1] = reinterpret_cast<void*>(&detour_present1);

    install_vtable(swap_chain, patched);
    com_add_ref(swap_chain);   /* keep the object alive while its vtable is ours */

    {
        std::lock_guard<std::mutex> guard(g_lock);
        g_chains.push_back(record);
        g_state.fetch_or(2);
        if (!g_fallback_present)
            g_fallback_present = record->real_present;
    }
    AAMOD_INFO("hooked IDXGISwapChain %p (%zu vtable slots), device %p, context %p, back buffer %ux%u",
               swap_chain, slots, record->device, record->context, record->width, record->height);
    return true;
}

bool present_attach_device(void* d3d11_device)
{
    if (!d3d11_device)
        return false;

    void* dxgi_device = nullptr;
    if (FAILED(com_qi(d3d11_device, kIID_IDXGIDevice, &dxgi_device)) || !dxgi_device) {
        AAMOD_WARN("device %p is not an IDXGIDevice; no present hook", d3d11_device);
        return false;
    }

    typedef HRESULT (STDMETHODCALLTYPE *GetAdapterFn)(void*, void**);
    void* adapter = nullptr;
    HRESULT hr = slot_fn<GetAdapterFn>(dxgi_device, kSlotDeviceGetAdapter)(dxgi_device, &adapter);
    com_release(dxgi_device);
    if (FAILED(hr) || !adapter) {
        AAMOD_WARN("IDXGIDevice::GetAdapter failed (0x%08lx)", (unsigned long)hr);
        return false;
    }

    void* factory = nullptr;
    hr = slot_fn<HRESULT (STDMETHODCALLTYPE*)(void*, const Guid*, void**)>(
             adapter, kSlotGetParent)(adapter, &kIID_IDXGIFactory, &factory);
    com_release(adapter);
    if (FAILED(hr) || !factory) {
        AAMOD_WARN("IDXGIAdapter::GetParent(IDXGIFactory) failed (0x%08lx)", (unsigned long)hr);
        return false;
    }

    bool hooked = attach_factory(factory);
    if (hooked)
        com_add_ref(factory);   /* hold one reference for the life of the hook */
    com_release(factory);       /* drop the one GetParent handed us */
    return hooked;
}

void present_detach_all()
{
    std::vector<HookedChain*> chains;
    std::vector<HookedFactory*> factories;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        chains.swap(g_chains);
        factories.swap(g_factories);
        g_state.store(0);
        g_fallback_present = nullptr;
    }

    for (HookedChain* c : chains) {
        if (*reinterpret_cast<void***>(c->object) == c->patched)
            install_vtable(c->object, c->original);
        if (c->context)
            com_release(c->context);
        if (c->device)
            com_release(c->device);
        com_release(c->object);
        HeapFree(GetProcessHeap(), 0, c->patched);
        delete c;
    }
    for (HookedFactory* f : factories) {
        if (*reinterpret_cast<void***>(f->object) == f->patched)
            install_vtable(f->object, f->original);
        com_release(f->object);
        HeapFree(GetProcessHeap(), 0, f->patched);
        delete f;
    }
    AAMOD_INFO("present hooks removed (%zu chain(s), %zu factory(s))", chains.size(), factories.size());
}

bool present_subscribe(FrameCallback cb, void* user)
{
    if (!cb)
        return false;
    std::lock_guard<std::mutex> guard(g_lock);
    for (const auto& entry : g_subscribers) {
        if (entry.first == cb && entry.second == user)
            return false;
    }
    if (g_subscribers.size() >= 32)
        return false;
    g_subscribers.push_back(std::make_pair(cb, user));
    return true;
}

bool present_unsubscribe(FrameCallback cb, void* user)
{
    std::lock_guard<std::recursive_mutex> delivery(g_dispatch_lock);
    std::lock_guard<std::mutex> guard(g_lock);
    for (size_t i = 0; i < g_subscribers.size(); ++i) {
        if (g_subscribers[i].first == cb && g_subscribers[i].second == user) {
            g_subscribers.erase(g_subscribers.begin() + i);
            return true;
        }
    }
    return false;
}

void present_unsubscribe_module(HMODULE module)
{
    std::lock_guard<std::recursive_mutex> delivery(g_dispatch_lock);
    std::lock_guard<std::mutex> guard(g_lock);
    for (auto it = g_subscribers.begin(); it != g_subscribers.end();) {
        HMODULE owner = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)it->first, &owner);
        if (owner == module) it = g_subscribers.erase(it);
        else ++it;
    }
}

uint64_t present_frame_count()
{
    return g_frames.load();
}

uint32_t present_hook_state()
{
    return g_state.load();
}

size_t present_subscriber_count()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_subscribers.size();
}

bool present_backbuffer_size(void* swap_chain, uint32_t* width, uint32_t* height)
{
    HookedChain chain;
    if (!get_chain(swap_chain, &chain))
        return false;
    if (width)
        *width = chain.width;
    if (height)
        *height = chain.height;
    return true;
}

} /* namespace aamod */
