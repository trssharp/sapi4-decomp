#!/usr/bin/env python3
"""Recursive-descent function discovery for msttssyn.dll.

  python3 tools/funcs.py data/msttsl/msttssyn.dll work/

Seeds: the entry point, the exports, every base relocation (in any section) that points into .text
and is not a jump-table entry, and every rel32 call target. Each function is walked block by block;
a jmp onto another function's entry is a tail call. MSVC jump tables (jmp [reg*4+table] with the table
in .text) are followed. Iterates until the set of entries is stable.

Writes work/functions.json and work/functions.txt (address, size, instructions, callees, imports,
x87 instruction count, ret pop count, flags). Addresses are the DLL's own (base 0x63670000).
"""
import json
import os
import sys

import capstone
from capstone import x86 as cx

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image

NORETURN = {"MSVCRT!exit", "MSVCRT!longjmp", "MSVCRT!_CxxThrowException", "MSVCRT!_purecall"}


def main():
    img = Image(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else "work"
    os.makedirs(out, exist_ok=True)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    cache = {}

    def insn(va):
        if va in cache:
            return cache[va]
        code = img.read(va, 16)
        i = next(md.disasm(code, va, 1), None)
        cache[va] = i
        return i

    # jump tables: find them during the walk; their entry slots are excluded from seeds
    jt_slots = set()
    data_refs = set()   # .text addresses used as memory operands (jump tables, byte index tables)
    entries = set()
    entries.add(img.base + img.pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    entries |= set(img.exports)
    reloc_targets = {}
    for r in img.relocs:
        t = img.u32(r)
        if img.in_text(t):
            reloc_targets[r] = t

    def walk(start, known):
        """-> dict describing the function at start (stops at other known entries: tail calls)"""
        seen = {}
        todo = [start]
        callees, imports, tails, jts, indirect = set(), set(), set(), [], 0
        x87 = 0
        retpop = None
        while todo:
            va = todo.pop()
            while True:
                if va in seen:
                    break
                if va != start and va in known:
                    tails.add(va)
                    break
                if not img.in_text(va):
                    break
                i = insn(va)
                if i is None:
                    seen[va] = 1   # undecodable
                    break
                seen[va] = i.size
                m = i.mnemonic
                if i.bytes[0] in (0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF) or m.startswith("f"):
                    x87 += 1
                nxt = va + i.size
                ops = i.operands
                if m in ("ret", "retn"):
                    retpop = ops[0].imm if ops else 0
                    break
                if m == "int3" or m == "hlt":
                    break
                if m == "call":
                    op = ops[0]
                    if op.type == cx.X86_OP_IMM:
                        callees.add(op.imm)
                    elif op.type == cx.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and op.mem.disp in img.iat:
                        imp = img.iat[op.mem.disp]
                        imports.add(imp)
                        if imp in NORETURN:
                            break
                    else:
                        indirect += 1
                    va = nxt
                    continue
                if m == "jmp":
                    op = ops[0]
                    if op.type == cx.X86_OP_IMM:
                        t = op.imm
                        if t in known and t != start:
                            tails.add(t)
                            break
                        va = t
                        continue
                    if op.type == cx.X86_OP_MEM:
                        if op.mem.base == 0 and op.mem.index == 0 and op.mem.disp in img.iat:
                            imports.add(img.iat[op.mem.disp])
                            break
                        if img.in_text(op.mem.disp & 0xFFFFFFFF):
                            data_refs.add(op.mem.disp & 0xFFFFFFFF)
                        if op.mem.index != 0 and op.mem.scale == 4 and img.in_text(op.mem.disp & 0xFFFFFFFF):
                            tbl = op.mem.disp & 0xFFFFFFFF
                            k = 0
                            targets = []
                            while (tbl + 4 * k) in img.relocs and img.in_text(img.u32(tbl + 4 * k)):
                                if k and (tbl + 4 * k) in seen:
                                    break
                                jt_slots.add(tbl + 4 * k)
                                targets.append(img.u32(tbl + 4 * k))
                                k += 1
                            for s in range(tbl, tbl + 4 * k):
                                seen.setdefault(s, 0)   # data inside the function
                            jts.append((tbl, k))
                            todo.extend(targets)
                            break
                    indirect += 1
                    break
                if m.startswith("j") or m.startswith("loop") or m == "jecxz":
                    op = ops[0]
                    if op.type == cx.X86_OP_IMM:
                        todo.append(op.imm)
                    va = nxt
                    continue
                for op in ops:
                    if op.type == cx.X86_OP_MEM and img.in_text(op.mem.disp & 0xFFFFFFFF):
                        data_refs.add(op.mem.disp & 0xFFFFFFFF)
                # push/mov of a code address: a callback (thread proc, window proc, ...)
                for op in ops:
                    if op.type == cx.X86_OP_IMM and img.in_text(op.imm & 0xFFFFFFFF):
                        for r in range(va, nxt):
                            if r in img.relocs and img.u32(r) == op.imm & 0xFFFFFFFF:
                                callees.add(op.imm & 0xFFFFFFFF)
                va = nxt
        code = {a: s for a, s in seen.items() if s}
        return dict(code=code, callees=callees, imports=imports, tails=tails, jts=jts, x87=x87,
                    retpop=retpop, indirect=indirect)

    def solve():
        funcs = {}
        prev_known = None
        for it in range(40):
            known = set(entries)
            for r, t in reloc_targets.items():
                if r not in jt_slots and t not in data_refs:
                    known.add(t)
            new = {}
            for f in sorted(known):
                new[f] = walk(f, known)
            more = set()
            for f, d in new.items():
                more |= {c for c in d["callees"] if img.in_text(c)}
            added = more - known
            entries.update(more)
            funcs = new
            if not added and known == prev_known:
                break
            prev_known = known
        return funcs

    def find_gaps(funcs):
        covered = set()
        for d in funcs.values():
            for a, s in d["code"].items():
                covered.update(range(a, a + s))
        for s in jt_slots:
            covered.update(range(s, s + 4))
        gaps = []
        a = img.text_lo
        while a < img.text_hi:
            if a in covered:
                a += 1
                continue
            b = a
            while b < img.text_hi and b not in covered:
                b += 1
            blob = img.read(a, b - a)
            if any(x not in (0x90, 0xCC, 0x00) for x in blob):
                gaps.append((a, b - a))
            a = b
        return covered, gaps

    # functions nothing references directly (reached only through computed addresses, or dead code):
    # seed each non-padding gap that does not start on a data table and decodes cleanly
    gapseeds = set()
    for rnd in range(10):
        funcs = solve()
        covered, gaps = find_gaps(funcs)
        seeds = set()
        for a, n in gaps:
            while n and img.read(a, 1)[0] in (0x90, 0xCC):
                a += 1
                n -= 1
            if not n or a in data_refs or a in gapseeds:
                continue
            ok = True
            v = a
            for _ in range(4):
                i = insn(v)
                if i is None:
                    ok = False
                    break
                v += i.size
            if ok:
                seeds.add(a)
        if not seeds:
            break
        gapseeds |= seeds
        entries.update(seeds)
    covered = set(a for a in covered if a not in jt_slots)
    # a known entry that another function's walk also reached as code (shared tail / jump target)
    inside = {}
    for f, d in funcs.items():
        for a in d["code"]:
            if a != f:
                inside.setdefault(a, f)
    body_owner = {f: inside[f] for f in funcs if f in inside and f not in img.exports}

    rows = []
    for f in sorted(funcs):
        d = funcs[f]
        size = sum(d["code"].values())
        hi = max(a + s for a, s in d["code"].items()) if d["code"] else f
        rows.append(dict(addr=f, size=size, end=hi, insns=len(d["code"]), callees=sorted(d["callees"]),
                         tails=sorted(d["tails"]), imports=sorted(d["imports"]), x87=d["x87"],
                         retpop=d["retpop"], indirect=d["indirect"], jumptables=d["jts"],
                         name=img.exports.get(f), shared_with=body_owner.get(f), unreferenced=f in gapseeds))
    json.dump(dict(functions=rows, gaps=gaps, jt_slots=sorted(jt_slots)), open(os.path.join(out, "functions.json"), "w"))
    with open(os.path.join(out, "functions.txt"), "w") as fo:
        for r in rows:
            flags = []
            if not r["callees"] and not r["tails"] and not r["indirect"]:
                flags.append("leaf" if not r["imports"] else "leaf+imp")
            if r["x87"]:
                flags.append("x87=%d" % r["x87"])
            if r["jumptables"]:
                flags.append("jt=%d" % len(r["jumptables"]))
            if r["unreferenced"]:
                flags.append("unref")
            if r["shared_with"]:
                flags.append("inside=%08x" % r["shared_with"])
            fo.write("%08x %6d %5d ret%-4s %-28s calls=%s imp=%s\n" % (
                r["addr"], r["size"], r["insns"], r["retpop"] if r["retpop"] is not None else "-",
                " ".join(flags), ",".join("%08x" % c for c in r["callees"]), ",".join(i.split("!")[1] for i in r["imports"])))
    tot = len(covered)
    print("unreferenced (gap-seeded) functions: %d" % len(gapseeds))
    print("functions: %d, code bytes in functions: %d of .text %d; uncovered non-padding gaps: %d (%d bytes)" % (
        len(rows), tot, img.text_hi - img.text_lo, len(gaps), sum(g[1] for g in gaps)))
    print("leaf functions (no calls at all): %d; jump tables: %d" % (
        sum(1 for r in rows if not r["callees"] and not r["tails"] and not r["indirect"] and not r["imports"]),
        sum(len(r["jumptables"]) for r in rows)))


if __name__ == "__main__":
    main()
