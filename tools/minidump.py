#!/usr/bin/env python3
"""Minimal minidump reader: exception, modules, and a raw stack scan.

Reads the streams we care about out of a MINIDUMP_HEADER container, resolves
addresses against the module list, and prints the crashing thread's stack as
"module+0xRVA" candidates (a poor man's stack trace -- no PDBs are involved).

    python tools/minidump.py <file.dmp> [--frames 40]

No dependencies; the layout used here is documented in
MSDN: MinidumpFormat / MINIDUMP_* structures.
"""

import struct
import sys

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_EXCEPTION = 6

# x64 CONTEXT offsets (CONTEXT_AMD64)
CTX_RSP = 0x98
CTX_RIP = 0xF8

EXC = {
    0xC0000005: "STATUS_ACCESS_VIOLATION",
    0xC000001D: "STATUS_ILLEGAL_INSTRUCTION",
    0xC0000094: "STATUS_INTEGER_DIVIDE_BY_ZERO",
    0xC00000FD: "STATUS_STACK_OVERFLOW",
    0xC0000409: "STATUS_STACK_BUFFER_OVERRUN (__fastfail)",
    0x80000003: "STATUS_BREAKPOINT",
    0xC0000135: "STATUS_DLL_NOT_FOUND",
    0xC0000142: "STATUS_DLL_INIT_FAILED",
    0xE06D7363: "C++ exception",
}


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


class Dump:
    def __init__(self, data):
        if data[:4] != b"MDMP":
            raise SystemExit("not a minidump (missing MDMP signature)")
        self.d = data
        self.streams = {}
        count = u32(data, 8)
        rva = u32(data, 12)
        for i in range(count):
            kind, _size, r = struct.unpack_from("<III", data, rva + i * 12)
            self.streams.setdefault(kind, []).append(r)

    def minidump_string(self, rva):
        length = u32(self.d, rva)
        raw = self.d[rva + 4:rva + 4 + length]
        return raw.decode("utf-16-le", "replace")

    def modules(self):
        out = []
        if STREAM_MODULE_LIST not in self.streams:
            return out
        base = self.streams[STREAM_MODULE_LIST][0]
        count = u32(self.d, base)
        for i in range(count):
            off = base + 4 + i * 108
            out.append({
                "base": u64(self.d, off + 0),
                "size": u32(self.d, off + 8),
                "name": self.minidump_string(u32(self.d, off + 20)),
            })
        out.sort(key=lambda m: m["base"])
        return out

    def threads(self):
        out = []
        if STREAM_THREAD_LIST not in self.streams:
            return out
        base = self.streams[STREAM_THREAD_LIST][0]
        count = u32(self.d, base)
        for i in range(count):
            off = base + 4 + i * 48
            out.append({
                "id": u32(self.d, off + 0),
                "stack_start": u64(self.d, off + 24),
                "stack_size": u32(self.d, off + 32),
                "stack_rva": u32(self.d, off + 36),
                "ctx_size": u32(self.d, off + 40),
                "ctx_rva": u32(self.d, off + 44),
            })
        return out

    def exception(self):
        if STREAM_EXCEPTION not in self.streams:
            return None
        base = self.streams[STREAM_EXCEPTION][0]
        ctx_size = u32(self.d, base + 160)
        ctx_rva = u32(self.d, base + 164)
        return {
            "thread_id": u32(self.d, base + 0),
            "code": u32(self.d, base + 8),
            "address": u64(self.d, base + 24),
            "params": [u64(self.d, base + 40 + 8 * i) for i in range(u32(self.d, base + 32))],
            "ctx_size": ctx_size,
            "ctx_rva": ctx_rva,
        }

    def context(self, ctx_rva):
        return {"rsp": u64(self.d, ctx_rva + CTX_RSP),
                "rip": u64(self.d, ctx_rva + CTX_RIP)}


def resolve(modules, addr):
    for m in modules:
        if m["base"] <= addr < m["base"] + m["size"]:
            return "%s+0x%x" % (m["name"].rsplit("\\", 1)[-1], addr - m["base"]), m
    return None, None


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    frames = 40
    if "--frames" in argv:
        frames = int(argv[argv.index("--frames") + 1])
    data = open(argv[1], "rb").read()
    dump = Dump(data)
    modules = dump.modules()

    exc = dump.exception()
    print("modules loaded: %d" % len(modules))
    for m in modules:
        if any(k in m["name"].lower() for k in
               ("astral", "aamod", "d3d11", "dxgi", "d3dcompiler", "glesv2", "egl")):
            print("  %-28s base 0x%016x  size 0x%x" % (
                m["name"].rsplit("\\", 1)[-1], m["base"], m["size"]))

    if not exc:
        print("no exception stream")
        return 0

    name, _m = resolve(modules, exc["address"])
    print("\nexception: 0x%08x %s" % (exc["code"], EXC.get(exc["code"], "?")))
    print("  thread   : %d" % exc["thread_id"])
    print("  address  : 0x%016x  ->  %s" % (exc["address"], name or "<unmapped>"))
    if exc["params"]:
        print("  params   : %s" % ", ".join("0x%x" % p for p in exc["params"]))
        if exc["code"] == 0xC0000005 and len(exc["params"]) > 1:
            print("  access   : %s of 0x%016x  ->  %s" % (
                "write" if exc["params"][0] else "read", exc["params"][1],
                resolve(modules, exc["params"][1])[0] or "<unmapped>"))

    ctx = dump.context(exc["ctx_rva"])
    print("  rip      : 0x%016x  ->  %s" % (ctx["rip"], resolve(modules, ctx["rip"])[0] or "<unmapped>"))
    print("  rsp      : 0x%016x" % ctx["rsp"])

    # stack scan: walk the crashing thread's stack memory and print the qwords
    # that land inside a module image (return-address candidates).
    for t in dump.threads():
        if t["id"] != exc["thread_id"]:
            continue
        start, size, rva = t["stack_start"], t["stack_size"], t["stack_rva"]
        if not size:
            break
        base = ctx["rsp"]
        offset = base - start
        if offset < 0 or offset > size:
            print("\n  (rsp outside the recorded stack range)")
            break
        blob = data[rva + offset:rva + size]
        print("\n  stack candidates (module+0xRVA), newest first:")
        shown = 0
        for i in range(0, len(blob) - 8, 8):
            value = struct.unpack_from("<Q", blob, i)[0]
            text, m = resolve(modules, value)
            if not text:
                continue
            print("    [rsp+0x%04x] 0x%016x  %s" % (offset + i, value, text))
            shown += 1
            if shown >= frames:
                break
        break
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
