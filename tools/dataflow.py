#!/usr/bin/env python3
"""Symbolic execution of a straight-line run of x86/x87 code: prints every memory store and the final
register / x87 stack contents as expressions, with the float roundings made explicit.

  python3 tools/dataflow.py data/msttsl/msttssyn.dll 63684c42 63684d0c   # [from, to)

Registers start as symbols (eax0, ...); memory reads are m32[expr] (or the value stored earlier in the
run, when the address expression is identical); f32(e) marks a store to a float (a rounding), f64(e) to
a double. x87 values are doubles (53-bit precision control word), so any expression without f32() is
exact double arithmetic in the order shown. Conditional jumps inside the range are listed and ignored.
"""
import os
import sys

import capstone
from capstone import x86 as cx

sys.path.insert(0, os.path.dirname(__file__))
from pe32 import Image

REGS32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]


def simp(e):
    return e


class St:
    def __init__(self):
        self.r = {k: k + "0" for k in REGS32}
        self.r["esp"] = "esp0"
        self.fst = []            # x87 stack, [0] = ST0
        self.mem = {}            # address expr -> (value expr, kind)
        self.out = []


def lin(expr_terms):
    """terms: dict sym -> coeff, const -> int; returns string"""
    parts = []
    const = expr_terms.pop("", 0)
    for s in sorted(expr_terms):
        c = expr_terms[s]
        if c == 0:
            continue
        parts.append(s if c == 1 else "%d*%s" % (c, s))
    if const or not parts:
        parts.append("%#x" % (const & 0xFFFFFFFF) if const >= 0x10000 else str(const))
    return " + ".join(parts)


class Lin:
    """linear combination of symbols (for address arithmetic)"""
    def __init__(self, d=None):
        self.d = dict(d or {})

    @staticmethod
    def sym(s):
        return Lin({s: 1})

    @staticmethod
    def const(c):
        return Lin({"": c})

    def add(self, o, k=1):
        d = dict(self.d)
        for s, c in o.d.items():
            d[s] = d.get(s, 0) + k * c
        return Lin({s: c for s, c in d.items() if c})

    def mul(self, k):
        return Lin({s: c * k for s, c in self.d.items()})

    def __str__(self):
        return lin(dict(self.d))

    def key(self):
        return str(self)


def main():
    img = Image(sys.argv[1])
    lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    R = {k: Lin.sym(k + "0") for k in REGS32}
    fst = []
    mem = {}

    def ea(op):
        m = op.mem
        a = Lin.const(m.disp)
        if m.base:
            a = a.add(R[md.reg_name(m.base)])
        if m.index:
            a = a.add(R[md.reg_name(m.index)], m.scale)
        return a

    def rd(op, size, kind):
        a = ea(op).key()
        if a in mem:
            v, k = mem[a]
            return v
        return "%s[%s]" % (kind, a)

    def fop(name, a, b):
        return "(%s %s %s)" % (a, name, b)

    va = lo
    while va < hi:
        i = next(md.disasm(img.read(va, 16), va, 1))
        m = i.mnemonic
        ops = i.operands
        va += i.size
        try:
            if m == "fld":
                if ops[0].type == cx.X86_OP_MEM:
                    kind = "m32f" if ops[0].size == 4 else "m64f"
                    fst.insert(0, rd(ops[0], ops[0].size, kind))
                else:
                    fst.insert(0, fst[int(i.op_str[3]) if "(" in i.op_str else 0])
            elif m in ("fild",):
                fst.insert(0, "(double)%s" % rd(ops[0], ops[0].size, "m%di" % (ops[0].size * 8)))
            elif m == "fldz":
                fst.insert(0, "0.0")
            elif m == "fld1":
                fst.insert(0, "1.0")
            elif m in ("fst", "fstp"):
                if ops[0].type == cx.X86_OP_MEM:
                    a = ea(ops[0]).key()
                    v = fst[0]
                    w = "f32" if ops[0].size == 4 else "f64"
                    val = "%s(%s)" % (w, v) if w == "f32" else v
                    mem[a] = (val, w)
                    print("  %08x  [%s] = %s" % (i.address, a, val))
                else:
                    k = int(i.op_str[3]) if "(" in i.op_str else 0
                    fst[k] = fst[0]
                if m == "fstp":
                    fst.pop(0)
            elif m in ("fistp", "fist"):
                a = ea(ops[0]).key()
                val = "fistp%d(%s)" % (ops[0].size * 8, fst[0])
                mem[a] = (val, "int")
                print("  %08x  [%s] = %s" % (i.address, a, val))
                if m == "fistp":
                    fst.pop(0)
            elif m == "fxch":
                k = int(i.op_str[3]) if "(" in i.op_str else 1
                fst[0], fst[k] = fst[k], fst[0]
            elif m in ("fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr", "faddp", "fsubp", "fsubrp", "fmulp", "fdivp", "fdivrp"):
                base = m.rstrip("p") if m.endswith("p") and m not in ("fsubp",) else m
                sym = {"fadd": "+", "fsub": "-", "fsubr": "-", "fmul": "*", "fdiv": "/", "fdivr": "/",
                       "faddp": "+", "fsubp": "-", "fsubrp": "-", "fmulp": "*", "fdivp": "/", "fdivrp": "/"}[m]
                rev = m in ("fsubr", "fdivr", "fsubrp", "fdivrp")
                if len(ops) == 1 and ops[0].type == cx.X86_OP_MEM:
                    b = rd(ops[0], ops[0].size, "m32f" if ops[0].size == 4 else "m64f")
                    fst[0] = fop(sym, b, fst[0]) if rev else fop(sym, fst[0], b)
                else:
                    # register forms: capstone prints "st(0), st(i)" or "st(i), st(0)" or "st(i)"
                    txt = i.op_str.replace(" ", "")
                    regs = [int(t[3]) if "(" in t else 0 for t in txt.split(",")] if txt else [1, 0]
                    if len(regs) == 1:
                        regs = [0, regs[0]] if not m.endswith("p") else [regs[0], 0]
                    d, s = regs
                    # Intel semantics: dst = dst op src; the reversed forms dst = src op dst
                    if m.endswith("p"):
                        # capstone's AT&T-ish naming for the popping forms: fsubp st(i) = st(i) - st0 (Intel fsubrp)...
                        # use Intel mnemonics from the opcode to avoid the well-known ambiguity
                        opb = i.bytes
                        sub = (opb[1] >> 3) & 7
                        # DE /r: 0 faddp, 1 fmulp, 4 fsubrp(st(i)=st0-st(i)), 5 fsubp(st(i)=st(i)-st0), 6 fdivrp, 7 fdivp
                        k = opb[1] & 7
                        a, b = fst[k], fst[0]
                        res = {0: fop("+", a, b), 1: fop("*", a, b), 4: fop("-", b, a), 5: fop("-", a, b),
                               6: fop("/", b, a), 7: fop("/", a, b)}[sub]
                        fst[k] = res
                        fst.pop(0)
                    else:
                        opb = i.bytes
                        sub = (opb[1] >> 3) & 7
                        k = opb[1] & 7
                        if opb[0] == 0xD8:    # st0 = st0 op st(i)
                            a, b = fst[0], fst[k]
                            res = {0: fop("+", a, b), 1: fop("*", a, b), 4: fop("-", a, b), 5: fop("-", b, a),
                                   6: fop("/", a, b), 7: fop("/", b, a)}[sub]
                            fst[0] = res
                        else:                 # DC: st(i) = st(i) op st0
                            a, b = fst[k], fst[0]
                            res = {0: fop("+", a, b), 1: fop("*", a, b), 4: fop("-", b, a), 5: fop("-", a, b),
                                   6: fop("/", b, a), 7: fop("/", a, b)}[sub]
                            fst[k] = res
            elif m == "fchs":
                fst[0] = "-(%s)" % fst[0]
            elif m == "fabs":
                fst[0] = "fabs(%s)" % fst[0]
            elif m in ("fcomp", "fcom", "fcompp", "fucomp", "fucompp", "ftst"):
                other = rd(ops[0], 4, "m32f") if ops and ops[0].type == cx.X86_OP_MEM else (fst[int(i.op_str[3])] if "(" in i.op_str else fst[1])
                print("  %08x  compare %s with %s" % (i.address, fst[0], other))
                if m in ("fcomp", "fucomp"):
                    fst.pop(0)
                elif m in ("fcompp", "fucompp"):
                    fst.pop(0); fst.pop(0)
            elif m == "mov" and ops[0].type == cx.X86_OP_REG and ops[0].size == 4:
                if ops[1].type == cx.X86_OP_REG:
                    R[i.reg_name(ops[0].reg)] = R[i.reg_name(ops[1].reg)]
                elif ops[1].type == cx.X86_OP_IMM:
                    R[i.reg_name(ops[0].reg)] = Lin.const(ops[1].imm)
                else:
                    a = ea(ops[1]).key()
                    if a in mem:
                        v = mem[a][0]
                        R[i.reg_name(ops[0].reg)] = Lin.sym("(%s)" % v) if not isinstance(v, Lin) else v
                    else:
                        R[i.reg_name(ops[0].reg)] = Lin.sym("m32[%s]" % a)
            elif m == "mov" and ops[0].type == cx.X86_OP_MEM and ops[0].size == 4:
                a = ea(ops[0]).key()
                v = R[i.reg_name(ops[1].reg)] if ops[1].type == cx.X86_OP_REG else Lin.const(ops[1].imm)
                mem[a] = (str(v), "raw")
                print("  %08x  [%s] = raw %s" % (i.address, a, v))
            elif m == "lea":
                R[i.reg_name(ops[0].reg)] = ea(ops[1])
            elif m in ("add", "sub") and ops[0].type == cx.X86_OP_REG:
                if ops[1].type == cx.X86_OP_REG:
                    o = R[i.reg_name(ops[1].reg)]
                elif ops[1].type == cx.X86_OP_IMM:
                    o = Lin.const(ops[1].imm)
                else:
                    a = ea(ops[1]).key()
                    o = Lin.sym("(%s)" % mem[a][0]) if a in mem else Lin.sym("m32[%s]" % a)
                r = i.reg_name(ops[0].reg)
                R[r] = R[r].add(o, 1 if m == "add" else -1)
            elif m in ("add", "sub") and ops[0].type == cx.X86_OP_MEM:
                a = ea(ops[0]).key()
                cur = Lin.sym("(%s)" % mem[a][0]) if a in mem else Lin.sym("m32[%s]" % a)
                o = R[i.reg_name(ops[1].reg)] if ops[1].type == cx.X86_OP_REG else Lin.const(ops[1].imm)
                v = cur.add(o, 1 if m == "add" else -1)
                mem[a] = (str(v), "raw")
            elif m == "shl" and ops[0].type == cx.X86_OP_REG and ops[1].type == cx.X86_OP_IMM:
                r = i.reg_name(ops[0].reg)
                R[r] = R[r].mul(1 << ops[1].imm)
            elif m in ("inc", "dec") and ops[0].type == cx.X86_OP_REG:
                r = i.reg_name(ops[0].reg)
                R[r] = R[r].add(Lin.const(1 if m == "inc" else -1))
            elif m == "push":
                R["esp"] = R["esp"].add(Lin.const(-4))
                v = R[i.reg_name(ops[0].reg)] if ops[0].type == cx.X86_OP_REG else Lin.const(ops[0].imm) if ops[0].type == cx.X86_OP_IMM else Lin.sym("m32[%s]" % ea(ops[0]).key())
                mem[R["esp"].key()] = (str(v), "raw")
            elif m == "pop":
                a = R["esp"].key()
                R[i.reg_name(ops[0].reg)] = Lin.sym("(%s)" % mem[a][0]) if a in mem else Lin.sym("m32[%s]" % a)
                R["esp"] = R["esp"].add(Lin.const(4))
            elif m.startswith("j") or m in ("cmp", "test", "fnstsw", "sahf", "nop"):
                if m.startswith("j"):
                    print("  %08x  (%s %s)" % (i.address, m, i.op_str))
            else:
                print("  %08x  ?? %s %s (not modelled; registers it writes become unknown)" % (i.address, m, i.op_str))
                if ops and ops[0].type == cx.X86_OP_REG:
                    R[i.reg_name(ops[0].reg)] = Lin.sym("?%x" % i.address)
        except (IndexError, KeyError) as ex:
            print("  %08x  !! %s %s: %r" % (i.address, m, i.op_str, ex))
    print("x87 stack at end (ST0 first):")
    for k, v in enumerate(fst):
        print("  st%d = %s" % (k, v))
    print("registers:")
    for r in REGS32:
        print("  %s = %s" % (r, R[r]))


if __name__ == "__main__":
    main()
