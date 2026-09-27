#!/usr/bin/env python3
"""Merge the per-process coverage dumps of tools/cover.sh and map them onto the function list.

  python3 tools/covreport.py data/msttsl/msttssyn.dll work

Reads work/functions.json (tools/funcs.py) and work/cover/*.exec|*.calls. Writes work/coverage.json and
work/coverage.txt: per function, instructions executed / total, dynamic executions, callers seen.
Also reports call targets the static analysis did not know as function entries.
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image

LOAD = 0x01000000


def load_counts(pattern, base):
    tot = {}
    for fn in glob.glob(pattern):
        for line in open(fn):
            o, n = line.split()
            a = int(o, 16) + base
            tot[a] = tot.get(a, 0) + int(n)
    return tot


def main():
    img = Image(sys.argv[1])
    work = sys.argv[2]
    F = json.load(open(os.path.join(work, "functions.json")))["functions"]
    ex = load_counts(os.path.join(work, "cover", "*.exec"), img.base)
    calls = load_counts(os.path.join(work, "cover", "*.calls"), img.base)
    starts = {f["addr"] for f in F}
    unknown = sorted(a for a in calls if img.in_text(a) and a not in starts)
    rows = []
    for f in F:
        insns = [a for a, s in f["code"]]
        hit = [a for a in insns if a in ex]
        dyn = sum(ex.get(a, 0) for a in insns)
        rows.append(dict(addr=f["addr"], size=f["size"], insns=len(insns), hit=len(hit), dyn=dyn,
                         calls=calls.get(f["addr"], 0),
                         hitbytes=sum(s for a, s in f["code"] if a in ex)))
    # dynamic call graph
    endmap = {}
    for f in F:
        for a, sz in f["code"]:
            endmap[a + sz] = f["addr"]
    edges = {}
    for fn in glob.glob(os.path.join(work, "cover", "*.edges")):
        for line in open(fn):
            r, t, n = line.split()
            r = int(r, 16) + img.base
            t = int(t, 16) + img.base
            caller = endmap.get(r)
            if caller is None:
                continue
            edges[(caller, t)] = edges.get((caller, t), 0) + int(n)
    callers, callees = {}, {}
    for (a, b), n in edges.items():
        callers.setdefault(b, {})[a] = n
        callees.setdefault(a, {})[b] = n
    reached = [r for r in rows if r["hit"]]
    with open(os.path.join(work, "callgraph.txt"), "w") as fo:
        for r in sorted(reached, key=lambda r: r["addr"]):
            a = r["addr"]
            fo.write("%08x dyn %d\n  from: %s\n  to:   %s\n" % (
                a, r["dyn"], " ".join("%08x" % x for x in sorted(callers.get(a, {}))) or "(host)",
                " ".join("%08x" % x for x in sorted(callees.get(a, {})))))
    roots = [r["addr"] for r in reached if not callers.get(r["addr"])]
    print("entered from the host only (COM methods, thread/window procs): %d" % len(roots))
    json.dump(dict(edges=[[a, b, n] for (a, b), n in edges.items()], roots=roots), open(os.path.join(work, "callgraph.json"), "w"))
    tot_dyn = sum(r["dyn"] for r in rows)
    json.dump(dict(functions=rows, unknown_call_targets=unknown), open(os.path.join(work, "coverage.json"), "w"))
    with open(os.path.join(work, "coverage.txt"), "w") as fo:
        for r in sorted(rows, key=lambda r: -r["dyn"]):
            if not r["hit"]:
                continue
            fo.write("%08x size %5d insns %4d/%4d dyn %12d (%5.2f%%) calls %d\n" % (
                r["addr"], r["size"], r["hit"], r["insns"], r["dyn"], 100.0 * r["dyn"] / max(tot_dyn, 1), r["calls"]))
    print("functions reached: %d of %d; bytes of reached functions %d; instruction bytes executed %d; "
          "dynamic instructions %d" % (len(reached), len(rows), sum(r["size"] for r in reached),
                                       sum(r["hitbytes"] for r in rows), tot_dyn))
    print("call targets not in the static function list: %d %s" % (len(unknown), " ".join("%08x" % a for a in unknown[:30])))


if __name__ == "__main__":
    main()
