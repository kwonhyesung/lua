#!/usr/bin/env python3
# dump/ 안의 모든 .luac를 훑어서, 지금 opcode 표(rewrite_opcodes.OPMAP)로 얼마나
# 커버되는지 통계를 낸다. 새 파일을 쓰지 않고 메모리에서만 처리(6339개라 빠르게).
import sys
import os
import struct
from collections import Counter

sys.path.insert(0, os.path.dirname(__file__))
from rewrite_opcodes import OPMAP, read, Ctx, read_int as _unused  # reuse helpers

DUMP_DIR = r"C:\Users\kwon\Desktop\luahook\out\dump"


def scan_code(inp, ctx, unmapped_counter, used_counter):
    n = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n):
        val = struct.unpack("<I", read(inp, 4))[0]
        op = val & 0x3F
        used_counter[op] += 1
        if op not in OPMAP:
            unmapped_counter[op] += 1


def skip_string(inp, ctx):
    b0 = read(inp, 1)[0]
    if b0 == 0:
        return
    if b0 == 0xFF:
        sz = int.from_bytes(read(inp, ctx.sizet_size), "little")
        read(inp, sz - 1)
    else:
        read(inp, b0 - 1)


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
            raise ValueError(f"bad const tag {t}")


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


def scan_function(inp, ctx, unmapped_counter, used_counter):
    skip_string(inp, ctx)  # source
    read(inp, ctx.int_size * 2)
    read(inp, 3)
    scan_code(inp, ctx, unmapped_counter, used_counter)
    skip_constants(inp, ctx)
    skip_upvalues(inp, ctx)
    n_protos = int.from_bytes(read(inp, ctx.int_size), "little", signed=True)
    for _ in range(n_protos):
        scan_function(inp, ctx, unmapped_counter, used_counter)
    skip_debug(inp, ctx)


def scan_file(path, unmapped_counter, used_counter):
    with open(path, "rb") as inp:
        read(inp, 12)
        int_size, sizet_size, inst_size, integer_size, number_size = read(inp, 5)
        read(inp, integer_size)
        read(inp, number_size)
        ctx = Ctx(int_size, sizet_size, integer_size, number_size)
        read(inp, 1)
        scan_function(inp, ctx, unmapped_counter, used_counter)
        leftover = inp.read()
        return len(leftover) == 0


def main():
    files = [f for f in os.listdir(DUMP_DIR) if f.endswith(".luac")]
    unmapped_counter = Counter()
    used_counter = Counter()
    clean = 0
    desynced = 0
    errored = 0
    for i, fn in enumerate(files):
        path = os.path.join(DUMP_DIR, fn)
        try:
            before = sum(unmapped_counter.values())
            ok = scan_file(path, unmapped_counter, used_counter)
            after = sum(unmapped_counter.values())
            if not ok:
                desynced += 1
            elif after == before:
                clean += 1
        except Exception as e:
            errored += 1
        if (i + 1) % 1000 == 0:
            print(f"...{i+1}/{len(files)}", file=sys.stderr)

    print(f"total files: {len(files)}")
    print(f"clean (all opcodes mapped, parse in sync): {clean}")
    print(f"desynced (parse ended with leftover bytes): {desynced}")
    print(f"errored (exception during parse): {errored}")
    print()
    print("top unmapped raw opcodes (by instruction-hit count):")
    for op, cnt in unmapped_counter.most_common(30):
        print(f"  raw {op:2d}: {cnt} hits  (used_total={used_counter[op]})")


if __name__ == "__main__":
    main()
