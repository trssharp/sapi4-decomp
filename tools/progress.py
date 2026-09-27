#!/usr/bin/env python3
"""Decompilation progress, measured: hooked functions (harness/hooklist.h) against the functions the
golden corpus reaches (work/coverage.json from tools/cover.sh) and the whole static function list.

  python3 tools/progress.py work
"""
import json
import os
import re
import sys


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else "work"
    root = os.path.join(os.path.dirname(__file__), "..")
    hooks = {}
    for line in open(os.path.join(root, "harness", "hooklist.h")):
        m = re.match(r"HOOKY?\(0x([0-9a-f]+),\s*(\w+)", line)
        if m:
            hooks[int(m.group(1), 16)] = m.group(2)
    F = json.load(open(os.path.join(work, "functions.json")))["functions"]
    C = {r["addr"]: r for r in json.load(open(os.path.join(work, "coverage.json")))["functions"]}
    stubs = {f["addr"] for f in F if f["insns"] == 1 and f["imports"]}   # jmp [IAT] import stubs
    allf = [f for f in F if f["addr"] not in stubs]
    reached = [f for f in allf if C.get(f["addr"], {}).get("hit")]
    tot_dyn = sum(C[f["addr"]]["dyn"] for f in allf if f["addr"] in C)
    done = [f for f in allf if f["addr"] in hooks]
    done_r = [f for f in reached if f["addr"] in hooks]
    dyn_done = sum(C[f["addr"]]["dyn"] for f in done if f["addr"] in C)
    print("static functions (excluding %d import stubs): %d, %d bytes" % (len(stubs), len(allf), sum(f["size"] for f in allf)))
    print("reached by the corpus: %d functions, %d bytes" % (len(reached), sum(f["size"] for f in reached)))
    print("decompiled + verified: %d functions, %d bytes" % (len(done), sum(f["size"] for f in done)))
    print("  of the reached set: %d/%d functions (%.1f%%), %d/%d bytes (%.1f%%)" % (
        len(done_r), len(reached), 100.0 * len(done_r) / len(reached),
        sum(f["size"] for f in done_r), sum(f["size"] for f in reached),
        100.0 * sum(f["size"] for f in done_r) / sum(f["size"] for f in reached)))
    print("  share of all guest instructions the corpus executes: %.1f%%" % (100.0 * dyn_done / tot_dyn))
    unreached_done = [hooks[f["addr"]] for f in done if not C.get(f["addr"], {}).get("hit")]
    if unreached_done:
        print("  decompiled but never reached by the corpus (unit-tested only): %s" % ", ".join(unreached_done))


if __name__ == "__main__":
    main()
