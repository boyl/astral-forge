# aamod — an open base mod for Astral Ascent

`aamod` is a runtime mod loader and C ABI for **Astral Ascent** (Construct 2 +
[Chowdren](https://github.com/matpow2/chowdren) v1388, 64-bit, no SteamStub, no
packer). It gives mod authors the low-level pieces that a mod loader normally
has to build from scratch — DLL injection, configuration, logging, memory,
inline hooks, and (in later milestones) frame/event callbacks, overlay UI, and
asset replacement — behind a small, versioned C interface that any language
with a C FFI can call.

Nothing about the game is patched on disk: the loader arrives as a *proxy DLL*
placed next to the executable, exactly the way Ultimate ASI Loader and
ReShade-class hooks work.

## Status

| Milestone | Contents | State |
|-----------|----------|-------|
| M-1 | Recon: injection viability, engine identification, asset format, toolchain | **done** |
| M0 | Proxy shim + core loader + C ABI + inline hook engine | **done, verified offline** |
| M1 | Engine anchor resolver, D3D11/DXGI present hook, frame callbacks | **offline half done, verified offline**; in-game run pending |
| M2 | Game event & system queries (frame id, object list, global variables) | planned |
| M3 | Overlay UI (own minimal renderer, ImGui optional backend) | planned |
| M4 | Asset access/replacement (`Assets.dat` reader, image/sound override) | planned |
| M5 | Data layer: object/instance reads, RNG, input | planned |
| M6 | Hardening: CI, docs, ABI freeze, error reporting, mod packaging | planned |
| M7 | Lua scripting host on top of the C ABI | later |

Everything below M1 is exercised by an offline harness (`build.ps1 -Test`) that
loads the built proxies into a test executable, forwards real calls to renamed
copies of the system DLLs, loads `aamod_core.dll`, discovers and runs a sample
mod, exercises three inline-hook prologue shapes, creates a real D3D11 device and
swap chain, and drives three `Present` calls — the sample mod receives all three
as frame callbacks. **Nothing has run inside the actual game yet**; that needs
the game directory to be written to (see Install).

## Build

Requirements: Visual Studio 2022 Build Tools (MSVC 14.44 or newer, x64),
PowerShell 7, Python 3.11+ (only for `tools/gen_shim_def.py`).

```powershell
cd astral-forge
.\build.ps1 -Test        # build, then run the offline end-to-end test
```

Artifacts land in `out/`:

| File | Purpose |
|------|---------|
| `winmm.dll`, `version.dll`, `d3d11.dll` | proxy shims, export-compatible with the system DLLs |
| `aamod_core.dll` | the loader: config, logging, mod discovery, hooks |
| `host.exe` | test harness (not shipped) |
| `hello.dll` | sample mod (not shipped) |

Each shim is generated: `tools/gen_shim_def.py` reads the export table of the
system DLL and emits a `.def`, a MASM stub file, a jump table, and a config
header. `.\build.ps1 -SkipDef` reuses the existing generated files.

## Install

```powershell
.\install.ps1 -WhatIf              # show exactly what would be written
.\install.ps1                      # requires the game to be closed (winmm proxy)
.\install.ps1 -Shim d3d11          # also install the D3D11 present proxy
.\install.ps1 -Uninstall           # removes only the files it installed
```

The installer copies the selected proxy (`winmm.dll` by default), the matching
renamed copy of the real system DLL (`winmmHooked.dll`), and `aamod_core.dll`
into the game directory, creates `<game>\aamod\{mods,logs}` with a default
`config.ini`, and writes `aamod/install.json` with a SHA-256 per file so
uninstall can verify it removes its own files and nothing else.

`winmm.dll` is enough to get the loader into the game (the executable imports it
statically). `d3d11.dll` additionally hooks device/swap-chain creation, which is
what enables frame callbacks and, later, the overlay; the executable imports
`D3D11CreateDevice` from it statically, and ANGLE's `libGLESv2` also resolves
`d3d11.dll` by base name, so both paths land on the proxy. Only one `-Shim` is
installed per invocation; run the installer again with another `-Shim` to add it.

Mods live in `<game>\aamod\mods\<mod id>\`.

## Writing a mod

A mod is one DLL exporting `AAMOD_Init`; `AAMOD_Shutdown` is optional.

```c
#include <aamod/aamod.h>

AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t api_size) {
    AAMOD_LOGI(api, "my mod up, game dir = %s", api->game_dir);
    int speed = api->config_int("my_mod.speed", 3);

    /* find an engine function by a message it prints, then hook it */
    void* target = NULL;
    size_t size  = 0;
    if (api->anchor_find(NULL, "Could not load image ", &target, &size)) {
        void* trampoline = NULL;
        api->hook_install(target, my_detour, &trampoline);
    }
    return AAMOD_OK;
}

AAMOD_EXPORT void AAMOD_Shutdown(void) { /* optional */ }
```

`my_mod/mod.json` describes the mod:

```json
{ "id": "my_mod", "name": "My Mod", "version": "1.0.0",
  "author": "you", "entry": "my_mod.dll", "priority": 10 }
```

Higher `priority` loads (and initialises) first; ties break on `id`.
`AAMOD_Shutdown` is called in reverse order.

## Frame callbacks

With the `d3d11` proxy installed, the loader hooks the D3D11/DXGI path and turns
every `IDXGISwapChain::Present` into a callback:

```c
static void on_frame(const AAModFrameInfo* info, void* user) {
    if (info->size != sizeof(AAModFrameInfo))   /* ABI guard */
        return;
    /* info->frame_index, info->width, info->height, info->device, info->context */
}
/* in AAMOD_Init */
api->frame_subscribe(on_frame, my_user_data);
```

`frame_subscribe` returns false if the hook could not be installed or the
subscriber table (32 entries) is full; `frame_unsubscribe` stops delivery. The
loader keeps the counters running even without subscribers, and `AAMOD_DetachPresent`
(or `AAMOD_Shutdown`) removes every hook and restores the original vtables.

How it works: the proxy intercepts `D3D11CreateDevice` /
`D3D11CreateDeviceAndSwapChain`, walks `ID3D11Device` → `IDXGIDevice` → adapter →
`IDXGIFactory`, and replaces the *object's* vtable with a copy whose
`CreateSwapChain`/`CreateSwapChainForHwnd`/`CreateSwapChainForComposition` entries
point at load-time detours; the created swap chain gets the same treatment for
`Present`, `Present1` and `ResizeBuffers`. Only that object is affected — other
factories and chains in the process are untouched. No `dxgi.lib`/`d3d11.lib`
imports and no DXGI headers are involved: slots are called through `void**`
indexes, so the loader does not depend on any SDK version. Mod exceptions inside
a callback are caught (`__try`/`__except`) so a broken mod cannot kill the frame.

### ABI policy

`AAModAPI` is a fixed-order struct of function pointers and values, beginning
with `api_version` and `api_size`. New members are only ever **appended**, so
a mod built against an older header keeps working; a mod checks
`api_version`/`api_size` before touching a field it needs. The current version
is `AAMOD_ABI_VERSION = 1`.

## Repository layout

```
include/aamod/aamod.h   public C ABI (the only header mods need)
src/core/               loader: bootstrap, config, logging, mod discovery, hooks,
                        anchor.cpp (function lookup from message strings),
                        present.cpp (D3D11/DXGI device, factory and swap-chain hooks)
src/shim/               shim_common.cpp + per-DLL generated stubs
src/shim/d3d11/         d3d11_intercept.cpp (D3D11CreateDevice* -> core)
tools/                  gen_shim_def.py (export-table driven stub generator)
tools/find_anchors.py   offline anchor miner (--find/--callers/--pointers/--spec)
tools/anchors.json      verified engine anchors for the current game build
mods/hello/             sample mod used by the test harness
tests/host/             offline test executable
docs/                   architecture and engine notes
install.ps1, build.ps1
```

## Finding engine functions

The game ships no symbols, but its functions are usually recognisable by the
messages they print. `tools/find_anchors.py` climbs from a string to the
function that prints it and onwards:

```powershell
python tools\find_anchors.py --find "imgui.ini"        # string -> function
python tools\find_anchors.py --callers 0x5ce670        # who calls it
python tools\find_anchors.py --pointers 0x7c010        # vtable/callback slots
python tools\find_anchors.py --spec tools\anchors.json # re-verify known anchors
```

The same resolution is available to mods at runtime through
`AAModAPI::anchor_find`, so a mod never hardcodes an address. Verified anchors
and the call chains discovered so far are in `docs/engine-notes.md`.

## Notes

* `research/` holds a read-only engine source snapshot used as a reference for
  reverse engineering. It is **not** part of this project, is git-ignored, and
  is never redistributed or linked against.
* Not affiliated with Hibernian Workshop, Scirra, or the Chowdren author.
  Game files are read at runtime only; no game content is bundled.

MIT licensed — see [LICENSE](LICENSE).
