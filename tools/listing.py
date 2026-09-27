#!/usr/bin/env python3
"""Annotated disassembly of functions from work/functions.json.

  python3 tools/listing.py data/msttsl/msttssyn.dll work 636712d5 [more addresses]
"""
import json
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image


def main():
    img = Image(sys.argv[1])
    F = {f["addr"]: f for f in json.load(open(os.path.join(sys.argv[2], "functions.json")))["functions"]}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    names = {}
    nf = os.path.join(sys.argv[2], "..", "docs", "names.txt")
    if os.path.exists(nf):
        for line in open(nf):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                names[int(p[0], 16)] = p[1]
    for a in sys.argv[3:]:
        f = F[int(a, 16)]
        print("; ---- %08x %s size %d ret %s" % (f["addr"], names.get(f["addr"], ""), f["size"], f["retpop"]))
        targets = set()
        for va, sz in f["code"]:
            i = next(md.disasm(img.read(va, sz), va, 1))
            if i.mnemonic.startswith("j"):
                try:
                    targets.add(int(i.op_str, 16))
                except ValueError:
                    pass
        prev = None
        for va, sz in f["code"]:
            if prev is not None and va != prev:
                print("        ; ...")
            i = next(md.disasm(img.read(va, sz), va, 1))
            note = ""
            for k in range(va, va + sz):
                if k in img.iat and False:
                    pass
            ops = i.op_str
            for word in ops.replace("[", " ").replace("]", " ").replace(",", " ").split():
                if word.startswith("0x"):
                    v = int(word, 16)
                    if v in img.iat:
                        note = img.iat[v]
                    elif v in names:
                        note = names[v]
                    elif img.section_of(v) in (".rdata", ".data"):
                        note = "%s" % img.section_of(v)
            lab = "L:" if va in targets else "  "
            print("%s %08x  %-6s %-40s %s" % (lab, va, i.mnemonic, ops, ("; " + note) if note else ""))
            prev = va + sz


if __name__ == "__main__":
    main()
