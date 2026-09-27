#!/usr/bin/env python3
"""Which callers use EAX (or ST0 / EDX) after calling a function? MSVC callers sometimes read the value a
"void" function happens to leave in EAX, so a decompiled version must reproduce it. For every direct call
site of each address given, looks at the instructions after the call (same basic block, up to the next
write) and reports reads of EAX/AX/AL/AH, EDX, or x87 use of ST0.

  python3 tools/eaxuse.py data/msttsl/msttssyn.dll work 63675c05 [...]
"""
import json
import os
import sys

import capstone
from capstone import x86 as cx

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image

EAXS = {"eax", "ax", "al", "ah"}
EDXS = {"edx", "dx", "dl", "dh"}


def main():
    img = Image(sys.argv[1])
    F = json.load(open(os.path.join(sys.argv[2], "functions.json")))["functions"]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    for t in sys.argv[3:]:
        target = int(t, 16)
        sites = []
        for f in F:
            for a, sz in f["code"]:
                if sz == 5 and img.read(a, 1)[0] == 0xE8:
                    i = next(md.disasm(img.read(a, 5), a, 1))
                    if i.operands[0].imm == target:
                        sites.append((f["addr"], a + 5))
        print("%08x: %d call sites" % (target, len(sites)))
        for fa, ret in sites:
            va = ret
            use = []
            live = {"eax": True, "edx": True, "st0": True}
            for _ in range(12):
                i = next(md.disasm(img.read(va, 16), va, 1), None)
                if i is None:
                    break
                rr, ww = i.regs_access()
                reads = {i.reg_name(r) for r in rr}
                writes = {i.reg_name(r) for r in ww}
                if live["eax"] and reads & EAXS:
                    use.append("reads EAX at %08x: %s %s" % (i.address, i.mnemonic, i.op_str))
                if live["edx"] and reads & EDXS:
                    use.append("reads EDX at %08x: %s %s" % (i.address, i.mnemonic, i.op_str))
                if i.mnemonic in ("fld", "fild", "fldz", "fld1"):
                    live["st0"] = False   # a new value is pushed first (an ST0 return would be popped later, not read)
                if live["st0"] and i.mnemonic.startswith("f") and i.mnemonic != "fnstsw":
                    use.append("x87 %s %s at %08x (ST0 returned?)" % (i.mnemonic, i.op_str, i.address))
                if i.mnemonic == "fnstsw":
                    live["eax"] = False
                if "eax" in writes and not (reads & EAXS):
                    live["eax"] = False
                if "edx" in writes and not (reads & EDXS):
                    live["edx"] = False
                if i.mnemonic in ("ret", "jmp", "call") or i.mnemonic.startswith("j"):
                    if live["eax"] and i.mnemonic == "ret":
                        use.append("returns with it still in EAX (passes it up) at %08x" % i.address)
                    break
                va += i.size
            print("  from %08x (ret %08x): %s" % (fa, ret, "; ".join(use) if use else "-"))


if __name__ == "__main__":
    main()
