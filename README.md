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
| M1 | Frame/hook API + config + logging polish, first in-game run | next |
| M2 | Game event & system queries (frame id, object list, global variables) | planned |
| M3 | Overlay UI (own minimal renderer, ImGui optional backend) | planned |
| M4 | Asset access/replacement (`Assets.dat` reader, image/sound override) | planned |
| M5 | Data layer: object/instance reads, RNG, input | planned |
| M6 | Hardening: CI, docs, ABI freeze, error reporting, mod packaging | planned |
| M7 | Lua scripting host on top of the C ABI | later |

M0 is verified by an offline harness (`build.ps1 -Test`) that loads the built
`winmm.dll` proxy into a test executable, forwards real calls to a renamed copy
of the system DLL, loads `aamod_core.dll`, discovers and runs a sample mod, and
exercises three inline-hook prologue shapes. It has **not** been run inside the
game yet.

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
.\install.ps1 -WhatIf    # show exactly what would be written
.\install.ps1            # requires the game to be closed
.\install.ps1 -Uninstall # removes only the files it installed
```

The installer copies `winmm.dll` (proxy), `winmmHooked.dll` (the real system
DLL under a new name), and `aamod_core.dll` into the game directory, creates
`<game>\aamod\{mods,logs}` with a default `config.ini`, and writes
`aamod/install.json` with a SHA-256 per file so uninstall can verify it removes
its own files and nothing else.

Mods live in `<game>\aamod\mods\<mod id>\`.

## Writing a mod

A mod is one DLL exporting `AAMOD_Init`; `AAMOD_Shutdown` is optional.

```c
#include <aamod/aamod.h>

AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t api_size) {
    AAMOD_LOGI(api, "my mod up, game dir = %s", api->game_dir);
    int speed = api->config_int(api, "my_mod", "speed", 3);
    void* trampoline = NULL;
    api->hook_install(api, target_address, my_detour, &trampoline);
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

### ABI policy

`AAModAPI` is a fixed-order struct of function pointers and values, beginning
with `api_version` and `api_size`. New members are only ever **appended**, so
a mod built against an older header keeps working; a mod checks
`api_version`/`api_size` before touching a field it needs. The current version
is `AAMOD_ABI_VERSION = 1`.

## Repository layout

```
include/aamod/aamod.h   public C ABI (the only header mods need)
src/core/               loader: bootstrap, config, logging, mod discovery, hooks
src/shim/               shim_common.cpp + per-DLL generated stubs
tools/                  gen_shim_def.py (export-table driven stub generator)
mods/hello/             sample mod used by the test harness
tests/host/             offline test executable
docs/                   architecture and engine notes
install.ps1, build.ps1
```

## Notes

* `research/` holds a read-only engine source snapshot used as a reference for
  reverse engineering. It is **not** part of this project, is git-ignored, and
  is never redistributed or linked against.
* Not affiliated with Hibernian Workshop, Scirra, or the Chowdren author.
  Game files are read at runtime only; no game content is bundled.

MIT licensed — see [LICENSE](LICENSE).
