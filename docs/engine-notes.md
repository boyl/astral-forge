# Engine notes (Astral Ascent, build 25330514)

Everything here was established by reading the shipped binary and a read-only
2016 Chowdren source snapshot; line references of the form
`base\file.cpp:123` are from that snapshot and are kept so a claim can be
re-checked. The full source-level survey lives in `research/engine-architecture.md`
(877 lines) and `research/cmdtool/` — both are git-ignored and are reference
material only, never redistributed or linked into this MIT project.

## 1. What the game is

| Fact | Value |
|------|-------|
| Engine | Construct 2, running on **Chowdren v1388** (`Release (Chowdren v1388)`, `...\Chowdren\outsrc\build\Release\Chowdren.pdb`) |
| Binary | `Astral Ascent.exe`, 172,132,352 B, unsigned, 7 sections, no `.bind` (no SteamStub) |
| Runtime | native x64, no Unity/IL2CPP/Mono, no .NET, no embedded Python/Lua |
| Imports | ~400, incl. `d3d11.dll` → `D3D11CreateDevice` only, `winmm.dll` → 20 functions |
| Static libs | zlib, SDL, boost::json, mbedtls (babahttp), Dear ImGui, XAudio2_7/8/9, ANGLE (libEGL/libGLESv2), d3dcompiler_43/46/47 |
| Symbols | ~30 `std::` RTTI names, ~170 decorated names, 7,066 `obj_*` names, 1 PDB path |

`CHOWDREN_SHOW_DEBUGGER` is the only `getenv` in the 2016 snapshot, but v1388
also carries `CHOWDREN_BACKEND_PICKER`, `CHOWDREN_SDL_DEBUG`,
`CHOWDREN_SDL_LOG`, `CHOWDREN_DEBUG_ACHIEVEMENTS` — `aamod` sets these from
`config.ini [engine]` before the engine starts.

The game ships its **own** mod loader (region rva `0x5b2a00`–`0x5b8c00`):
`mods.txt` parsing at rva `0x5b5910`, texture override at `0x5b8600`, Steam
Workshop enumeration at `0x5b54f0`, Steam loader at `0x5b8f90`, and a debugger
gate at rva `0x5b3900` that opens `CONIN$`/`CONOUT$`. Its debug menu is the
game's own Construct event sheet (strings contain Construct BBCode), so it is
not a reusable developer console.

## 2. Assets.dat

`Assets.dat` is 1,600,620,112 B. Its table of contents is a flat array of
little-endian `u32 offset, u32 length` pairs with an 8-byte stride, terminated
by the first payload; the first payload begins at 803,008, giving **100,376
entries**. The last four slots (indices 100372–100375) are garbage that
overlaps real entries, so consumers must filter by "ascending offset,
non-overlapping".

Entries have two shapes:

* **bare files** — e.g. `OggS` (Ogg Vorbis) at index 80,000, `DXBC` with a 4-byte
  prefix (shaders), EBML/`webm` with a 4-byte prefix;
* **record + zlib** — a header followed by a standard zlib stream (`78 9c`).
  The header is `u32 id block` (e.g. `0x01500360` ×3), `u16 count`, a 32-byte
  high-entropy field, `u32 uncompressed size`, then the stream. Its length grows
  in 8-byte steps with the record (66, 74, 82, 90, 98, 106, …), i.e. the zlib
  stream does **not** start at a fixed offset.

The 32-byte field is **not** a content hash: SHA-256 and MD5 of the compressed
stream, of the inflated stream, and of the first 64 header bytes were all
computed and none matched. It is still unexplained.

Index 0 inflates to 290,452 B beginning `DDS ` (864×336). Note that the 2016
source says image records inflate to raw RGBA, so v1388's image format is an
open question — see §5.

### Addressing, per the engine source

`base\assetfile.cpp` and `chowdren\assets.py` agree on the layout: a `u16`
preload handle table followed by one `u32` offset table per asset type
(image, sound, font, shader, file), with `header_size = entries*4 + images*2`.
`AssetFile::set_item(index, type)` then seeks `asset_offsets[type][index]`, so
assets are addressed as **"image #N"**, not by name. File-type assets are
addressed by a name hash produced by a gperf table
(`chowdren\stringhash.py`); the algorithm itself is in mmfparser, which is not
in the snapshot and has not been recovered.

The image record format (per source) is: `u16 width, u16 height, i16 hot_x,
i16 hot_y, i16 action_x, i16 action_y, u32 size`, then a zlib payload decoded by
`stbi_zlib_decode_buffer` into `width*height*4` RGBA bytes
(`base\image.cpp:127-140`; the writer is `chowdren\platforms\common.py:20-25`).

### Override seams

* `get_internal_image(unsigned index)` (`base\image.cpp:486-497`) backed by
  `static Image* internal_images[IMAGE_COUNT]` (`base\image.cpp:481`) — the
  natural place to substitute an image before the game touches it.
* `Image::load()` (`base\image.cpp:111`) — finer control, earlier in the path.
* Global variables live in `vector<DynamicNumber> values` /
  `vector<std::string> values` (`base\globals.h:9-74`), indexed by the
  Construct global-variable table position (`chowdren\converter.py:941-950`).

## 3. Where to hook for a frame callback / overlay

`platform_swap_buffers()` (`base\desktop\platform.cpp:944`) is the single best
hook point:

* it is the only Present/SwapWindow in the tree (`:1028` D3D9 `Present`,
  `:1034` `SDL_GL_SwapWindow`);
* it is called from exactly one place, `base\run.cpp:376-377`, inside
  `GameManager::draw()` — after `Frame::draw` (`:354`), `draw_fade()` (`:355`)
  and `Render::set_offset(0,0)` (`:359`);
* it is where `screen_fbo.unbind()` (`:983`) has already restored the default
  render target and `Render::set_view` was applied (`:986`);
* it carries the stable anchor string `"Failed present: "` (`:1029`).

`platform_begin_draw()` (`:934`) is the second choice.

Main loop, for reference: `GameManager::update()` (`base\run.cpp:596-770`) →
`update_frame()` (`:207-267`, the single event tick `frame->update();` at
`:263`) → `Frame::update()` (`base\common.cpp:1284-1323`, which runs
`data->init`/`on_start` or `handle_pre_events`, then `update_objects()`,
`clean_instances()`, and `data->handle_events()` at `:1313`).

**Events are not table-driven.** Every game event is a member function of one
generated class, `class Frames : public Frame` (`chowdren\converter.py:492-499`),
and event conditions are inlined as `if (...) goto <group>_end`
(`converter.py:2604-2636`). The only id→function mapping is the `switch(id)`
inside `FrameData::event_callback(int)` (`converter.py:1664-1675`). The seven
virtuals of `FrameData` are declared in a fixed order
(`base\frame.h:151-165`): `event_callback, init, on_start, on_end, on_app_end,
handle_events, handle_pre_events`, and the base implementations are empty
(`base\common.cpp:1038-1064`) — which makes **vtable replacement** the cleanest
way to intercept frame-level events.

Object layout is `base\frameobject.h:266-290`. Two gotchas: `x`/`y` are
layer-relative (`get_x() = x + layer->off_x`, `frameobject.h:1030-1038`), and
objects are pool-allocated (`FRAMEOBJECT_HEAD`/`IMPL`, `:248-255`), so anything
that tracks instances must key on `id`, never on the pointer.

## 4. Finding those functions in the shipped binary

The snapshot is 2016 code; the shipped binary is v1388, so addresses must be
recovered. The method used successfully during recon, and the one M1 will
formalise, is **anchor string → xref → `.pdata` function**:

1. take a string that the source proves is unique to the target function
   (`"Failed present: "`, `"mods.txt"`, …);
2. locate it in the executable's mapped image and find the instruction that
   references it (`lea rdx, [rip+…]`);
3. map that address to the enclosing function from the exception directory
   (`.pdata`), which `out/functions.csv` already lists — 154,191 entries with
   ranges, names and sizes.

This is how the game's own mod-loader functions above were located, and it does
not require any debugger or disassembler to be installed.

## 5. Known unknowns

* The `get_file_hash` gperf algorithm (mmfparser is missing from the snapshot).
* Whether v1388 defines `CHOWDREN_USE_DYNAMIC_NUMBER`, which decides if globals
  are plain `double` or `{double value; bool is_fp;}`.
* Construct object-type ids (they start at 2, `converter.py:752`) and global
  variable indices — both live in the game's `.ccn`, not in the code.
* v1388's image record format, given that a sampled payload inflated to `DDS `
  rather than the raw RGBA the 2016 source describes; also unresolved is the
  disk endianness of the hot/action fields.
* Whether the built-in console behind `CHOWDREN_SHOW_DEBUGGER` (rva `0x5b3900`)
  is interactive, and whether the statically linked ImGui is reachable.
* The meaning of the 32-byte field in each Assets.dat record.
