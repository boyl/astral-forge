# aamod architecture (M0)

## 1. Why a proxy DLL

`Astral Ascent.exe` is unsigned, unpacked (7 sections, no `.bind`), and imports
`winmm.dll` (20 functions) and `d3d11.dll` (1 function) among ~400 imports.
There is no SteamStub, no IL2CPP, no managed runtime. Therefore the classic
application-directory proxy works:

```
Windows loader
  └─ Astral Ascent.exe
       └─ imports winmm.dll
            └─ <game dir>\winmm.dll          <- ours (proxy)
                 ├─ forwards every export to
                 │    <game dir>\winmmHooked.dll   (copy of System32\winmm.dll)
                 └─ loads <game dir>\aamod_core.dll  -> AttachCore()
                      └─ discovers mods -> LoadLibrary -> mod's AAMOD_Init
```

Two conditions make it valid, both verified on this machine:

* `SafeDllSearchMode` is unset, i.e. the default (1): the application directory
  is searched before `System32`.
* `winmm.dll`, `version.dll`, and `d3d11.dll` are **not** in
  `HKLM\...\Session Manager\KnownDLLs` (31 entries). `install.ps1` re-checks
  this at install time and refuses a shim that is pinned.

Only one shim is needed (`winmm` by default); `version` and `d3d11` exist
because a future milestone may want the D3D11 device/swap-chain entry point, or
a game build may drop the winmm import.

### The shim: MASM stub table, not .def forwarders

MSVC's linker does **not** implement `.def` export forwarders (`name=other.name`
fails with `LNK2001` for every accepted syntax variant, and `/EXPORT:` does the
same), so the shim cannot be a thin forwarding DLL in the usual MinGW sense.

Instead, `tools/gen_shim_def.py` reads the system DLL's export table and emits
per shim:

* `<shim>.def` — every named export with its original ordinal (ordinal-only
  exports are reported and skipped);
* `<shim>_stubs.asm` — one tiny MASM stub per export:
  `lea rax, g_aamod_stub_table / jmp qword ptr [rax + 8*i]`
  (`rax` is not an argument register in the Win64 ABI, so this is transparent);
* `<shim>_table.cpp` — the table, the name list, and
  `aamod_shim_init_table(HMODULE real)` which fills each slot with
  `GetProcAddress(real, name)`;
* `<shim>_config.h` — the shim's identity (`AAMOD_SHIM_HOOKED L"winmmHooked.dll"`).

`DllMain(ATTACH)` resolves the real DLL in this order — `<own dir>\<name>Hooked.dll`
(absolute), then `%SystemDirectory%\<name>.dll` (absolute) — fills the table,
and starts a thread that loads `aamod_core.dll` from its own directory and calls
`AAMOD_AttachCore`. Unresolved slots point at a stub that returns 0 rather than
crashing.

This is also the hook surface for later milestones: any single export can be
redirected to an implementation instead of the real function by replacing one
table entry.

Export parity is enforced in the build: `gen_shim_def.py --check` diffs the
system DLL against the generated `.def` and asserts the asm stub count matches.
Current: winmm 180/180, version 17/17, d3d11 51/51.

## 2. Core bootstrap

`aamod_core.dll` exports `AAMOD_AttachCore(HMODULE)`, `AAMOD_WaitForCore(ms)`,
`AAMOD_CoreReady()`, `AAMOD_Version()`. `AAMOD_AttachCore` spawns a worker
thread — never do work under the loader lock — and returns immediately. That
thread:

1. resolves `game_dir` (`GetModuleFileNameW(NULL)`) and `core_dir`;
2. resolves the data directory: `<game dir>\aamod` if it exists or can be
   created, else `%LOCALAPPDATA%\aamod`;
3. opens `<data>\logs\aamod.log` (append, timestamped, level + thread id);
4. loads/generates `<data>\config.ini`;
5. applies the `[engine]` switches as `CHOWDREN_*` environment variables via
   `SetEnvironmentVariableW` (must happen before Chowdren reads them);
6. resolves mod directories in order: `AAMOD_MOD_DIR` (env) → `general.mod_dirs`
   in the config (`;`-separated) → `<game dir>\aamod\mods` → `<data>\mods` →
   `%LOCALAPPDATA%\aamod\mods`;
7. discovers mods and loads them.

Discovery scans `<dir>\<mod>\mod.json` and reads a flat JSON object
(`json_min.cpp` — deliberately not a general JSON parser). Ordering is
`priority` descending, then `id` ascending; the first occurrence of a duplicate
`id` wins. Loading uses `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` so
a mod's own dependencies resolve next to it, then `GetProcAddress("AAMOD_Init")`
and a single call with `(const AAModAPI*, api_size)`. Mods are unloaded in
reverse order on process exit; `AAMOD_Shutdown` runs first if exported.

The core does **not** keep a private copy of the mod list — the loader is also
the API provider, so a mod can be loaded before or after the built-in ones.

## 3. Public ABI

`include/aamod/aamod.h` is the only file a mod needs. `AAModAPI` starts with
`api_version` and `api_size` and is append-only:

| Field | Notes |
|-------|-------|
| `log(api, level, fmt, ...)` | printf-style into the shared log |
| `config_str/int/bool` | `section`, `key`, `default` from `config.ini` |
| `alloc/free_` | core-owned allocator, for data handed across the boundary |
| `hook_install/remove` | inline hooks, core-owned bookkeeping |
| `game_module/game_dir/mod_dir` | resolved paths (UTF-8) |
| `asset_register`, `event_subscribe` | reserved, currently unused (M2/M4) |

Conventions: `extern "C"`, `__cdecl`, `uint32_t` return codes
(`AAMOD_OK`, `AAMOD_ERR_*`), and mods must not throw across the boundary.
`AAMOD_LOGI(api, ...)` and friends are macros over `api->log`.

## 4. Inline hook engine

`src/core/hook.cpp` implements a 14-byte absolute jump patch:

```
FF 25 00 00 00 00 | <64-bit absolute target>     (replaces >= 14 bytes of prologue)
```

* The prologue is decoded with a compact length decoder (one-byte opcodes plus
  the `0F` map, ModRM/SIB/displacement handling). Instructions that cannot be
  relocated safely are **refused**, not guessed: relative `jmp`/`call`,
  `rel8` branches, and anything the decoder marks unknown cause
  `install()` to return false.
* The trampoline is a relocated copy of the stolen bytes followed by a jump
  back; it is allocated within ±2 GB so RIP-relative operands keep working.
  RIP-relative displacements and `rel32` fields are adjusted to their new
  address.
* `hook_remove(target)` restores the original bytes and frees the trampoline;
  `hook_active()` reports how many patches are live.

The offline test exercises three real prologues, including one with a
RIP-relative global access:

```
hook: installed 00007FF856701140 -> 00007FF856701090 (14 bytes, 4 insn,
      trampoline 00007FF6...0000, delta 69952)
hello: hook self-test multiply: OK (value=54 hits=1 restored=54)
hello: hook self-test 3/3 OK
```

Known limits (intentional, documented in `hook.h`): no mid-instruction targets,
no whole-function relocation, no `jmp`-into-prologue safety check; Windows
requires the patch site to be writable (`VirtualProtect` is applied and the old
protection restored).

## 5. Testing

`build.ps1 -Test` builds everything and then:

1. `gen_shim_def.py --check` for each shim (export parity + stub count);
2. copies `out\{winmm.dll, aamod_core.dll, host.exe}` to `out\test\`, plus
   `hello.dll` + `mod.json` into `out\test\aamod\mods\hello`, plus
   `System32\winmm.dll` as `out\test\winmmHooked.dll`;
3. writes a `config.ini`, runs `host.exe`, and asserts on its stdout
   (`timeBeginPeriod(1)=0`, `timeGetTime delta=20 ms`, `waveOutGetNumDevs=2`,
   `RESULT=OK`) and on the produced `aamod.log` (core startup, discovery,
   `AAMOD_Init`, config read, allocation, three hook self-tests, `loaded`,
   shutdown).

This covers the whole chain without launching the game: real export
forwarding, core load, config, mod lifecycle, and hook install/call/remove.

## 6. Open items after M0

* The overlay hook point and asset override target are identified but not
  wired (see [engine-notes.md](engine-notes.md)).
* The core API is single-threaded by contract: mods are initialised on the
  loader thread. Frame callbacks (M1) will run on the game's main thread, and
  the API will document which calls are safe from where.
* No crash handler yet; a mod that crashes takes the game down with it (M6).
