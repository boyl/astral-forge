// aamod - runtime engine anchor resolver.
//
// The game binary is huge (172 MB) and carries no symbols, but it does carry
// the engine's diagnostic strings.  A string literal that a function prints is
// a stable, self-describing handle on that function:
//
//   1. find the literal's bytes inside the mapped image        (string RVA)
//   2. scan .text for RIP-relative operands pointing at it     (xref sites)
//   3. map each site to its function through the .pdata table  (function RVA)
//
// tools/find_anchors.py does the same thing on disk for offline analysis; this
// is the in-process twin used by the loader and by mods at runtime.
//
// Like the offline tool it is *not* a byte-signature: nothing is hardcoded to a
// particular build, so a game update that keeps the message text keeps the
// anchor working.  Messages that disappear simply fail to resolve.
#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace aamod {

struct AnchorHit {
    void*     code       = nullptr;  // function entry point (never mid-function)
    size_t    size       = 0;        // function length in bytes from .pdata
    uintptr_t code_rva   = 0;        // entry RVA, relative to the module base
    uintptr_t string_rva = 0;        // the literal occurrence that was matched
    int       xrefs      = 0;        // reference sites belonging to `code`
    int       sites      = 0;        // literal occurrences found in the image
};

// Resolve `literal` to the function that references it inside `module`
// (NULL means the host executable).  Returns true on success.
bool anchor_resolve(HMODULE module, const char* literal, AnchorHit* out);

// Resolve several literals with a single pass over the code section.
// Returns the number of literals that resolved.
size_t anchor_resolve_all(HMODULE module, const char* const* literals,
                          AnchorHit* out, size_t count);

// The function containing `address` (in `module`), or false when it lies in a
// section without unwind data (leaf assembler, JIT code, ...).
bool anchor_function_of(HMODULE module, const void* address, AnchorHit* out);

// Drop cached module tables (used by tests / after LoadLibrary).
void anchor_flush();

}  // namespace aamod
