#include "anchor.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace aamod {
namespace {

// A literal normally occurs once; allow a few duplicates before we give up on
// it (some format strings are duplicated across static libraries).
constexpr size_t kMaxOccurrences = 8;

// x86-64 RIP-relative operand prefixes we understand:
//   REX? 0x8D  (lea  reg, [rip+disp])    ModRM mod=00 rm=101
//   REX? 0x8B  (mov  reg, [rip+disp])    ModRM mod=00 rm=101
// The opcode byte sits at `j`; the instruction ends after the disp32, so the
// instruction length is (j - start) + 1(opcode) + 1(modrm) + 4(disp).
bool rip_operand_at(const uint8_t* p, size_t n, size_t i,
                    size_t* length, int32_t* disp)
{
    size_t j = i;
    if (p[j] >= 0x40 && p[j] <= 0x4F)
        ++j;
    if (j + 6 > n)
        return false;
    const uint8_t op = p[j];
    if (op != 0x8D && op != 0x8B)
        return false;
    if ((p[j + 1] & 0xC7) != 0x05)
        return false;
    ::memcpy(disp, p + j + 2, 4);
    *length = (j - i) + 6;
    return true;
}

struct Section {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    uint32_t       rva  = 0;
};

struct ModuleInfo {
    const uint8_t*           base       = nullptr;
    size_t                   image_size = 0;
    std::vector<Section>     sections;                  // mapped sections
    const uint8_t*           text       = nullptr;      // executable section
    size_t                   text_size  = 0;
    uint32_t                 text_rva   = 0;
    const RUNTIME_FUNCTION*  pdata      = nullptr;      // sorted by BeginAddress
    size_t                   pdata_count = 0;
    bool                     valid      = false;
};

std::mutex& cache_mutex()
{
    static std::mutex m;
    return m;
}

std::vector<std::pair<HMODULE, ModuleInfo>>& cache()
{
    static std::vector<std::pair<HMODULE, ModuleInfo>> c;
    return c;
}

const IMAGE_NT_HEADERS64* nt_headers(const uint8_t* base)
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    const IMAGE_NT_HEADERS64* nt =
        (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;
    return nt;
}

bool load_module(HMODULE mod, ModuleInfo& mi)
{
    if (!mod)
        mod = GetModuleHandleW(NULL);
    if (!mod)
        return false;

    const uint8_t* base = (const uint8_t*)mod;
    const IMAGE_NT_HEADERS64* nt = nt_headers(base);
    if (!nt)
        return false;

    mi.base = base;
    mi.image_size = nt->OptionalHeader.SizeOfImage;

    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const uint8_t* data = base + sec->VirtualAddress;
        size_t size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize
                                            : sec->SizeOfRawData;
        if (!size)
            continue;
        Section s;
        s.data = data;
        s.size = size;
        s.rva  = sec->VirtualAddress;
        mi.sections.push_back(s);

        const char* name = (const char*)sec->Name;
        if (::strncmp(name, ".text", 5) == 0) {
            mi.text      = data;
            mi.text_size = size;
            mi.text_rva  = sec->VirtualAddress;
        } else if (::strncmp(name, ".pdata", 6) == 0) {
            mi.pdata       = (const RUNTIME_FUNCTION*)data;
            mi.pdata_count = size / sizeof(RUNTIME_FUNCTION);
        }
    }

    mi.valid = mi.text && mi.text_size > 0 && mi.pdata && mi.pdata_count > 0;
    return mi.valid;
}

const ModuleInfo* module_info(HMODULE mod)
{
    if (!mod)
        mod = GetModuleHandleW(NULL);

    std::lock_guard<std::mutex> lock(cache_mutex());
    for (auto& entry : cache()) {
        if (entry.first == mod)
            return entry.second.valid ? &entry.second : NULL;
    }
    ModuleInfo mi;
    load_module(mod, mi);
    cache().push_back(std::make_pair(mod, mi));
    const ModuleInfo& stored = cache().back().second;
    return stored.valid ? &stored : NULL;
}

// Every literal occurrence of `lit` in the image, up to kMaxOccurrences.
size_t find_literal(const ModuleInfo& mi, const char* lit,
                    std::vector<uint32_t>* rvas)
{
    const size_t len = ::strlen(lit);
    if (!len)
        return 0;
    size_t found = 0;
    for (const Section& s : mi.sections) {
        if (s.size < len)
            continue;
        const size_t limit = s.size - len;
        for (size_t off = 0; off <= limit; ++off) {
            if (s.data[off] != (uint8_t)lit[0])
                continue;
            if (::memcmp(s.data + off, lit, len) != 0)
                continue;
            if (rvas)
                rvas->push_back(s.rva + (uint32_t)off);
            if (++found >= kMaxOccurrences)
                return found;
        }
    }
    return found;
}

// .pdata lookup: the table is sorted by BeginAddress, so the entry we want is
// the last one starting at or before `rva`.
bool function_at(const ModuleInfo& mi, uint32_t rva,
                 uint32_t* func_rva, size_t* func_size)
{
    size_t lo = 0;
    size_t hi = mi.pdata_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (mi.pdata[mid].BeginAddress <= rva)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return false;
    const RUNTIME_FUNCTION& f = mi.pdata[lo - 1];
    if (f.EndAddress <= f.BeginAddress || rva >= f.EndAddress)
        return false;
    if (func_rva)
        *func_rva = f.BeginAddress;
    if (func_size)
        *func_size = (size_t)(f.EndAddress - f.BeginAddress);
    return true;
}

struct Wanted {
    uint32_t rva;     // literal RVA
    uint16_t index;   // literal index
};

struct Caller {
    uint32_t func_rva;
    uint32_t string_rva;
};

}  // namespace

void anchor_flush()
{
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache().clear();
}

size_t anchor_resolve_all(HMODULE mod, const char* const* literals,
                          AnchorHit* out, size_t count)
{
    if (!out || !literals || !count)
        return 0;
    for (size_t i = 0; i < count; ++i)
        out[i] = AnchorHit();

    const ModuleInfo* mi = module_info(mod);
    if (!mi)
        return 0;

    // ---- step 1: locate every literal occurrence ----------------------------
    std::vector<Wanted> wanted;
    std::vector<int>    sites(count, 0);
    for (size_t k = 0; k < count; ++k) {
        if (!literals[k] || !literals[k][0])
            continue;
        std::vector<uint32_t> rvas;
        const size_t n = find_literal(*mi, literals[k], &rvas);
        sites[k] = (int)n;
        for (uint32_t rva : rvas) {
            Wanted w;
            w.rva   = rva;
            w.index = (uint16_t)k;
            wanted.push_back(w);
        }
    }
    if (wanted.empty())
        return 0;
    std::sort(wanted.begin(), wanted.end(),
              [](const Wanted& a, const Wanted& b) { return a.rva < b.rva; });

    // ---- step 2: one pass over .text for RIP-relative references ------------
    std::vector<Caller> callers;
    const uint8_t* p = mi->text;
    const size_t   n = mi->text_size;
    for (size_t i = 0; i + 6 <= n; ++i) {
        size_t  length = 0;
        int32_t disp   = 0;
        if (!rip_operand_at(p, n, i, &length, &disp))
            continue;
        const int64_t target =
            (int64_t)mi->text_rva + (int64_t)i + (int64_t)length + disp;
        if (target < 0 || target > 0xFFFFFFFFll)
            continue;
        const uint32_t rva = (uint32_t)target;
        auto it = std::lower_bound(wanted.begin(), wanted.end(), rva,
                    [](const Wanted& w, uint32_t v) { return w.rva < v; });
        for (; it != wanted.end() && it->rva == rva; ++it) {
            uint32_t func_rva = 0;
            if (!function_at(*mi, (uint32_t)(mi->text_rva + i),
                             &func_rva, NULL))
                continue;
            Caller c;
            c.func_rva   = func_rva;
            c.string_rva = rva;
            callers.push_back(c);
            // remember which literal this belongs to below
            (void)it->index;
        }
        if (!callers.empty())
            ; // keep the loop branch-free
    }

    // ---- step 3: pick, per literal, the function with the most references ---
    size_t resolved = 0;
    for (size_t k = 0; k < count; ++k) {
        if (!sites[k])
            continue;
        // Which literal occurrences did we match?
        std::vector<uint32_t> mine;
        for (const Wanted& w : wanted)
            if (w.index == k)
                mine.push_back(w.rva);
        if (mine.empty())
            continue;
        std::sort(mine.begin(), mine.end());

        // count callers per function
        struct Tally { uint32_t func_rva; uint32_t string_rva; int count; };
        std::vector<Tally> tally;
        for (const Caller& c : callers) {
            if (!std::binary_search(mine.begin(), mine.end(), c.string_rva))
                continue;
            bool merged = false;
            for (Tally& t : tally) {
                if (t.func_rva == c.func_rva) {
                    ++t.count;
                    merged = true;
                    break;
                }
            }
            if (!merged)
                tally.push_back(Tally{c.func_rva, c.string_rva, 1});
        }
        if (tally.empty())
            continue;
        std::sort(tally.begin(), tally.end(), [](const Tally& a, const Tally& b) {
            if (a.count != b.count)
                return a.count > b.count;
            return a.func_rva < b.func_rva;
        });
        const Tally& best = tally[0];

        AnchorHit& hit = out[k];
        hit.code       = (void*)(mi->base + best.func_rva);
        hit.code_rva   = best.func_rva;
        hit.string_rva = best.string_rva;
        hit.xrefs      = best.count;
        hit.sites      = sites[k];
        uint32_t frva  = best.func_rva;
        size_t   fsize = 0;
        if (function_at(*mi, frva, NULL, &fsize))
            hit.size = fsize;
        ++resolved;
    }
    return resolved;
}

bool anchor_resolve(HMODULE mod, const char* literal, AnchorHit* out)
{
    if (!literal)
        return false;
    const char* list[1] = { literal };
    AnchorHit hit;
    if (anchor_resolve_all(mod, list, &hit, 1) != 1)
        return false;
    if (out)
        *out = hit;
    return true;
}

bool anchor_function_of(HMODULE mod, const void* address, AnchorHit* out)
{
    const ModuleInfo* mi = module_info(mod);
    if (!mi || !address)
        return false;
    const uint8_t* addr = (const uint8_t*)address;
    if (addr < mi->base || addr >= mi->base + mi->image_size)
        return false;
    uint32_t func_rva = 0;
    size_t   func_size = 0;
    if (!function_at(*mi, (uint32_t)(addr - mi->base), &func_rva, &func_size))
        return false;
    if (out) {
        *out = AnchorHit();
        out->code     = (void*)(mi->base + func_rva);
        out->code_rva = func_rva;
        out->size     = func_size;
    }
    return true;
}

}  // namespace aamod
