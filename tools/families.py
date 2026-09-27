#!/usr/bin/env python3
"""Near-duplicate functions: same sequence of instruction mnemonics (operands ignored). Catches the
template copies tools/dupes.py misses because their structure offsets or constants differ.

  python3 tools/families.py data/msttsl/msttssyn.dll work [min_size]
"""
import json
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image


def main():
    img = Image(sys.argv[1])
    F = json.load(open(os.path.join(sys.argv[2], "functions.json")))["functions"]
    cov = {}
    cp = os.path.join(sys.argv[2], "coverage.json")
    if os.path.exists(cp):
        cov = {r["addr"]: r for r in json.load(open(cp))["functions"]}
    minsz = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    groups = {}
    for f in F:
        if f["size"] < minsz:
            continue
        key = tuple(next(md.disasm(img.read(a, sz), a, 1)).mnemonic for a, sz in f["code"])
        groups.setdefault(key, []).append(f)
    out = [g for g in groups.values() if len(g) > 1]
    out.sort(key=lambda g: -g[0]["size"] * len(g))
    for g in out:
        print("%d x ~%d bytes: %s" % (len(g), g[0]["size"], " ".join(
            "%08x%s" % (f["addr"], "*" if cov.get(f["addr"], {}).get("hit") else "") for f in g)))


if __name__ == "__main__":
    main()
