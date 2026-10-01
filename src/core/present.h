/* aamod core - D3D11/DXGI present hook (M1).
 *
 * The game imports exactly one function from d3d11.dll (D3D11CreateDevice) and
 * creates its swap chain afterwards through DXGI. So the chain of custody is:
 *
 *   our d3d11 shim intercepts D3D11CreateDevice
 *     -> core: present_attach_device(device)
 *        -> device -> IDXGIDevice -> GetAdapter -> GetParent(IDXGIFactory)
 *           -> patch CreateSwapChain/CreateSwapChainForHwnd in a *cloned*
 *              vtable of that factory object
 *     -> when the game creates its swap chain, the detour sees it
 *        -> present_attach_swapchain(chain)
 *           -> patch Present/Present1/ResizeBuffers in a cloned vtable
 *  -> every presented frame runs the subscribed callbacks, before Present.
 *
 * Cloning the vtable (rather than patching the existing one) keeps this free
 * of any dependency on d3d11.lib/dxgi.lib: the core never links against the
 * graphics stack, it only walks vtables through raw slots.
 */
#ifndef AAMOD_CORE_PRESENT_H
#define AAMOD_CORE_PRESENT_H

#include <cstddef>
#include <cstdint>
#include <windows.h>

#include "aamod/aamod.h"

namespace aamod {

/* The frame info and callback are the public ABI types, deliberately not a
 * private copy: a private mirror that drifts by one field (it did, once) means
 * every mod reads the wrong offsets off the struct we fill. */
typedef AAModFrameInfo FrameInfo;
typedef AAModFrameFn   FrameCallback;

/* Install the swap-chain-creation detour for a D3D11 device. Safe to call more
 * than once (extra devices are ignored once the first factory is hooked). */
bool present_attach_device(void* d3d11_device);

/* Install Present/Present1/ResizeBuffers detours on one swap chain. Called by
 * the factory detour, and directly by tests. */
bool present_attach_swapchain(void* swap_chain);

/* Restore every patched vtable and drop the references we hold. */
void present_detach_all();

bool present_subscribe(FrameCallback cb, void* user);
bool present_unsubscribe(FrameCallback cb, void* user);

uint64_t present_frame_count();
uint32_t present_hook_state();   /* bit 1: factory hooked, bit 2: chain hooked */

bool present_backbuffer_size(void* swap_chain, uint32_t* width, uint32_t* height);

} /* namespace aamod */

#endif /* AAMOD_CORE_PRESENT_H */
