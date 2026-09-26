#!/usr/bin/env python3
# 원본 뜻을 몰라도 되는 순수 컨테이너 디스어셈블러: 각 명령의 raw opcode(0-63)와
# A/B/C(iABC) 또는 A/Bx(iABx)/A/sBx(iAsBx) 필드를 표준 비트 레이아웃대로만 뽑고,
# 상수 풀도 같이 출력한다. opcode "의미"는 전혀 모른 채로 순수 구조만 본다.
import struct
import sys


def read(f, n):
    d = f.read(n)
    if len(d) < n:
        raise EOFError(f"unexpected EOF wanting {n} bytes, got {len(d)}")
    return d


class Ctx:
    def __init__(self, int_size, sizet_size, integer_size, number_size):
        self.int_size = int_size
        self.sizet_size = sizet_size
        self.integer_size = integer_size
        self.number_size = number_size


def read_int(inp, ctx):
    return int.from_bytes(read(inp, ctx.int_size), "little", signed=True)


def read_string(inp, ctx):
    b0 = read(inp, 1)[0]
    if b0 == 0:
        return None
    if b0 == 0xFF:
        sz = int.from_bytes(read(inp, ctx.sizet_size), "little")
        strlen = sz - 1
    else:
        strlen = b0 - 1
    data = read(inp, strlen)
    return data.decode("utf-8", errors="replace")


def read_code(inp, ctx):
    n = read_int(inp, ctx)
    out = []
    for _ in range(n):
        raw4 = read(inp, 4)
        val = struct.unpack("<I", raw4)[0]
        op = val & 0x3F
        a = (val >> 6) & 0xFF
        c = (val >> 14) & 0x1FF
        b = (val >> 23) & 0x1FF
        bx = (val >> 14) & 0x3FFFF
        sbx = bx - 131071  # MAXARG_sBx offset for 18-bit signed field
        out.append((op, a, b, c, bx, sbx))
    return out


def read_constants(inp, ctx):
    n = read_int(inp, ctx)
    out = []
    for _ in range(n):
        t = read(inp, 1)[0]
        if t == 0:
            out.append(None)
        elif t == 1:
            out.append(bool(read(inp, 1)[0]))
        elif t == 3:
            out.append(struct.unpack("<d", read(inp, ctx.number_size))[0])
        elif t == 19:
            out.append(int.from_bytes(read(inp, ctx.integer_size), "little", signed=True))
        elif t in (4, 20):
            out.append(read_string(inp, ctx))
        else:
            raise ValueError(f"unknown constant tag {t}")
    return out


def read_upvalues(inp, ctx):
    n = read_int(inp, ctx)
    out = []
    for _ in range(n):
        d = read(inp, 2)
        out.append((d[0], d[1]))
    return out


def skip_debug(inp, ctx):
    n_line = read_int(inp, ctx)
    read(inp, n_line * ctx.int_size)
    n_locals = read_int(inp, ctx)
    for _ in range(n_locals):
        read_string(inp, ctx)
        read(inp, ctx.int_size * 2)
    n_upval_names = read_int(inp, ctx)
    names = [read_string(inp, ctx) for _ in range(n_upval_names)]
    return names


def read_function(inp, ctx, depth, out_lines):
    source = read_string(inp, ctx)
    linedefined = read_int(inp, ctx)
    lastlinedefined = read_int(inp, ctx)
    numparams, is_vararg, maxstack = read(inp, 3)
    code = read_code(inp, ctx)
    consts = read_constants(inp, ctx)
    upvals = read_upvalues(inp, ctx)
    n_protos = read_int(inp, ctx)
    proto_codes = []
    for _ in range(n_protos):
        proto_codes.append(read_function(inp, ctx, depth + 1, out_lines))
    upval_names = skip_debug(inp, ctx)

    out_lines.append(f"{'  '*depth}-- proto depth={depth} params={numparams} vararg={is_vararg} maxstack={maxstack} nups={len(upvals)}")
    out_lines.append(f"{'  '*depth}-- constants: {consts}")
    out_lines.append(f"{'  '*depth}-- upvalue names: {upval_names}")
    for i, (op, a, b, c, bx, sbx) in enumerate(code):
        kc = consts[c] if 0 <= c - 0 < 0 else None  # placeholder, real RK-check below
        # RK: bit 8 (0x100) of B/C set => constant index (val & 0xFF); else register
        def rk(x):
            if x & 0x100:
                idx = x & 0xFF
                return f"K[{idx}]={consts[idx]!r}" if 0 <= idx < len(consts) else f"K[{idx}]=?"
            return f"R{x}"
        out_lines.append(f"{'  '*depth}[{i:3d}] op={op:2d} A={a:3d} B={b:3d}({rk(b)}) C={c:3d}({rk(c)}) Bx={bx} sBx={sbx}")
    return code


def main():
    if len(sys.argv) != 2:
        print("usage: disasm.py <file.luac>")
        sys.exit(1)
    out_lines = []
    with open(sys.argv[1], "rb") as inp:
        header = read(inp, 12)
        int_size, sizet_size, inst_size, integer_size, number_size = read(inp, 5)
        read(inp, integer_size)  # LUAC_INT
        read(inp, number_size)   # LUAC_NUM
        ctx = Ctx(int_size, sizet_size, integer_size, number_size)
        read(inp, 1)  # sizeupvalues of main
        read_function(inp, ctx, 0, out_lines)
        leftover = inp.read()
        if leftover:
            out_lines.append(f"WARNING: {len(leftover)} leftover bytes -- parse desynced")
    print("\n".join(out_lines))


if __name__ == "__main__":
    main()
