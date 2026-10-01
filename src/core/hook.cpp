/* aamod inline hooks (x64).
 *
 * Layout of a patched function:
 *
 *   target:   FF 25 00000000 <abs64 detour>  [90 ...]      (>= 14 bytes)
 *   detour  -> calls the trampoline to reach the original
 *   tramp:    <relocated prologue>           FF 25 00000000 <abs64 target+copied>
 *
 * The prologue is relocated instruction by instruction, which needs a real
 * length decoder: MSVC prologues mix push/mov/sub rsp with xmm saves and
 * occasionally RIP-relative or PC-relative operands. Two kinds of operand are
 * fixed up after the copy:
 *
 *   - RIP-relative disp32 (mod=00, rm=101)
 *   - rel32 branches (E8/E9/0F 8x) - a 14-byte patch cannot move a rel8 branch,
 *     so those are refused and reported.
 *
 * Both are 4-byte fields whose value is relative to the *next* instruction, so
 * inside a copy they change by exactly (target - trampoline). The trampoline is
 * therefore allocated within +/-2GB of the target when possible; if a fixup
 * cannot be represented the hook fails instead of corrupting the caller.
 */
#include "hook.h"
#include "log.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <map>

namespace aamod {
namespace hook {

namespace {

const size_t kNoFix = (size_t)-1;

struct Insn {
    size_t len;
    size_t fix_off;     // instruction-relative offset of a 4-byte relative field
    bool   bad_rel8;    // rel8 branch: cannot be relocated
};

struct OpInfo {
    uint8_t modrm;
    uint8_t imm;        // immediate bytes: 0,1,2,3(enter),4,8
    uint8_t rel;        // 1 or 4
    uint8_t bad;
};

struct Tables {
    OpInfo one[256];
    OpInfo two[256];
    OpInfo t38[256];
    OpInfo t3a[256];

    Tables()
    {
        memset(one, 0, sizeof(one));
        memset(two, 0, sizeof(two));
        memset(t38, 0, sizeof(t38));
        memset(t3a, 0, sizeof(t3a));
        for (int i = 0; i < 256; ++i) {
            one[i].bad = two[i].bad = 1;
            t38[i].modrm = 1;                 // all 0F 38 /r forms take a modrm
            t3a[i].modrm = 1;                 // all 0F 3A /r forms take modrm+imm8
            t3a[i].imm = 1;
        }

        // 00-3F: eight arithmetic groups, +0..3 modrm, +4 imm8, +5 imm32
        for (int g = 0; g < 8; ++g) {
            uint8_t b = (uint8_t)(g * 8);
            one[b + 0].modrm = one[b + 1].modrm = one[b + 2].modrm = one[b + 3].modrm = 1;
            one[b + 4].imm = 1;
            one[b + 5].imm = 4;
        }
        one[0x63].modrm = 1;                                  // movsxd
        one[0x68].imm = 4;                                    // push imm32
        one[0x69].modrm = 1; one[0x69].imm = 4;               // imul r,r/m,imm32
        one[0x6A].imm = 1;                                    // push imm8
        one[0x6B].modrm = 1; one[0x6B].imm = 1;               // imul r,r/m,imm8
        for (int i = 0x6C; i <= 0x6F; ++i) one[i].bad = 0;     // ins/outs
        for (int i = 0x70; i <= 0x7F; ++i) one[i].rel = 1;     // jcc rel8
        one[0x80].modrm = 1; one[0x80].imm = 1;
        one[0x81].modrm = 1; one[0x81].imm = 4;
        one[0x83].modrm = 1; one[0x83].imm = 1;
        for (int i = 0x84; i <= 0x8F; ++i) one[i].modrm = 1;   // test/xchg/mov/lea/pop
        for (int i = 0x90; i <= 0x99; ++i) one[i].bad = 0;     // xchg/nop/cwde/cdq
        for (int i = 0x9B; i <= 0x9F; ++i) one[i].bad = 0;     // fwait/pushfq/sahf/lahf
        // 0xA0-0xA3 moffs64: deliberately unsupported (ambiguous length)
        for (int i = 0xA4; i <= 0xA7; ++i) one[i].bad = 0;     // movs/cmps
        one[0xA8].imm = 1; one[0xA9].imm = 4;                  // test al/eax,imm
        for (int i = 0xAA; i <= 0xAF; ++i) one[i].bad = 0;     // stos/lods/scas
        for (int i = 0xB0; i <= 0xB7; ++i) one[i].imm = 1;     // mov r8,imm8
        for (int i = 0xB8; i <= 0xBF; ++i) one[i].imm = 8;     // mov r32/r64,imm (8 only with REX.W)
        one[0xC0].modrm = 1; one[0xC0].imm = 1;
        one[0xC1].modrm = 1; one[0xC1].imm = 1;
        one[0xC2].imm = 2;                                     // ret imm16
        one[0xC3].bad = 0;                                     // ret
        // 0xC4/0xC5 VEX: unsupported
        one[0xC6].modrm = 1; one[0xC6].imm = 1;                // mov r/m8,imm8
        one[0xC7].modrm = 1; one[0xC7].imm = 4;                // mov r/m32,imm32
        one[0xC8].imm = 3;                                     // enter iw,ib
        one[0xC9].bad = 0;                                     // leave
        one[0xCA].imm = 2;                                     // retf imm16
        for (int i = 0xCB; i <= 0xCF; ++i) one[i].bad = 0;     // retf/int/iret
        for (int i = 0xD0; i <= 0xD3; ++i) one[i].modrm = 1;   // shifts
        one[0xD7].bad = 0;                                     // xlat
        for (int i = 0xD8; i <= 0xDF; ++i) one[i].modrm = 1;   // x87
        for (int i = 0xE0; i <= 0xE3; ++i) one[i].rel = 1;     // loop/jrcxz rel8
        for (int i = 0xE4; i <= 0xE7; ++i) one[i].imm = 1;     // in/out imm8
        one[0xE8].rel = 4;                                     // call rel32
        one[0xE9].rel = 4;                                     // jmp rel32
        one[0xEB].rel = 1;                                     // jmp rel8
        for (int i = 0xEC; i <= 0xEF; ++i) one[i].bad = 0;     // in/out dx
        one[0xF1].bad = 0; one[0xF4].bad = 0; one[0xF5].bad = 0;
        one[0xF6].modrm = 1;                                   // grp3 (imm for /0 /1)
        one[0xF7].modrm = 1;
        for (int i = 0xF8; i <= 0xFD; ++i) one[i].bad = 0;     // clc..std
        one[0xFE].modrm = 1;
        one[0xFF].modrm = 1;

        two[0x00].modrm = two[0x01].modrm = two[0x02].modrm = two[0x03].modrm = 1;
        for (int i = 0x05; i <= 0x09; ++i) two[i].bad = 0;     // syscall/clts/sysret/invd/wbinvd
        two[0x0B].bad = 0;                                     // ud2
        two[0x0E].bad = 0;                                     // femms
        two[0x0F].modrm = 1; two[0x0F].imm = 1;                // 3dnow!
        for (int i = 0x10; i <= 0x17; ++i) two[i].modrm = 1;
        for (int i = 0x18; i <= 0x1F; ++i) two[i].modrm = 1;
        for (int i = 0x20; i <= 0x23; ++i) two[i].modrm = 1;
        for (int i = 0x28; i <= 0x2F; ++i) two[i].modrm = 1;
        for (int i = 0x30; i <= 0x35; ++i) two[i].bad = 0;     // wrmsr..sysexit
        two[0x37].bad = 0;                                     // getsec
        for (int i = 0x3B; i <= 0x3F; ++i) two[i].modrm = 1;
        for (int i = 0x40; i <= 0x4F; ++i) two[i].modrm = 1;   // cmovcc
        for (int i = 0x50; i <= 0x6F; ++i) two[i].modrm = 1;   // SSE
        two[0x70].modrm = 1; two[0x70].imm = 1;
        two[0x71].modrm = 1; two[0x71].imm = 1;
        two[0x72].modrm = 1; two[0x72].imm = 1;
        two[0x73].modrm = 1; two[0x73].imm = 1;
        for (int i = 0x74; i <= 0x76; ++i) two[i].modrm = 1;
        two[0x77].bad = 0;                                     // emms
        for (int i = 0x78; i <= 0x7F; ++i) two[i].modrm = 1;
        for (int i = 0x80; i <= 0x8F; ++i) two[i].rel = 4;     // jcc rel32
        for (int i = 0x90; i <= 0x9F; ++i) two[i].modrm = 1;   // setcc
        two[0xA0].bad = two[0xA1].bad = two[0xA2].bad = 0;     // push/pop fs, cpuid
        two[0xA3].modrm = 1;                                   // bt
        two[0xA4].modrm = 1; two[0xA4].imm = 1;                // shld imm8
        two[0xA5].modrm = 1; two[0xA5].imm = 1;                // shrd imm8
        two[0xA8].bad = two[0xA9].bad = two[0xAA].bad = 0;     // push/pop gs, rsm
        two[0xAB].modrm = 1;                                   // bts
        two[0xAC].modrm = 1; two[0xAC].imm = 1;
        two[0xAD].modrm = 1; two[0xAD].imm = 1;
        two[0xAE].modrm = 1;                                   // fence/ldmxcsr/clflush
        two[0xAF].modrm = 1;                                   // imul
        for (int i = 0xB0; i <= 0xB8; ++i) two[i].modrm = 1;   // cmpxchg/movzx/popcnt
        two[0xB9].modrm = 1;                                   // ud1
        two[0xBA].modrm = 1; two[0xBA].imm = 1;                // grp8 bt imm8
        for (int i = 0xBB; i <= 0xBF; ++i) two[i].modrm = 1;   // btc/btr/bsf/bsr/movsx
        two[0xC0].modrm = two[0xC1].modrm = 1;                 // xadd
        two[0xC2].modrm = 1; two[0xC2].imm = 1;                // cmpps
        two[0xC3].modrm = 1;                                   // movnti
        two[0xC4].modrm = 1; two[0xC4].imm = 1;                // pinsrw
        two[0xC5].modrm = 1; two[0xC5].imm = 1;                // pextrw
        two[0xC6].modrm = 1; two[0xC6].imm = 1;                // shufps
        two[0xC7].modrm = 1;                                   // grp9
        for (int i = 0xC8; i <= 0xCF; ++i) two[i].bad = 0;     // bswap
        for (int i = 0xD0; i <= 0xFF; ++i) two[i].modrm = 1;   // SSE/MMX

        // An entry that describes an operand is a valid instruction: clear the
        // "bad" default for those (the intent of every line above).
        for (int i = 0; i < 256; ++i) {
            OpInfo* s[4] = { &one[i], &two[i], &t38[i], &t3a[i] };
            for (int k = 0; k < 4; ++k)
                if (s[k]->modrm || s[k]->imm || s[k]->rel)
                    s[k]->bad = 0;
        }
    }
};

const Tables& tables()
{
    static Tables t;
    return t;
}

bool decode_insn(const uint8_t* code, const uint8_t* limit, Insn* out)
{
    const Tables& T = tables();
    const uint8_t* c = code;
    bool rex_w = false;
    bool opsize16 = false;

    for (;;) {
        if (c >= limit)
            return false;
        uint8_t b = *c;
        if (b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 ||
            b == 0x64 || b == 0x65 || b == 0x67) {
            ++c;
            continue;
        }
        if (b == 0x66) { opsize16 = true; ++c; continue; }
        if ((b & 0xF0) == 0x40) { rex_w = (b & 8) != 0; ++c; continue; }
        break;
    }
    if (c >= limit)
        return false;

    uint8_t op = *c++;
    bool two_byte = false;
    const OpInfo* info = NULL;
    if (op == 0x0F) {
        if (c >= limit)
            return false;
        uint8_t op2 = *c++;
        if (op2 == 0x38) {
            if (c >= limit) return false;
            info = &T.t38[*c++];
        } else if (op2 == 0x3A) {
            if (c >= limit) return false;
            info = &T.t3a[*c++];
        } else {
            info = &T.two[op2];
            two_byte = true;
        }
    } else {
        info = &T.one[op];
    }
    if (info->bad)
        return false;

    out->len = 0;
    out->fix_off = kNoFix;
    out->bad_rel8 = false;

    uint8_t modrm_reg = 0;
    bool has_modrm = info->modrm != 0;
    if (has_modrm) {
        if (c >= limit)
            return false;
        uint8_t modrm = *c;
        modrm_reg = (uint8_t)((modrm >> 3) & 7);
        uint8_t mod = (uint8_t)((modrm >> 6) & 3);
        uint8_t rm = (uint8_t)(modrm & 7);
        ++c;
        if (mod != 3) {
            if (rm == 4) {                                  // SIB
                if (c >= limit)
                    return false;
                uint8_t sib = *c++;
                if (mod == 0 && (sib & 7) == 5) {
                    if (c + 4 > limit) return false;
                    c += 4;                                 // disp32, not rip-relative
                }
            } else if (rm == 5 && mod == 0) {                // RIP-relative disp32
                if (c + 4 > limit)
                    return false;
                out->fix_off = (size_t)(c - code);
                c += 4;
            }
            if (mod == 1) {
                if (c >= limit) return false;
                c += 1;
            } else if (mod == 2) {
                if (c + 4 > limit) return false;
                c += 4;
            }
        }
    }

    uint8_t imm = info->imm;
    if (!two_byte && (op == 0xF6 || op == 0xF7) && has_modrm && modrm_reg <= 1)
        imm = (op == 0xF6) ? 1 : 4;
    if (imm == 3) {                                          // enter iw,ib
        if (c + 3 > limit) return false;
        c += 3;
    } else if (imm) {
        size_t n = imm;
        if (n == 4 && opsize16) n = 2;                       // iz under a 66 prefix
        if (n == 8 && !rex_w) n = 4;                         // movabs needs REX.W
        if (c + n > limit) return false;
        c += n;
    }

    if (info->rel) {
        size_t n = info->rel;
        if (c + n > limit)
            return false;
        if (n == 1)
            out->bad_rel8 = true;
        else
            out->fix_off = (size_t)(c - code);
        c += n;
    }

    out->len = (size_t)(c - code);
    return out->len != 0;
}

void log_bytes(const char* what, const void* target, size_t off, const uint8_t* p, size_t n)
{
    char hex[3 * 16 + 1];
    size_t k = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        static const char* d = "0123456789ABCDEF";
        hex[k++] = d[p[i] >> 4];
        hex[k++] = d[p[i] & 0xF];
        hex[k++] = ' ';
    }
    hex[k] = 0;
    AAMOD_WARN("hook: %s at %p+%zu [%s]", what, target, off, hex);
}

/* Allocate an executable page close enough to `target` that 32-bit relative
 * fields keep working after relocation. */
void* alloc_near(const void* target, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t granularity = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t max_delta = 0x70000000ULL;              // well inside +/-2GB
    const uintptr_t t = (uintptr_t)target;

    for (uintptr_t delta = granularity; delta < max_delta; delta += granularity) {
        if (t > delta) {
            void* p = VirtualAlloc((void*)(t - delta), size, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_EXECUTE_READWRITE);
            if (p)
                return p;
        }
        void* p = VirtualAlloc((void*)(t + delta), size, MEM_COMMIT | MEM_RESERVE,
                               PAGE_EXECUTE_READWRITE);
        if (p)
            return p;
        if (delta > 0x4000000ULL)                           // give up scanning far away
            break;
    }
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

struct Patch {
    void*   target;
    uint8_t original[32];
    size_t  patched_len;
    void*   trampoline;
};

std::map<void*, Patch> g_patches;

} // anonymous namespace

size_t instruction_length(const uint8_t* code, const uint8_t* limit)
{
    Insn in;
    if (!decode_insn(code, limit, &in))
        return 0;
    return in.len;
}

bool is_padding(const uint8_t* code, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (code[i] != 0xCC && code[i] != 0x90 && code[i] != 0x00)
            return false;
    }
    return true;
}

bool install(void* target, void* detour, void** trampoline, size_t min_prologue)
{
    if (!target || !detour)
        return false;
    if (g_patches.find(target) != g_patches.end()) {
        AAMOD_WARN("hook: %p already patched", target);
        return false;
    }

    const uint8_t* code = (const uint8_t*)target;
    const size_t want = min_prologue < 14 ? 14 : min_prologue;
    Insn insns[16];
    size_t copied = 0;
    size_t count = 0;

    while (copied < want) {
        if (count >= 16) {
            log_bytes("prologue too long", target, copied, code + copied, 16);
            return false;
        }
        Insn in;
        if (!decode_insn(code + copied, code + copied + 15, &in)) {
            log_bytes("undecodable prologue", target, copied, code + copied, 16);
            return false;
        }
        if (in.bad_rel8) {
            log_bytes("8-bit relative branch in prologue", target, copied, code + copied, 16);
            return false;
        }
        insns[count++] = in;
        copied += in.len;
    }

    const size_t total = copied + 14;
    uint8_t* tramp = (uint8_t*)alloc_near(target, total);
    if (!tramp) {
        AAMOD_ERROR("hook: VirtualAlloc failed for trampoline");
        return false;
    }
    memcpy(tramp, code, copied);

    // Relocate the relative fields: each is relative to the next instruction,
    // so inside the copy every value shifts by exactly (target - tramp).
    const int64_t delta = (int64_t)((const uint8_t*)target - tramp);
    size_t off = 0;
    for (size_t i = 0; i < count; ++i) {
        if (insns[i].fix_off != kNoFix) {
            uint8_t* p = tramp + off + insns[i].fix_off;
            int32_t old = 0;
            memcpy(&old, p, 4);
            int64_t v = (int64_t)old + delta;
            if (v < INT32_MIN || v > INT32_MAX) {
                AAMOD_WARN("hook: relative fixup out of range at %p+%zu "
                           "(trampoline %p, delta %lld)", target, off, tramp, (long long)delta);
                VirtualFree(tramp, 0, MEM_RELEASE);
                return false;
            }
            int32_t nv = (int32_t)v;
            memcpy(p, &nv, 4);
        }
        off += insns[i].len;
    }

    // trampoline tail: jmp back to target+copied
    tramp[copied + 0] = 0xFF;
    tramp[copied + 1] = 0x25;
    *(uint32_t*)(tramp + copied + 2) = 0;
    *(uint64_t*)(tramp + copied + 6) = (uint64_t)(code + copied);

    // patch site: jmp qword ptr [rip+0] ; <abs64 detour> ; nop padding
    uint8_t patch[32];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xFF;
    patch[1] = 0x25;
    *(uint32_t*)(patch + 2) = 0;
    *(uint64_t*)(patch + 6) = (uint64_t)detour;

    Patch p;
    p.target = target;
    p.patched_len = copied;
    p.trampoline = tramp;
    memset(p.original, 0, sizeof(p.original));
    memcpy(p.original, code, copied);

    DWORD old_prot = 0;
    if (!VirtualProtect(target, copied, PAGE_EXECUTE_READWRITE, &old_prot)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        AAMOD_ERROR("hook: VirtualProtect failed at %p (err %lu)", target, GetLastError());
        return false;
    }
    memcpy(target, patch, copied);
    FlushInstructionCache(GetCurrentProcess(), target, copied);
    VirtualProtect(target, copied, old_prot, &old_prot);

    g_patches[target] = p;
    if (trampoline)
        *trampoline = tramp;
    AAMOD_INFO("hook: installed %p -> %p (%zu bytes, %zu insn, trampoline %p, delta %lld)",
               target, detour, copied, count, tramp, (long long)delta);
    return true;
}

bool remove(void* target)
{
    std::map<void*, Patch>::iterator it = g_patches.find(target);
    if (it == g_patches.end())
        return false;
    Patch& p = it->second;

    DWORD old_prot = 0;
    if (VirtualProtect(target, p.patched_len, PAGE_EXECUTE_READWRITE, &old_prot)) {
        memcpy(target, p.original, p.patched_len);
        FlushInstructionCache(GetCurrentProcess(), target, p.patched_len);
        VirtualProtect(target, p.patched_len, old_prot, &old_prot);
    }
    VirtualFree(p.trampoline, 0, MEM_RELEASE);
    g_patches.erase(it);
    AAMOD_INFO("hook: removed %p", target);
    return true;
}

size_t active()
{
    return g_patches.size();
}

} // namespace hook
} // namespace aamod
