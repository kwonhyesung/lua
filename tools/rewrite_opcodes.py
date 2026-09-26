#!/usr/bin/env python3
# 이 게임(Meram, Lua 5.3 컨테이너, opcode 번호만 뒤섞임)의 .luac를 읽어서
# opcode 필드(각 명령의 하위 6비트)만 표준 Lua 5.3 번호로 바꿔치기하고
# 나머지 바이트는 그대로 복사한 표준 .luac를 만든다.
# (REVIEW.md 방식 재현: 표준 컨테이너 구조는 그대로, opcode만 재매핑)
import struct
import sys

# raw(이 게임의 뒤섞인 번호) -> 실제 Lua 5.3 opcode 번호
# 실제 번호는 lua/lua v5.3 lopcodes.h의 OpCode enum 순서로 확정(OP_BNOT 포함, 5.3에 추가된 것 주의):
#   0 MOVE 1 LOADK 2 LOADKX 3 LOADBOOL 4 LOADNIL 5 GETUPVAL 6 GETTABUP 7 GETTABLE
#   8 SETTABUP 9 SETUPVAL 10 SETTABLE 11 NEWTABLE 12 SELF 13 ADD 14 SUB 15 MUL
#   16 MOD 17 POW 18 DIV 19 IDIV 20 BAND 21 BOR 22 BXOR 23 SHL 24 SHR 25 UNM
#   26 BNOT 27 NOT 28 LEN 29 CONCAT 30 JMP 31 EQ 32 LT 33 LE 34 TEST 35 TESTSET
#   36 CALL 37 TAILCALL 38 RETURN 39 FORLOOP 40 FORPREP 41 TFORCALL 42 TFORLOOP
#   43 SETLIST 44 CLOSURE 45 VARARG 46 EXTRAARG
# raw -> semantic 은 D:\dllgo\original\REVIEW.md 의 분석 결과.
# REVIEW.md의 표는 원본과 피연산자 대조 결과 틀린 것으로 확인되어 전부 버림.
# 아래는 known-source .luac 3개(HandleButtonClickEvent/OnNoticeButton/OnBeginPlay)를
# 레지스터·상수 사용 패턴으로 직접 손으로 대조해서 확정한 값만 담음(disasm.py 사용).
# raw -> 실제 Lua 5.3 opcode 번호 (lua/lua v5.3 lopcodes.h 기준, OP_BNOT 포함 순서로 확정):
#   0 MOVE 1 LOADK 2 LOADKX 3 LOADBOOL 4 LOADNIL 5 GETUPVAL 6 GETTABUP 7 GETTABLE
#   8 SETTABUP 9 SETUPVAL 10 SETTABLE 11 NEWTABLE 12 SELF 13 ADD 14 SUB 15 MUL
#   16 MOD 17 POW 18 DIV 19 IDIV 20 BAND 21 BOR 22 BXOR 23 SHL 24 SHR 25 UNM
#   26 BNOT 27 NOT 28 LEN 29 CONCAT 30 JMP 31 EQ 32 LT 33 LE 34 TEST 35 TESTSET
#   36 CALL 37 TAILCALL 38 RETURN 39 FORLOOP 40 FORPREP 41 TFORCALL 42 TFORLOOP
#   43 SETLIST 44 CLOSURE 45 VARARG 46 EXTRAARG
OPMAP = {
    0:  44,  # CLOSURE  (확정: 매 청크 wrapper의 첫 명령, proto 생성과 정확히 일치)
    5:  30,  # JMP      (확정: if문 sBx 점프거리가 실제 분기 크기와 일치)
    12: 36,  # CALL     (확정: SELF 뒤 인자수/리턴수 패턴이 여러 번 일치)
    13: 34,  # TEST     (확정: if cond then 앞에서 항상 등장)
    14: 38,  # RETURN   (확정: 매 함수 끝 B=1/B=2 패턴)
    15: 7,   # GETTABLE (확정: self.필드 읽기 전부 일치)
    19: 6,   # GETTABUP (확정: ___MOD.필드 읽기 전부 일치)
    20: 27,  # NOT      (확정: not self.IsOpen)
    27: 0,   # MOVE     (확정: 지역변수를 인자 레지스터로 복사)
    30: 1,   # LOADK    (확정: 문자열 상수 로드 Bx 패턴 일치)
    51: 12,  # SELF     (확정: self:Method() 호출 전부 일치)
    60: 10,  # SETTABLE (확정: self.필드 = 값 대입 전부 일치)
    21: 4,   # LOADNIL  (추정: Init(msg,type,nil) 트레일링 nil 패턴, B=C=0)
    26: 3,   # LOADBOOL (확정: IsUserAppearance B=0→false, IsCreature B=1→true)
    63: 31,  # EQ       (확정: IsFailed/IsActive의 "필드==nil" JMP+LOADBOOL/LOADBOOL 패턴)
    2:  11,  # NEWTABLE (확정: CheatClearDummyInfos, self.DummyInfos = {})
    23: 5,   # GETUPVAL (확정: self를 upvalue로 캡처한 클로저의 첫 명령)
    9:  32,  # LT       (확정: IsAlive, self.Hp > 0 → "0 < Hp"로 컴파일됨)
    10: 13,  # ADD      (확정: AddHp, self.Hp + value)
    22: 14,  # SUB      (확정: TStatCounter.Dec, self.Count - 1)
    56: 37,  # TAILCALL (확정: return self:GetAttr("DataId"), CALL 뒤 multiret+RETURN B=0 패턴)
    8:  29,  # CONCAT   (확정: Id.."-"..Label, B..C 레지스터 범위 연결)
    52: 43,  # SETLIST  (확정: {EquipTile,EquipColor} 테이블 리터럴)
    62: 15,  # MUL      (확정: days * DAY_ELAPSED_VALUE)
    50: 33,  # LE       (확정: SetAccountExpandedSlots의 0-클램프 비교)
    53: 28,  # LEN      (확정: MeramAvatars.Size, #self.Avatars)
    24: 18,  # DIV      (확정: SetMovementSpeed, 80/speed)
    6:  35,  # TESTSET  (추정: GetMapData, ctx and ctx.MapData 단축평가)
    61: 16,  # MOD      (확정: GetDaysUntilWeek, (diff+7)%7)
    48: 25,  # UNM      (확정: TakeDirectDamage -damage / GetZOrder -arg1, 두 파일 다 일치)
    57: 40,  # FORPREP  (확정: for i=1,10 do 준비+FORLOOP로 점프)
    7:  39,  # FORLOOP  (확정: for 루프 몸통 끝, 몸통 시작으로 역방향 점프)
    58: 41,  # TFORCALL (확정: for k,v in pairs(t) do, 반복자 호출)
    59: 42,  # TFORLOOP (확정: TFORCALL 뒤 몸통 시작으로 역방향 점프)
    4:  8,   # SETTABUP (확정: BindText, component.Text = text, A=업밸류 인덱스)
    3:  19,  # IDIV     (확정: CanSay, (Talk // 10) % 10 자릿수 추출)
    11: 20,  # BAND     (확정: IsCostumeSlotType, slotType & CostumeSlotBit)
    29: 21,  # BOR      (확정: TEnumFlags.SetFlags, Flags | flag)
    28: 26,  # BNOT     (확정: TEnumFlags.ClearFlags, ~flag 만들어서 BAND)
    17: 23,  # SHL      (확정: MeramUtils.MaxInt, (1 << (n-1)) - 1 부호있는 n비트 최대값 공식.
                # 처음엔 POW로 잘못 추정했었음 — Round에서 "그럴듯해 보인" 건 내가 지정한
                # 매핑을 도구가 그대로 출력한 것뿐, 독립 검증이 아니었다. 상수풀에 1밖에
                # 없어서 2^n일 수 없다는 게 결정적 반증.
    18: 24,  # SHR      (확정: GetInstanceNumber, (mapId & MASK) >> SHIFT_BIT 비트필드 추출)
    31: 17,  # POW      (확정: MeramUtils.Round, 10 ^ decimalPlaces — raw17과 헷갈렸던 진짜 raw값)
    1:  9,   # SETUPVAL (확정: IterDirections 반복자 클로저, upvalue i를 i+1로 갱신)
    49: 45,  # VARARG   (확정: string.format(fmt, ...) 스타일 가변인자 로깅 헬퍼, vararg=1 proto)
    # 나머지는 아직 미확인. 잘못 추측해서 노이즈 만들지 않도록 일부러 비워둠 —
    # unmapped_hits로 찍히는 값을 보고 known-source 파일 하나씩 더 대조해서 채울 것.
}


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
    d = read(inp, ctx.int_size)
    return int.from_bytes(d, "little", signed=True)


def copy_int(inp, outp, ctx):
    d = read(inp, ctx.int_size)
    outp.write(d)
    return int.from_bytes(d, "little", signed=True)


def copy_string(inp, outp, ctx):
    b0 = read(inp, 1)
    outp.write(b0)
    size_byte = b0[0]
    if size_byte == 0:
        return  # NULL string, no payload
    if size_byte == 0xFF:
        sz = read(inp, ctx.sizet_size)
        outp.write(sz)
        strlen = int.from_bytes(sz, "little") - 1
    else:
        strlen = size_byte - 1
    outp.write(read(inp, strlen))


def rewrite_code(inp, outp, ctx, unmapped_hits):
    sizecode = copy_int(inp, outp, ctx)
    for _ in range(sizecode):
        raw4 = read(inp, 4)
        val = struct.unpack("<I", raw4)[0]
        op = val & 0x3F
        if op in OPMAP:
            newop = OPMAP[op]
            val = (val & ~0x3F) | newop
        else:
            unmapped_hits.add(op)
        outp.write(struct.pack("<I", val))


def copy_constants(inp, outp, ctx):
    n = copy_int(inp, outp, ctx)
    for _ in range(n):
        tag = read(inp, 1)
        outp.write(tag)
        t = tag[0]
        if t == 0:          # nil
            pass
        elif t == 1:        # boolean
            outp.write(read(inp, 1))
        elif t == 3:        # float (lua_Number)
            outp.write(read(inp, ctx.number_size))
        elif t == 19:       # integer (lua_Integer)
            outp.write(read(inp, ctx.integer_size))
        elif t in (4, 20):  # short/long string
            copy_string(inp, outp, ctx)
        else:
            raise ValueError(f"unknown constant tag {t}")


def copy_upvalues(inp, outp, ctx):
    n = copy_int(inp, outp, ctx)
    for _ in range(n):
        outp.write(read(inp, 2))  # instack byte + idx byte
    return n


def copy_debug(inp, outp, ctx):
    n_line = copy_int(inp, outp, ctx)
    outp.write(read(inp, n_line * ctx.int_size))
    n_locals = copy_int(inp, outp, ctx)
    for _ in range(n_locals):
        copy_string(inp, outp, ctx)
        outp.write(read(inp, ctx.int_size * 2))  # startpc, endpc
    n_upval_names = copy_int(inp, outp, ctx)
    for _ in range(n_upval_names):
        copy_string(inp, outp, ctx)


def rewrite_function(inp, outp, ctx, unmapped_hits):
    copy_string(inp, outp, ctx)         # source
    outp.write(read(inp, ctx.int_size * 2))  # linedefined, lastlinedefined
    outp.write(read(inp, 3))            # numparams, is_vararg, maxstacksize
    rewrite_code(inp, outp, ctx, unmapped_hits)
    copy_constants(inp, outp, ctx)
    copy_upvalues(inp, outp, ctx)
    n_protos = copy_int(inp, outp, ctx)
    for _ in range(n_protos):
        rewrite_function(inp, outp, ctx, unmapped_hits)
    copy_debug(inp, outp, ctx)


def rewrite(path_in, path_out):
    unmapped_hits = set()
    with open(path_in, "rb") as inp, open(path_out, "wb") as outp:
        header = read(inp, 12)  # signature(4) version(1) format(1) LUAC_DATA(6)
        outp.write(header)
        int_size = read(inp, 1)[0]
        sizet_size = read(inp, 1)[0]
        inst_size = read(inp, 1)[0]
        integer_size = read(inp, 1)[0]
        number_size = read(inp, 1)[0]
        outp.write(bytes([int_size, sizet_size, inst_size, integer_size, number_size]))
        outp.write(read(inp, integer_size))  # LUAC_INT
        outp.write(read(inp, number_size))   # LUAC_NUM
        ctx = Ctx(int_size, sizet_size, integer_size, number_size)
        outp.write(read(inp, 1))  # sizeupvalues of main proto (single byte)
        rewrite_function(inp, outp, ctx, unmapped_hits)
        leftover = inp.read()
        if leftover:
            print(f"WARNING: {len(leftover)} bytes left unread at end of {path_in} -- parse likely desynced")
    if unmapped_hits:
        print(f"WARNING: unmapped raw opcodes seen: {sorted(unmapped_hits)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: rewrite_opcodes.py <in.luac> <out.luac>")
        sys.exit(1)
    rewrite(sys.argv[1], sys.argv[2])
    print(f"wrote {sys.argv[2]}")
