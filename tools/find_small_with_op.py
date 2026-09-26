#!/usr/bin/env python3
# raw op X를 포함하면서 명령 수가 적은 파일들을 찾는다(손으로 대조하기 쉬운 후보 고르기용).
import sys
import os
import struct

sys.path.insert(0, os.path.dirname(__file__))
from rewrite_opcodes import read, Ctx

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


def scan_code(inp, ctx, ops_seen, total_instr):
    n = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    total_instr[0] += n
    for _ in range(n):
        val = struct.unpack("<I", read(inp, 4))[0]
        ops_seen.add(val & 0x3F)


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


def scan_function(inp, ctx, ops_seen, total_instr):
    skip_string(inp, ctx)
    read(inp, ctx.int_size * 2)
    read(inp, 3)
    scan_code(inp, ctx, ops_seen, total_instr)
    skip_constants(inp, ctx)
    skip_upvalues(inp, ctx)
    n_protos = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n_protos):
        scan_function(inp, ctx, ops_seen, total_instr)
    skip_debug(inp, ctx)


def scan_file(path):
    ops_seen = set()
    total_instr = [0]
    with open(path, "rb") as inp:
        read(inp, 12)
        int_size, sizet_size, inst_size, integer_size, number_size = read(inp, 5)
        read(inp, integer_size)
        read(inp, number_size)
        ctx = Ctx(int_size, sizet_size, integer_size, number_size)
        read(inp, 1)
        scan_function(inp, ctx, ops_seen, total_instr)
    return ops_seen, total_instr[0]


def main():
    target_op = int(sys.argv[1])
    max_instr = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    hits = []
    for fn in os.listdir(DUMP_DIR):
        if not fn.endswith(".luac"):
            continue
        path = os.path.join(DUMP_DIR, fn)
        try:
            ops_seen, n = scan_file(path)
        except Exception:
            continue
        if target_op in ops_seen and n <= max_instr:
            hits.append((n, fn))
    hits.sort()
    for n, fn in hits[:limit]:
        print(f"{n:3d} instr  {fn}")


if __name__ == "__main__":
    main()
