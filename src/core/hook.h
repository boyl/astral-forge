#ifndef AAMOD_HOOK_H
#define AAMOD_HOOK_H

#include <stdint.h>
#include <stddef.h>

namespace aamod {
namespace hook {

// x64 inline hook: overwrite the head of `target` with
//   FF 25 00 00 00 00 <abs64>      (jmp qword ptr [rip+0])  = 14 bytes
// and return a trampoline = relocated prologue + jmp back to target+patch_len.
//
// The prologue is relocated instruction by instruction using a full one-byte +
// 0F-map length decoder. Four-byte relative fields (RIP-relative operands and
// rel32 branches) are re-based onto the trampoline, which is allocated within
// +/-2GB of the target for that reason. A hook fails (returns false, target
// untouched) rather than corrupting the host process when:
//   - an instruction cannot be decoded,
//   - the prologue contains a rel8 branch (not relocatable),
//   - a rebased displacement does not fit in 32 bits,
//   - the target is already patched.
//
// `min_prologue` may be used to demand more than 14 bytes (e.g. 16) so a later
// hook can be layered on the same site.
bool install(void* target, void* detour, void** trampoline, size_t min_prologue = 14);
bool remove(void* target);

// Number of currently installed hooks.
size_t active();

// Length of one x64 instruction starting at `code` (0 = unrecognised).
size_t instruction_length(const uint8_t* code, const uint8_t* limit);

// True if the detected patch site looks like function padding / int3s.
bool is_padding(const uint8_t* code, size_t n);

} // namespace hook
} // namespace aamod

#endif // AAMOD_HOOK_H
