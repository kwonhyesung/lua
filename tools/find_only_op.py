#!/usr/bin/env python3
# "이 파일에서 모르는 opcode가 딱 target_op 하나뿐" 인 파일들을 찾는다(문맥 추론용).
import sys
import os
import struct

sys.path.insert(0, os.path.dirname(__file__))
from rewrite_opcodes import OPMAP, read, Ctx

DUMP_DIR = r"C:\Users\kwon\Desktop\luahook\out\dump"


def skip_string(inp, ctx):
    b0 = read(inp, 1)[0]
    if b0 == 0:
        return
    if b0 == 0xFF:
        sz = int.from_bytes(read(inp, ctx.sizet_size), "little")
        read(inp, sz - 1)
    else:
        read(inp, b0 - 1)


def scan_code(inp, ctx, unknown_ops, count_target, target_op):
    n = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    hits = 0
    for _ in range(n):
        val = struct.unpack("<I", read(inp, 4))[0]
        op = val & 0x3F
        if op not in OPMAP:
            unknown_ops.add(op)
        if op == target_op:
            hits += 1
    return n, hits


def skip_constants(inp, ctx):
    n = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n):
        t = read(inp, 1)[0]
        if t == 0:
            pass
        elif t == 1:
            read(inp, 1)
        elif t == 3:
            read(inp, ctx.number_size)
        elif t == 19:
            read(inp, ctx.integer_size)
        elif t in (4, 20):
            skip_string(inp, ctx)
        else:
            raise ValueError(f"bad tag {t}")


def skip_upvalues(inp, ctx):
    n = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    read(inp, n * 2)


def skip_debug(inp, ctx):
    n_line = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    read(inp, n_line * ctx.int_size)
    n_locals = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n_locals):
        skip_string(inp, ctx)
        read(inp, ctx.int_size * 2)
    n_upval_names = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n_upval_names):
        skip_string(inp, ctx)


def scan_function(inp, ctx, unknown_ops, target_op, totals):
    skip_string(inp, ctx)
    read(inp, ctx.int_size * 2)
    read(inp, 3)
    n, hits = scan_code(inp, ctx, unknown_ops, totals, target_op)
    totals[0] += n
    totals[1] += hits
    skip_constants(inp, ctx)
    skip_upvalues(inp, ctx)
    n_protos = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n_protos):
        scan_function(inp, ctx, unknown_ops, target_op, totals)
    skip_debug(inp, ctx)


def scan_file(path, target_op):
    unknown_ops = set()
    totals = [0, 0]  # total instr, target hits
    with open(path, "rb") as inp:
        read(inp, 12)
        int_size, sizet_size, inst_size, integer_size, number_size = read(inp, 5)
        read(inp, integer_size)
        read(inp, number_size)
        ctx = Ctx(int_size, sizet_size, integer_size, number_size)
        read(inp, 1)
        scan_function(inp, ctx, unknown_ops, target_op, totals)
    return unknown_ops, totals[0], totals[1]


def main():
    target_op = int(sys.argv[1])
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    hits = []
    for fn in os.listdir(DUMP_DIR):
        if not fn.endswith(".luac"):
            continue
        path = os.path.join(DUMP_DIR, fn)
        try:
            unknown_ops, n, target_hits = scan_file(path, target_op)
        except Exception:
            continue
        if unknown_ops == {target_op}:
            hits.append((n, fn, target_hits))
    hits.sort()
    for n, fn, th in hits[:limit]:
        print(f"{n:3d} instr, target op x{th}  {fn}")


if __name__ == "__main__":
    main()
