# tools/find_sig.py — xlua.dll에서 lua_load 위치를 찾아 바이트 시그니처를 출력한다.
# 사슬: "attempt to load a" 문자열 → f_parser → (lea 참조) luaD_protectedparser → (call 참조) lua_load
import struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM
try:
    from capstone.x86 import X86_REG_RIP
except ImportError:
    X86_REG_RIP = 0x29

PATH = r"C:\Nexon\MapleStory Worlds\msw_Data\Plugins\x86_64\xlua.dll"
SIG_LEN = 32  # lua_load 앞부분 몇 바이트를 시그니처로 쓸지

pe = pefile.PE(PATH)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()            # RVA로 인덱싱되는 전체 이미지
text = next(s for s in pe.sections if s.Name.startswith(b".text"))
t0, t1 = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize

# .pdata: (begin, end, parent_begin) 함수 범위 목록.
# MSVC는 오류 처리용 콜드 블록을 함수 밖으로 떼어내고 UNW_FLAG_CHAININFO(0x4)로 원 함수에 연결한다.
# 그 조각은 원 함수(FunctionEntry)에 속하는 것으로 취급한다.
funcs = sorted((e.struct.BeginAddress, e.struct.EndAddress,
                e.unwindinfo.FunctionEntry if e.unwindinfo and e.unwindinfo.Flags & 0x4 else e.struct.BeginAddress)
               for e in pe.DIRECTORY_ENTRY_EXCEPTION)

def func_of(rva):
    for b, e, parent in funcs:
        if b <= rva < e:
            return (parent, e) if parent == b else func_of(parent)
    raise SystemExit(f"no function contains RVA {rva:#x}")

def lea_refs(target):
    """target RVA를 lea r64,[rip+disp32]로 참조하는 명령 위치들"""
    hits = []
    for i in range(t0, t1 - 7):
        if img[i] in (0x48, 0x4C) and img[i+1] == 0x8D and (img[i+2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", img, i + 3)[0]
            if i + 7 + disp == target:
                hits.append(i)
    return hits

def call_refs(target):
    """target RVA를 E8 rel32로 호출하는 명령 위치들"""
    hits = []
    for i in range(t0, t1 - 5):
        if img[i] == 0xE8:
            rel = struct.unpack_from("<i", img, i + 1)[0]
            if i + 5 + rel == target:
                hits.append(i)
    return hits

def containing_funcs(refs):
    return sorted({func_of(r)[0] for r in refs})

# 1. 문자열
needle = b"attempt to load a %s chunk"
s = img.find(needle)
assert s > 0 and img.find(needle, s + 1) < 0, "string not unique"
print(f"string        RVA {s:#x}")

# 2. 문자열을 참조하는 함수 (checkmode 또는 인라인된 f_parser)
F = containing_funcs(lea_refs(s))
# 어디서도 참조되지 않는(call도 lea도 없는) 죽은 복사본은 제외 — 이 빌드엔 인라인 안 된 checkmode 잔재가 하나 남아 있다
F = [f for f in F if call_refs(f) or lea_refs(f)]
assert len(F) == 1, f"string referenced from {len(F)} functions: {F}"
F = F[0]
# checkmode가 인라인되지 않았으면, 함수포인터로 쓰이는(lea 참조되는) 함수가 나올 때까지 호출자를 따라 올라간다
while not lea_refs(F):
    callers = containing_funcs(call_refs(F))
    assert len(callers) == 1, f"expected 1 caller of {F:#x}, got {callers}"
    F = callers[0]
print(f"f_parser      RVA {F:#x}")

# 3. f_parser 주소를 lea로 넘기는 함수 = luaD_protectedparser
P = containing_funcs(lea_refs(F))
assert len(P) == 1, f"f_parser lea-referenced from {P}"
P = P[0]
print(f"protectedparser RVA {P:#x}")

# 4. 그것을 call하는 함수들. 이 빌드에선 lua_load가 호출자 대부분에 인라인돼 있어 한 곳이 아니다:
#    - '?' 문자열을 참조하는 함수 = lua_load 본체(독립 복사본 또는 lua_load가 인라인된 luaL_loadbufferx)
#    - 그 외 = lua_load가 인라인된 큰 함수(luaL_loadfilex, db_debug 등)
#    후킹 대상 = luaL_loadbufferx(L, buff, sz, name, mode): lua_load형이면서 export들이 호출하는 함수.
#    (게임은 C#에서 xluaL_loadbuffer로 바이트를 넘기므로 이 경로가 주 경로. 전체 커버는 ALT_SIG = luaD_protectedparser)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
exports = {e.address: e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}

def refs_q(b, e):
    """함수 본문이 '?\\0' 문자열을 RIP 상대로 참조하는가 (lua_load: chunkname NULL → "?")"""
    for ins in md.disasm(bytes(img[b:e]), b):
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and op.mem.disp:
                tgt = ins.address + ins.size + op.mem.disp
                if img[tgt:tgt+2] == b"?\x00":
                    return True
    return False

callers = containing_funcs(call_refs(P))
print(f"protectedparser callers ({len(callers)}):")
loadbufferx = None
for c in callers:
    cb, ce = func_of(c)
    q = refs_q(cb, ce)
    n_callers = len(containing_funcs(call_refs(cb)))
    kind = "lua_load-shaped" if q else "lua_load inlined into larger fn"
    n_exp = sum(1 for x in containing_funcs(call_refs(cb)) if x in exports)
    print(f"  {cb:#x} size={ce-cb:<4} callers={n_callers} (exports={n_exp}) {kind}")
    # lua_load 인라인 + getS 리더(luaL_loadbufferx): 호출자 중 export가 있는 lua_load형 함수
    if q and n_exp:
        assert loadbufferx is None, f"two export-called lua_load-shaped callers: {loadbufferx:#x}, {cb:#x}"
        loadbufferx = cb
assert loadbufferx, "no export-called lua_load-shaped caller of protectedparser"
print(f"loadbufferx   RVA {loadbufferx:#x}")

# 5. 검증: 5인자 형태 — 5번째 인자(mode)를 스택 홈([rsp+disp], disp>=0x28)에서 r9로 읽어 P에 넘겨야 한다
Lb, Le = func_of(loadbufferx)
mode_from_stack = any(
    ins.mnemonic == "mov" and ins.op_str.startswith("r9, qword ptr [rsp + ")
    and ins.operands[1].mem.disp >= 0x28
    for ins in md.disasm(bytes(img[Lb:Le]), Lb))
assert mode_from_stack, "sanity failed: loadbufferx does not read a 5th stack arg into r9"
print("sanity        OK ('?' substitution + 5th arg read from stack into r9)")

# 6. 시그니처: 앞 SIG_LEN 바이트, RIP상대/큰 imm/call·jmp 목표는 ??
def signature(Lb):
    sig = []
    for ins in md.disasm(bytes(img[Lb:Lb+SIG_LEN+16]), Lb):
        if ins.address - Lb >= SIG_LEN:
            break
        raw = list(ins.bytes)
        wild = set()
        if ins.disp_size and any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands):
            wild |= set(range(ins.disp_offset, ins.disp_offset + ins.disp_size))
        if ins.imm_size and ins.imm_size >= 4:
            wild |= set(range(ins.imm_offset, ins.imm_offset + ins.imm_size))
        if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
            wild |= set(range(1, len(raw)))
        sig += ["??" if k in wild else f"{b:02X}" for k, b in enumerate(raw)]
    return sig

# 7. 유일성 검증 (파일 전체)
def matches(buf, pat):
    n = len(pat); out = []
    for i in range(len(buf) - n + 1):
        if all(p is None or buf[i+k] == p for k, p in enumerate(pat)):
            out.append(i)
    return out

def unique_sig(Lb, label):
    sig = signature(Lb)
    pat = [None if x == "??" else int(x, 16) for x in sig]
    m = matches(img, pat)
    assert m == [Lb], f"{label} signature not unique: {[hex(x) for x in m]}"
    return " ".join(sig)

print(f"SIG: {unique_sig(loadbufferx, 'loadbufferx')}")
print("unique        OK  (target: luaL_loadbufferx)")
try:
    print(f"ALT_SIG (luaD_protectedparser {P:#x}, all load paths): {unique_sig(P, 'protectedparser')}")
except AssertionError as ex:
    print(f"ALT_SIG unavailable: {ex}")
