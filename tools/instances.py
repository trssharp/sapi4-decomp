#!/usr/bin/env python3
"""For a group of same-code functions (tools/dupes.py), the relocated constants of each member in
code order: the per-instance globals a shared C implementation needs.

  python3 tools/instances.py data/msttsl/msttssyn.dll work ADDR [ADDR...]
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image


def consts(img, f):
    out = []
    for a, sz in f["code"]:
        for k in range(sz - 3):
            if (a + k) in img.relocs:
                out.append(img.u32(a + k))
    return out


def main():
    img = Image(sys.argv[1])
    F = {f["addr"]: f for f in json.load(open(os.path.join(sys.argv[2], "functions.json")))["functions"]}
    for a in sys.argv[3:]:
        print("%s: %s" % (a, " ".join("%08x" % v for v in consts(img, F[int(a, 16)]))))


if __name__ == "__main__":
    main()
