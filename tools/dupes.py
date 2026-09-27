#!/usr/bin/env python3
"""Groups of functions with the same code (template instantiations, copies): identical bytes once every
relocated address, rel32 call/jump target and data displacement is masked. One decompilation per
group can then serve every member (differing only in the masked constants).

  python3 tools/dupes.py data/msttsl/msttssyn.dll work [min_size]
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
    minsz = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    groups = {}
    for f in F:
        if f["size"] < minsz:
            continue
        key = []
        for a, sz in f["code"]:
            b = bytearray(img.read(a, sz))
            for k in range(sz - 3):
                if (a + k) in img.relocs:
                    b[k:k + 4] = b"\0\0\0\0"
            if b[0] in (0xE8, 0xE9) and sz == 5:
                b[1:5] = b"\0\0\0\0"
            if b[0] == 0x0F and 0x80 <= b[1] <= 0x8F and sz == 6:
                pass    # intra-function jumps are position independent in shape
            key.append(bytes(b))
        groups.setdefault(b"|".join(key), []).append(f["addr"])
    out = [g for g in groups.values() if len(g) > 1]
    out.sort(key=lambda g: -len(g))
    for g in out:
        f = next(x for x in F if x["addr"] == g[0])
        print("%d x %d bytes: %s" % (len(g), f["size"], " ".join("%08x" % a for a in g)))


if __name__ == "__main__":
    main()
