#!/usr/bin/env python3
"""Locate engine functions inside a shipped binary by their string anchors.

The method, in four steps:

1. take string literals that a read-only source snapshot proves belong to a
   target function (an engine error message, a file name, ...);
2. find those literals inside the executable image (they live in .rdata);
3. scan .text for RIP-relative operands that reference them -- a
   ``lea reg, [rip+disp32]`` is how every x64 compiler materialises a string
   pointer, and requiring the computed target to equal a known literal RVA
   makes the scan essentially false-positive free;
4. map each reference site back to its owning function with the exception
   directory (.pdata), which is a sorted array of
   ``{begin_rva, end_rva, unwind_info}`` triples.

Because a function usually fails with more than one message, the literals that
land in the same function form a cluster; ``--mine`` scores those clusters
against the source snapshot and guesses the function's name from the overlap.
No disassembler, debugger or symbol file is required, and the same algorithm is
what ``src/core/anchor.cpp`` implements at runtime (so this tool is the offline
twin of the loader's own resolver).

Examples
--------
    python tools/find_anchors.py --mine
    python tools/find_anchors.py --spec tools/anchors.json
    python tools/find_anchors.py --selftest
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import struct
import sys

DEFAULT_EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Astral Ascent\Astral Ascent.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SRC = os.path.join(ROOT, "research", "cmdtool", "base")

# --------------------------------------------------------------------------- PE


class PE:
    """Just enough of the PE format: sections, RVAs, and the function table."""

    def __init__(self, path: str):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe : pe + 4] != b"PE\0\0":
            raise ValueError("bad PE signature")
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        self.optional_off = pe + 24
        self.magic = struct.unpack_from("<H", d, self.optional_off)[0]
        if self.magic != 0x20B:
            raise ValueError("expected PE32+ (0x20B), got 0x%X" % self.magic)
        self.image_base = struct.unpack_from("<Q", d, self.optional_off + 24)[0]
        self.section_align = struct.unpack_from("<I", d, self.optional_off + 32)[0]
        self.entry_rva = struct.unpack_from("<I", d, self.optional_off + 16)[0]
        sec_off = self.optional_off + opt_size
        self.sections = []
        for i in range(nsec):
            o = sec_off + 40 * i
            name = d[o : o + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", d, o + 8)
            self.sections.append(
                dict(name=name, va=va, vsize=vsize, raw_size=raw_size, raw_ptr=raw_ptr)
            )

    def section(self, name: str):
        for s in self.sections:
            if s["name"] == name:
                return s
        return None

    def rva_to_off(self, rva: int) -> int | None:
        for s in self.sections:
            span = max(s["vsize"], s["raw_size"])
            if s["va"] <= rva < s["va"] + span:
                off = s["raw_ptr"] + (rva - s["va"])
                return off if off < s["raw_ptr"] + s["raw_size"] else None
        return None

    def off_to_rva(self, off: int) -> int | None:
        for s in self.sections:
            if s["raw_ptr"] <= off < s["raw_ptr"] + s["raw_size"]:
                return s["va"] + (off - s["raw_ptr"])
        return None

    def load_functions(self):
        """Parsed .pdata, as a sorted parallel array of (begin, end) RVAs."""
        sec = self.section(".pdata")
        if sec is None:
            self.func_begin, self.func_end = [], []
            return 0
        raw = self.data[sec["raw_ptr"] : sec["raw_ptr"] + sec["raw_size"]]
        begins, ends = [], []
        for i in range(0, len(raw) - 11, 12):
            b, e, _u = struct.unpack_from("<III", raw, i)
            if b == 0 or e <= b:
                continue
            begins.append(b)
            ends.append(e)
        self.func_begin, self.func_end = begins, ends
        return len(begins)

    def function_of(self, rva: int):
        """(begin, end) of the function containing rva, or None."""
        i = bisect.bisect_right(self.func_begin, rva) - 1
        if i < 0:
            return None
        if rva < self.func_end[i]:
            return (self.func_begin[i], self.func_end[i])
        return None

    def function_size(self, begin: int) -> int:
        i = bisect.bisect_left(self.func_begin, begin)
        if i < len(self.func_begin) and self.func_begin[i] == begin:
            return self.func_end[i] - begin
        return 0


# ------------------------------------------------------------------- literals

EXE_STRING_RE = re.compile(rb"[\x20-\x7e]{4,}")  # kept for --strings diagnostics
SRC_LITERAL_RE = re.compile(r'"((?:[^"\\\n]|\\.){3,})"')
_SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}


def unescape_c(lit: str) -> str | None:
    out = []
    i = 0
    while i < len(lit):
        c = lit[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        if i + 1 >= len(lit):
            return None
        e = lit[i + 1]
        if e in _SIMPLE_ESCAPES:
            out.append(_SIMPLE_ESCAPES[e])
        elif e == "x":
            try:
                out.append(chr(int(lit[i + 2 : i + 4], 16)))
            except ValueError:
                return None
            i += 2
        else:
            return None  # trigraphs, unicode escapes: not worth guessing
        i += 2
    return "".join(out)


def source_literals(files, min_len=4):
    """{literal_bytes: [(file, line)]} harvested from C/C++ sources."""
    found: dict[bytes, list[tuple[str, int]]] = {}
    for path in files:
        try:
            text = open(path, "r", encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        rel = os.path.relpath(path, ROOT)
        for lineno, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith(("//", "*", "#")):
                continue
            for m in SRC_LITERAL_RE.finditer(line):
                lit = unescape_c(m.group(1))
                if lit is None or len(lit) < min_len:
                    continue
                try:
                    raw = lit.encode("ascii")
                except UnicodeEncodeError:
                    continue
                if b"\0" in raw:
                    continue
                found.setdefault(raw, []).append((rel, lineno))
    return found


def iter_sources(root):
    exts = (".cpp", ".h", ".hpp", ".c")
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if fn.endswith(exts):
                yield os.path.join(dirpath, fn)


# -------------------------------------------------------------- xref scanning

# mod=00, rm=101 -> [rip+disp32]; the four variants below cover lea/mov with and
# without REX.W/R (32-bit forms included so a 32-bit-only code path still hits).
XREF_RE = re.compile(
    rb"(?:\x48|\x4c)?(?:\x8d|\x8b)[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]"
)

# which source function encloses a given line (heuristic, for naming only)
SRC_FUNC_RE = re.compile(
    r"^[A-Za-z_][\w:<>,*&\s]*?[\s*&](\w+(?:::~\w+)?)\s*\([^;{]*\)\s*(?:const\s*)?\{?\s*$"
)


def enclosing_index(lines):
    """list of function names, parallel to lines (last definition seen)."""
    names, cur = [], None
    for line in lines:
        stripped = line.rstrip()
        if stripped.endswith(("{", ")")) or stripped.endswith(") const"):
            m = SRC_FUNC_RE.match(stripped)
            if m and not stripped.lstrip().startswith(("if", "for", "while", "switch", "return")):
                cur = m.group(1)
        names.append(cur)
    return names


def scan_xrefs(pe: PE, wanted_rvas, progress=None):
    """[(site_rva, literal_rva)] for every RIP-relative operand in .text."""
    text = pe.section(".text")
    if text is None:
        return []
    raw = pe.data[text["raw_ptr"] : text["raw_ptr"] + text["raw_size"]]
    base_rva = text["va"]
    hits = []
    for m in XREF_RE.finditer(raw):
        op_off = m.end() - 1  # the ModRM byte
        disp_off = op_off + 1  # disp32 follows mod=00/rm=101 directly
        if disp_off + 4 > len(raw):
            continue
        disp = struct.unpack_from("<i", raw, disp_off)[0]
        # bytes before the ModRM byte: optional REX + opcode
        length = (op_off - m.start()) + 1 + 4
        site_rva = base_rva + m.start()
        target = site_rva + length + disp
        if target in wanted_rvas:
            hits.append((site_rva, target))
    if progress:
        progress("scan .text: %d bytes, %d candidate operands -> %d resolved"
                 % (len(raw), len(XREF_RE.findall(raw)), len(hits)))
    return hits


CALL_RE = re.compile(rb"[\xe8\xe9](....)", re.DOTALL)


def scan_calls(pe: PE, targets):
    """Near call/jmp sites that land in `targets` -> [(site_rva, target_rva)].

    Only direct transfers (E8 call rel32 / E9 jmp rel32) are visible: calls made
    through a vtable, an IAT slot or a register cannot be followed statically,
    and the runtime resolver has the same limitation. Some hits are false
    positives from immediate bytes that happen to look like an opcode; the
    reported function table keeps them easy to spot.
    """
    wanted = set(targets)
    text = pe.section(".text")
    if text is None:
        return []
    raw = pe.data[text["raw_ptr"] : text["raw_ptr"] + text["raw_size"]]
    base_rva = text["va"]
    hits = []
    for m in CALL_RE.finditer(raw):
        disp = struct.unpack_from("<i", raw, m.start() + 1)[0]
        site_rva = base_rva + m.start()
        target = site_rva + 5 + disp
        if target in wanted:
            hits.append((site_rva, target))
    return hits


def string_function_hits(pe: PE, needle: bytes):
    """Resolve a raw byte string to the functions that reference it.

    Returns (strings, by_func) where `strings` maps the RVA of each matching
    string run to its bytes and `by_func` maps a containing function to the
    list of (site_rva, string_bytes) reference sites inside it.
    """
    strings: dict[int, bytes] = {}
    start = 0
    while True:
        off = pe.data.find(needle, start)
        if off < 0:
            break
        begin = off
        while begin > 0 and 0x20 <= pe.data[begin - 1] < 0x7F:
            begin -= 1
        end = off + len(needle)
        while end < len(pe.data) and 0x20 <= pe.data[end] < 0x7F:
            end += 1
        rva = pe.off_to_rva(begin)
        if rva is not None:
            strings[rva] = pe.data[begin:end]
        start = off + 1
    if not strings:
        return {}, {}
    by_func: dict[tuple, list] = {}
    for site_rva, target in scan_xrefs(pe, set(strings)):
        func = pe.function_of(site_rva)
        if func is None:
            continue
        by_func.setdefault(func, []).append((site_rva, strings.get(target)))
    return strings, by_func


# ------------------------------------------------------------------- pipeline


def image_string_index(pe: PE, wanted: set[bytes], suffix_merge=False, max_occurrences=4,
                       progress=None):
    """{literal: [rva, ...]} for literals contained in the mapped image.

    Matching is exact by default: the literals come from the source snapshot, so
    the compiled string is byte-identical and the code's ``lea`` points straight
    at it. ``suffix_merge`` additionally accepts the start of the enclosing
    printable run, which is what a linker does when it merges string suffixes;
    it is off by default because it also links a needle to every unrelated
    string that merely *contains* it.
    """
    data = pe.data
    out: dict[bytes, list[int]] = {}
    missing = 0
    for i, lit in enumerate(sorted(wanted, key=lambda x: -len(x))):
        rvas = []
        start = 0
        while len(rvas) < max_occurrences:
            off = data.find(lit, start)
            if off < 0:
                break
            candidates = {off}
            if suffix_merge:
                begin = off
                while begin > 0 and 0x20 <= data[begin - 1] < 0x7F and off - begin < 256:
                    begin -= 1
                candidates.add(begin)
            for candidate in candidates:
                rva = pe.off_to_rva(candidate)
                if rva is not None and rva not in rvas:
                    rvas.append(rva)
            start = off + 1
        if rvas:
            out[lit] = rvas
        else:
            missing += 1
        if progress and (i + 1) % 500 == 0:
            progress("  searched %d/%d literals (%d present)" % (i + 1, len(wanted), len(out)))
    return out


def resolve(pe: PE, literals: dict[bytes, list[tuple[str, int]]], verbose=True,
            suffix_merge=False):
    """Cluster reference sites by owning function."""
    present = image_string_index(pe, set(literals), suffix_merge=suffix_merge,
                                 progress=print if verbose else None)
    rva_to_literal = {rva: lit for lit, rvas in present.items() for rva in rvas}
    wanted = set(rva_to_literal)
    if verbose:
        print("  literals searched      : %d" % len(literals))
        print("  literals in the image  : %d" % len(present))
    hits = scan_xrefs(pe, wanted, progress=print if verbose else None)
    clusters: dict[int, dict] = {}
    for site_rva, target in hits:
        func = pe.function_of(site_rva)
        if func is None:
            continue
        key = func[0]
        c = clusters.setdefault(key, dict(rva=key, size=func[1] - func[0], literals={}))
        text = rva_to_literal.get(target)
        if text is None:
            continue
        c["literals"].setdefault(text, []).append(site_rva)
    return present, clusters


def score_clusters(pe, clusters, literals):
    """Attach a best-guess source function name to each cluster."""
    cache: dict[str, list] = {}

    def enclosing(rel, line):
        if rel not in cache:
            path = os.path.join(ROOT, rel)
            try:
                text = open(path, "r", encoding="utf-8", errors="replace").read()
            except OSError:
                cache[rel] = []
            else:
                cache[rel] = enclosing_index(text.splitlines())
        names = cache[rel]
        return names[line - 1] if 0 < line <= len(names) else None

    for c in clusters.values():
        votes: dict[str, list[str]] = {}
        for lit in c["literals"]:
            for rel, line in literals.get(lit, []):
                fn = enclosing(rel, line)
                if fn:
                    votes.setdefault(fn, []).append("%s:%d" % (rel, line))
        best, best_hits = None, []
        for fn, where in votes.items():
            if len(where) > len(best_hits):
                best, best_hits = fn, where
        c["guess"] = best
        c["score"] = len(best_hits)
        c["evidence"] = sorted(best_hits)
        c["total_literals"] = len(c["literals"])
    return clusters


def write_report(out_dir, pe, clusters, literals, present, extra=None):
    os.makedirs(out_dir, exist_ok=True)
    ordered = sorted(clusters.values(), key=lambda c: (-c["total_literals"], -c["score"], c["rva"]))
    doc = dict(
        exe=pe.path,
        image_base=pe.image_base,
        sections=[s["name"] for s in pe.sections],
        function_count=len(pe.func_begin),
        target_count=len(ordered),
        targets=[
            dict(
                rva=c["rva"],
                rva_hex="0x%x" % c["rva"],
                size=c["size"],
                literals=len(c["literals"]),
                unique_strings=sum(1 for lit in c["literals"] if len(present.get(lit, [])) == 1),
                guess=c.get("guess"),
                score=c.get("score", 0),
                evidence=c.get("evidence", []),
                strings=sorted(x.decode("ascii", "replace") for x in c["literals"]),
            )
            for c in ordered
        ],
    )
    if extra:
        doc["targets_named"] = extra
    with open(os.path.join(out_dir, "anchors.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)

    lines = ["# Anchored functions", "",
             "exe: `%s` (image base 0x%x, %d functions in .pdata)" % (pe.path, pe.image_base, len(pe.func_begin)),
             "",
             "| function | size | strings | best source guess | evidence |",
             "|---|---|---|---|---|",
             ]
    for c in ordered[:120]:
        lines.append("| `0x%x` | %d | %d | `%s` | %s |" % (
            c["rva"], c["size"], c["total_literals"], c.get("guess") or "?",
            "<br>".join(c.get("evidence", [])[:3]) or ""))
    lines.append("")
    lines.append("Full machine-readable table: `out/anchors.json`.")
    with open(os.path.join(out_dir, "anchors.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return doc


# ---------------------------------------------------------------------- modes


def cmd_mine(args):
    files = list(iter_sources(args.src))
    print("source snapshot : %s (%d files)" % (args.src, len(files)))
    literals = source_literals(files, args.min_len)
    print("literals        : %d" % len(literals))
    pe = PE(args.exe)
    n = pe.load_functions()
    print("exe             : %s (%.1f MB, %d .pdata functions)" % (pe.path, len(pe.data) / 1e6, n))
    present, clusters = resolve(pe, literals, suffix_merge=args.suffix_merge)
    score_clusters(pe, clusters, literals)
    doc = write_report(args.out, pe, clusters, literals, present)
    exact = sum(1 for lit, rvas in present.items() if len(rvas) == 1)
    print("clustered       : %d functions carry %d/%d literals (%d of them unique in the image)"
          % (len(clusters), len(present), len(literals), exact))
    for c in sorted(clusters.values(), key=lambda c: -c["total_literals"])[: args.top]:
        print("  0x%-9x %6d B  %2d strings  %s" % (
            c["rva"], c["size"], c["total_literals"],
            ("-> " + c["guess"]) if c.get("guess") else ""))
    print("report          : %s" % os.path.join(args.out, "anchors.md"))


def cmd_find(args):
    """Resolve arbitrary strings that exist in the *exe* (v1388-only features)."""
    pe = PE(args.exe)
    pe.load_functions()
    print("exe             : %s (%d .pdata functions)" % (pe.path, len(pe.func_begin)))
    for pattern in args.find:
        strings, by_func = string_function_hits(pe, pattern.encode("utf-8"))
        print('\n"%s": %d distinct string(s) in the image' % (pattern, len(strings)))
        if not strings:
            continue
        for func, sites in sorted(by_func.items()):
            texts = sorted({s.decode("ascii", "replace") for _r, s in sites if s})
            print("  function 0x%-9x size %-6d xrefs %-2d  <- %s"
                  % (func[0], func[1] - func[0], len(sites),
                     " | ".join(t[:70] for t in texts[:3])))
        if not by_func:
            print("  (no RIP-relative reference found; string may be data-only)")
    return 0


def cmd_callers(args):
    """Who calls this function?  Tokens are RVAs (0x...) or string anchors."""
    pe = PE(args.exe)
    pe.load_functions()
    print("exe             : %s (%d .pdata functions)" % (pe.path, len(pe.func_begin)))
    targets = []
    for token in args.callers:
        try:
            rva = int(token, 16) if token.lower().startswith("0x") else int(token)
        except ValueError:
            strings, by_func = string_function_hits(pe, token.encode("utf-8"))
            if not strings:
                print('\n"%s": absent from the image' % token)
                continue
            if not by_func:
                print('\n"%s": %d string(s) but no RIP-relative reference' % (token, len(strings)))
                continue
            print('\n"%s": %d string(s) -> %d candidate function(s)'
                  % (token, len(strings), len(by_func)))
            for func, sites in sorted(by_func.items(), key=lambda kv: -len(kv[1])):
                print("  candidate 0x%-9x size %-6d xrefs %d"
                      % (func[0], func[1] - func[0], len(sites)))
                targets.append(func[0])
            continue
        func = pe.function_of(rva)
        if func is None:
            print("\n0x%x: not inside any .pdata function" % rva)
            continue
        print("\n0x%x: inside function 0x%x (size %d)" % (rva, func[0], func[1] - func[0]))
        targets.append(func[0])
    if not targets:
        return 1

    hits = scan_calls(pe, set(targets))
    callers: dict[tuple, list] = {}
    for site_rva, target in hits:
        caller = pe.function_of(site_rva)
        callers.setdefault(caller, []).append((site_rva, target))

    print("\ndirect call/jmp sites into %d target(s): %d"
          % (len(set(targets)), len(hits)))
    for target in dict.fromkeys(targets):
        print("\ntarget 0x%x size %d" % (target, pe.function_size(target)))
        rows = [(c, [s for s in sites if s[1] == target])
                for c, sites in callers.items()]
        rows = [(c, s) for c, s in rows if s]
        if not rows:
            print("  (no direct caller: reached through a vtable, the IAT, or data)")
        for caller, sites in sorted(rows, key=lambda kv: -len(kv[1])):
            csize = (caller[1] - caller[0]) if caller else 0
            print("  caller 0x%-9x size %-6d %d site(s)  %s"
                  % (caller[0] if caller else 0, csize, len(sites),
                     " ".join("0x%x" % r for r, _t in sites[:4])))
    return 0


def cmd_pointers(args):
    """Where is this function referenced as data?  (vtable / callback slots)

    Direct `call` scanning cannot see indirect dispatch, but the function's
    absolute address still sits in a table somewhere. Printing the neighbouring
    qwords shows whether the target lives in a vtable or a callback array, which
    is exactly how the engine reaches its debug overlay.
    """
    pe = PE(args.exe)
    pe.load_functions()
    print("exe             : %s (%d .pdata functions)" % (pe.path, len(pe.func_begin)))
    for token in args.pointers:
        rva = int(token, 16) if token.lower().startswith("0x") else int(token)
        va = pe.image_base + rva
        print("\n0x%x (va 0x%x) size %d" % (rva, va, pe.function_size(rva)))
        needle = struct.pack("<Q", va)
        hits = []
        for s in pe.sections:
            raw = pe.data[s["raw_ptr"] : s["raw_ptr"] + s["raw_size"]]
            start = 0
            while True:
                off = raw.find(needle, start)
                if off < 0:
                    break
                hits.append((s, off))
                start = off + 1
        if not hits:
            print("  (no absolute pointer to this function in any section)")
            continue
        for s, off in hits:
            raw = pe.data[s["raw_ptr"] : s["raw_ptr"] + s["raw_size"]]
            print("  data slot 0x%x  section %s" % (s["va"] + off, s["name"]))
            for p in range(max(0, off - 32), min(len(raw) - 8, off + 40) + 1, 8):
                v = struct.unpack_from("<Q", raw, p)[0]
                mark = "  <== target" if p == off else ""
                if v >= pe.image_base and pe.function_size(v - pe.image_base):
                    fr = v - pe.image_base
                    label = "func 0x%-9x size %-6d" % (fr, pe.function_size(fr))
                elif v == 0:
                    label = "(null)"
                else:
                    label = "0x%x" % v
                print("      0x%-9x %s%s" % (s["va"] + p, label, mark))
    return 0


def cmd_spec(args):
    spec = json.load(open(args.spec, "r", encoding="utf-8"))
    pe = PE(args.exe)
    pe.load_functions()
    print("exe             : %s (%d .pdata functions)" % (pe.path, len(pe.func_begin)))
    resolved, failed = {}, []
    mismatched = []
    for name, entry in spec.get("targets", {}).items():
        anchors = entry["anchors"]
        lits = {a.encode("utf-8"): [(entry.get("source", "?"), 0)] for a in anchors}
        present, clusters = resolve(pe, lits, verbose=False, suffix_merge=args.suffix_merge)
        ordered = sorted(clusters.values(),
                         key=lambda c: (-len(c["literals"]), c["rva"]))
        if not present:
            failed.append((name, "no anchor found in the image"))
            continue
        if not ordered:
            failed.append((name, "anchor present but no reference found"))
            continue
        expect = entry.get("expect")
        if expect is not None:
            want = int(expect, 16) if isinstance(expect, str) else int(expect)
            match = [c for c in ordered if c["rva"] == want]
            if not match:
                mismatched.append((name, expect,
                                   [("0x%x" % c["rva"], c["size"]) for c in ordered[:4]]))
            ordered = match or ordered
        best = ordered[0]
        resolved[name] = dict(
            rva=best["rva"], rva_hex="0x%x" % best["rva"], size=best["size"],
            anchors_hit=sorted(x.decode() for x in best["literals"]),
            anchors_missing=sorted(
                x.decode() for x in (set(lits) - set(best["literals"]))
            ),
            candidates=len(ordered), source=entry.get("source"),
        )
    print(json.dumps(dict(resolved=resolved, failed=failed), ensure_ascii=False, indent=1))
    if mismatched:
        print("\nMISMATCHED expectations:")
        for name, expect, got in mismatched:
            print("  %-28s expected %s, candidates: %s"
                  % (name, expect, ", ".join("%s(size %d)" % g for g in got)))
    return 0 if not failed and not mismatched else 1


def cmd_selftest(args):
    """Ground truth we control: resolve literals inside our own host.exe."""
    exe = os.path.join(ROOT, "out", "host.exe")
    if not os.path.exists(exe):
        print("selftest: %s not built yet" % exe)
        return 2
    pe = PE(exe)
    pe.load_functions()
    print("exe            : %s (%d .pdata functions)" % (exe, len(pe.func_begin)))
    ok = True

    def check(anchors, suffix_merge, expect_ok=True):
        nonlocal ok
        literals = {a: [("tests/host/host.cpp", 0)] for a in anchors}
        present, _clusters = resolve(pe, literals, verbose=False, suffix_merge=suffix_merge)
        print("  mode: %s" % ("exact" if not suffix_merge else "suffix-merge"))
        for lit in anchors:
            rvas = present.get(lit)
            if not rvas:
                print("    MISSING  %s" % lit.decode())
                ok = False
                continue
            sites = [s for s, t in scan_xrefs(pe, set(rvas)) if t in rvas]
            funcs = {pe.function_of(s) for s in sites}
            funcs.discard(None)
            print("    %-58s rva 0x%-6x xrefs %d  function %s"
                  % (lit.decode(), rvas[0], len(sites),
                     ", ".join("0x%x+%d" % (f[0], f[1] - f[0]) for f in funcs) or "-"))
            if expect_ok and (not sites or len(funcs) != 1):
                ok = False

    # exact literals, as the compiler emits them
    check([b"host: winmm import test", b"RESULT=OK",
           b"host: FAIL aamod_core.dll was not loaded by the shim"], suffix_merge=False)
    # a needle that is only a *slice* of a compiled string must still resolve
    check([b"aamod core version"], suffix_merge=True)
    print("selftest: %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=os.path.join(ROOT, "out"))
    ap.add_argument("--spec", default=os.path.join(ROOT, "tools", "anchors.json"))
    ap.add_argument("--mine", action="store_true", help="harvest literals from the source snapshot")
    ap.add_argument("--find", nargs="+", metavar="TEXT",
                    help="locate strings that exist in the exe itself (v1388-only features)")
    ap.add_argument("--callers", nargs="+", metavar="RVA|TEXT",
                    help="list direct callers of a function (RVA or string anchor)")
    ap.add_argument("--pointers", nargs="+", metavar="RVA",
                    help="find vtable/callback slots holding this function's address")
    ap.add_argument("--selftest", action="store_true", help="resolve anchors in our own host.exe")
    ap.add_argument("--suffix-merge", action="store_true",
                    help="also accept the start of the enclosing string run")
    ap.add_argument("--min-len", type=int, default=6)
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args(argv)

    if args.selftest:
        return cmd_selftest(args)
    if args.callers:
        return cmd_callers(args)
    if args.pointers:
        return cmd_pointers(args)
    if args.find:
        return cmd_find(args)
    if args.mine:
        return cmd_mine(args)
    if args.spec and os.path.exists(args.spec):
        return cmd_spec(args)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
